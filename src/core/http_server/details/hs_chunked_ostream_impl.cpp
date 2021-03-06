#include "hs_chunked_ostream_impl.hpp"

#include "http_server_macroses.hpp"

#include <boost/iostreams/operations.hpp>  
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/device/file_descriptor.hpp> 

//#define T2H_DEEP_DEBUG

namespace t2h_core { namespace details {

/**
 *
 */

/**
 * Public hs_chunked_ostream_impl api
 */

hs_chunked_ostream_impl::hs_chunked_ostream_impl(
	http_server_ostream_policy_params const & base_params, hs_chunked_ostream_params const & params) 
	: base_chunked_ostream(base_params, params), params_(params), ex_data_() 
{
	// Make sure about ZERO init of ex_data_ struct 
	ex_data_.avaliable_bytes = 0;
	ex_data_.state = hs_chunked_ostream_impl::state_default;
}

hs_chunked_ostream_impl::~hs_chunked_ostream_impl() 
{
	on_break();
}

void hs_chunked_ostream_impl::on_bytes_avaliable_change(boost::int64_t avaliable_bytes) 
{
#if defined(T2H_DEEP_DEBUG)
	HCORE_TRACE("bytes updated notification : avaliable_bytes is '%i'", avaliable_bytes)
#endif // T2H_DEEP_DEBUG
	boost::mutex::scoped_lock guard(ex_data_.waiter_lock);
	ex_data_.avaliable_bytes = avaliable_bytes;
	guard.unlock();
	ex_data_.waiter.notify_one();
}

void hs_chunked_ostream_impl::on_break() 
{
#if defined(T2H_DEEP_DEBUG)
	HCORE_TRACE("stop notificatation") 
#endif // T2H_DEEP_DEBUG
	boost::mutex::scoped_lock guard(ex_data_.waiter_lock);
	ex_data_.state = hs_chunked_ostream_impl::is_breaked;
	guard.unlock();
	ex_data_.waiter.notify_one();
}

/**
 * Protected hs_chunked_ostream_impl api
 */

bool hs_chunked_ostream_impl::write_content_impl(http_data & hd) 
{
	/*  First we must to ensure about we have needed bytes in file,
	 	because real file size could be less then bytes_end(at moment of call). 
		if file size > than request/chunk then we 
		sync with torrent_core via blocking call 'wait_avaliable_bytes'.
		If bytes are avaliable then just send block to clien otherwise error. */
	namespace io = boost::iostreams;
		
	bool eof = false;	
	std::vector<char> iobuffer(params_.max_chunk_size + 10);
	std::ios::openmode const open_mode = std::ios::in | std::ios::binary;
	boost::int64_t read_offset = params_.max_chunk_size, seek_pos = hd.read_start;
	for(boost::int64_t readed = 0, writed = 0, bytes_wait = 0;
		ex_data_.state != hs_chunked_ostream_impl::is_breaked;) 
	{	
		if ((seek_pos + read_offset) >= hd.read_end) {
			bytes_wait = hd.read_end;
			eof = true;
		} else 
			bytes_wait = seek_pos + read_offset;
	
		if (!wait_for_bytes(bytes_wait)) {
			HCORE_WARNING("failed for bytes waiting for file '%s'", 
				hd.fi->file_path.c_str())		
			return false;
		} // if	

		{ // start file io scope	
		io::file_descriptor_source file_handle(hd.fi->file_path, open_mode);
		if (!file_handle.is_open()) { 
			HCORE_WARNING("failed to open file '%s'", hd.fi->file_path.c_str())
			return false;
		} // if
	
		if (io::seek(file_handle, seek_pos, BOOST_IOS::beg) < 0) { 
			HCORE_WARNING("failed to seek to pos '%i' file '%s'", 
				seek_pos, hd.fi->file_path.c_str())
			return false;
		} // if
		
		if ((readed =
			io::read(file_handle, &iobuffer.at(0), read_offset)) <= 0) 
		{ 
			HCORE_WARNING("failed to read file '%s' from '%i' to '%i'", 
				hd.fi->file_path.c_str(), seek_pos, readed)
			return false;
		} // if
		} // end file io scope
		
		if ((writed = ostream_impl_->write(&iobuffer.at(0), readed)) != readed) {
			HCORE_WARNING("failed to write data for '%s', writed %i", 
				hd.fi->file_path.c_str(), writed)
			return false;
		}
		
		if (eof)  
			return true;
	
		seek_pos = seek_pos + writed;
	} // for

	return false;
} 

/**
 * Private hs_chunked_ostream_impl api
 */

bool hs_chunked_ostream_impl::wait_for_bytes(boost::int64_t bytes) 
{
	using boost::posix_time::seconds;
	/*  Wait for notifications(with do extra test of bytes) till deadline not came,
	 	deadline update each update of ex_data_.avaliable_bytes*/
	boost::mutex::scoped_lock guard(ex_data_.waiter_lock);
	for (;;) {
		if (ex_data_.state == hs_chunked_ostream_impl::is_breaked)
			return false;

		if (bytes <= ex_data_.avaliable_bytes)
			return true;
		
		boost::system_time const waiter_deadline = boost::get_system_time() + seconds(params_.cores_sync_timeout); 
		if (!ex_data_.waiter.timed_wait(guard, waiter_deadline))
			return false;
	} // wait loop
	return true;
}

} } // namespace t2h_core, details

