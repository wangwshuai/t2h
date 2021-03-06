#ifndef TORRENTS_INFO_EX_HPP_INCLUDED
#define TORRENTS_INFO_EX_HPP_INCLUDED

#include "base_resolver.hpp"
#include "torrent_core_future.hpp"

#include <map>
#include <vector>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

#if defined (__GNUG__)
#	pragma GCC system_header
#endif

#include <libtorrent/config.hpp>
#include <libtorrent/session.hpp>

#include <boost/tuple/tuple.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace t2h_core { namespace details {

/**
 *	File extended information
 */

struct file_info {
	/**
	 *	list of files for each torrent
	 */
	typedef boost::shared_ptr<file_info> ptr_type;
	typedef std::list<ptr_type> list_type;
	
	/**
	 * pieces and files priority types for details see 
	 */
	static int const off_prior = 0;
	static int const normal_prior = 1;
	static int const max_prior = 5;	
	static int const over_max_prior = 7;
	
	/**
	 * recheck_av states
	 */
	static int const off_recheck = -1;
	static int const recheck_limit = 5;

	int pieces;										// Pieces number in file
	int total_pieces_download_count;				// Count of downloaded pieces
	int pieces_download_count;						// Chocked count of downloaded pieces(not total)
	boost::int64_t avaliable_bytes;					// Seq. bytes which was downloaded and saved to the HDD(eg current size)
	int pieces_range_first;							// File piece range start offset
	int pieces_range_last;							// File piece range end offset 
	std::string path;								// Path to file(UTF8)
	boost::int64_t size;							// Size of file(eg real/expected file size)
	std::size_t block_size;							// Size of each pices(exclude last one)
	int file_index;									// File index, could be very useful in need to get some extended info from libtorrent::torrent_info::file_at
	int chocked_range;								// Chocked range of pieces download. Need to detect range for do a update avaliable_bytes 
	std::vector<int> av_pieces;						// Pieces set INT_MAX means piece not downloaded yet 
	std::size_t last_av_pos;						// Last position of av_pieces set 
	std::size_t end_av_pos;							// Last position + chocked_range -1 of av_pieces set
	std::size_t last_av_pieces_pos;					// Last position of pieces
	int recheck_av;									// set to off_recheck then sequential_download complete, else updater check current seq. each call
};

typedef file_info::ptr_type file_info_ptr;

/**
 * Public file_info api
 */

file_info_ptr file_info_add(file_info::list_type & flist, 
						libtorrent::file_entry const & fe, 
						libtorrent::torrent_info const & ti,
						libtorrent::torrent_handle const & handle,
						int file_index,
						int max_partial_download_size); 

file_info_ptr file_info_add_by_index(file_info::list_type & flist, 
								libtorrent::torrent_info const & info, 
								int file_index,
								int max_partial_download_size); 

void file_info_remove(file_info::list_type & flist, std::string const & path);
void file_info_remove(file_info::list_type & flist, int piece); 
void file_info_reset(file_info::list_type & flist); 

file_info_ptr file_info_bin_search(file_info::list_type const & flist, int piece); 
file_info_ptr file_info_search(file_info::list_type const & flist, std::string const path); 
file_info_ptr file_info_search_by_index(file_info::list_type const & flist, int index);

void file_info_set_pieces_priority(file_info::list_type & flist, 
									libtorrent::torrent_handle & handle, 
									int file_index, 
									int priority);

file_info_ptr file_info_update(file_info::list_type & flist, libtorrent::torrent_handle & handle, int piece); 

void file_info_reinit(file_info_ptr fi);

/**
 * Torrent extended informantion & functionality
 */

struct torrent_ex_info {
	typedef boost::shared_ptr<torrent_ex_info> ptr_type;
	typedef boost::intrusive_ptr<libtorrent::torrent_info> torrent_info_ptr;

	torrent_ex_info();
	
	/* Extended functionality */
	static bool initialize_f(
		ptr_type ex_info, boost::filesystem::path const & save_root, boost::filesystem::path const & path);
	static bool prepare_f(
		ptr_type ex_info, boost::filesystem::path const & save_root, boost::filesystem::path const & path);
	static void prepare_u(
		ptr_type ex_info, boost::filesystem::path const & save_root, std::string const & url);
	static bool prepare_sandbox(ptr_type ex_info);
	
	/* Extended data filds */
	base_resolver_ptr resolver;									// Error reslover
	boost::posix_time::time_duration last_resolve_checkout;		// Error checking duration 
	details::torrent_core_future_ptr future;					// Future callback

	libtorrent::torrent_handle handle;							// libtorrent torrent handle
	libtorrent::add_torrent_params torrent_params;				// libtorrent add torrent params

	std::string sandbox_dir_name;								// sandbox directory name
	std::size_t index;											// Torrent index(eg hash)
	file_info::list_type avaliables_files;						// files
};

typedef torrent_ex_info::ptr_type torrent_ex_info_ptr;

/**
 * Torrent extended info helpers 
 */
struct default_on_path_process {
	inline std::string operator()(std::string const & p) { return p; } 
};

std::string torrent_info_to_json(
	torrent_ex_info_ptr ex_info, 
	boost::function<std::string(std::string const &)> on_path_process = default_on_path_process());

} } // namespace t2h_core, details

#endif

