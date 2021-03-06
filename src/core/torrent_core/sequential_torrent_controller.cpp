#include "sequential_torrent_controller.hpp"

#include "lookup_error.hpp"
#include "misc_utility.hpp"
#include "torrent_core_utility.hpp"

#include <libtorrent/file.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bitfield.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/extensions/metadata_transfer.hpp>

//#define T2H_DEEP_DEBUG

namespace t2h_core {

/**
 * Private hidden sequential_torrent_controller api 
 */
namespace details {

static inline void log_state_update_alerts(libtorrent::torrent_status const & status) 
{
	using libtorrent::torrent_status;

	TCORE_TRACE("Updated state for torrent '%s' :\n"
		"paused '%s', torrent state '%i', sequential download '%i', progress '%f', "
		"download rate '%i', num completed '%i'",
		status.handle.save_path().c_str(),
		status.paused ? "true" : "false",
		(int)status.state,
		status.sequential_download,
		status.progress,
		status.download_rate,
		status.num_complete)
}

} // namespace details

/**
 * Public sequential_torrent_controller api
 */

sequential_torrent_controller::sequential_torrent_controller() : 
	base_torrent_core_cntl(), 
	setting_manager_(),
	event_handler_(),
	session_ref_(NULL), 	
	shared_buffer_ref_(NULL),
	settings_()
{
}
	
sequential_torrent_controller::~sequential_torrent_controller() 
{
	session_ref_ = NULL; 
	shared_buffer_ref_ = NULL;
}

int sequential_torrent_controller::availables_categories() const 
{
	using libtorrent::alert;
	/* Tell libtorrent session we are ready to dispatch follow categories */
	return (alert::all_categories & 
			~(alert::dht_notification +
#if defined(T2H_CORE_NO_DETAILED_PROGRESS_NOTIFICATIONS)
			alert::progress_notification +
#endif // T2H_CORE_NO_DETAILED_PROGRESS_NOTIFICATIONS
#if defined(T2H_DEBUG) && defined(T2H_DEEP_DEBUG)
			alert::debug_notification +
#endif // T2H_DEEP_DEBUG
			alert::stats_notification));
} 

bool sequential_torrent_controller::set_session(libtorrent::session * session_ref) 
{
	try
	{
		update_settings();
		session_ref_ = session_ref;
	}
	catch (std::exception const &) 
	{
		return false;
	}
	return true;
}

void sequential_torrent_controller::set_event_handler(torrent_core_event_handler_ptr event_handler) 
{
	event_handler_ = event_handler;
}

void sequential_torrent_controller::set_setting_manager(setting_manager_ptr sets_manager) 
{
	setting_manager_ = sets_manager;
}

void sequential_torrent_controller::set_shared_buffer(details::shared_buffer * shared_buffer_ref) 
{
	shared_buffer_ref_ = shared_buffer_ref;
}

void sequential_torrent_controller::on_setup_core_session(libtorrent::session_settings & settings) 
{
	if (settings_.sequential_download)
		settings.prioritize_partial_pieces = true;
}

bool sequential_torrent_controller::add_torrent(details::torrent_ex_info_ptr ex_info)
{		
	using details::future_cast;
	using details::add_torrent_future;
	using boost::posix_time::seconds;
	
	// TODO add coments 
	details::scoped_future_promise_init<details::add_torrent_future> scoped_promise(ex_info->future); 
	session_ref_->async_add_torrent(ex_info->torrent_params);
	boost::system_time const timeout = 
		boost::get_system_time() + seconds(settings_.futures_timeouts.torrent_add_timeout);
	if (ex_info->future->timed_wait_result(timeout)) {
		ex_info->handle = future_cast<add_torrent_future>(ex_info->future)->handle;
		return ex_info->handle.is_valid();
	}
	return false;
}

void sequential_torrent_controller::dispatch_alert(libtorrent::alert * alert) 
{
	using namespace libtorrent;
	
	if (torrent_paused_alert * paused = alert_cast<torrent_paused_alert>(alert))  
		on_pause(paused);
	if (metadata_received_alert * mra = alert_cast<metadata_received_alert>(alert))
		on_metadata_recv(mra);
	if (file_completed_alert * file_completed = alert_cast<file_completed_alert>(alert)) 
		on_file_complete(file_completed);
	if (add_torrent_alert * add_alert = alert_cast<add_torrent_alert>(alert)) 
		on_add_torrent(add_alert);
	if (torrent_finished_alert * tor_finised_alert = alert_cast<torrent_finished_alert>(alert)) 
		on_finished(tor_finised_alert);
	if (piece_finished_alert * piece_fin_alert = alert_cast<piece_finished_alert>(alert)) 
		on_piece_finished(piece_fin_alert);
	if (state_changed_alert * state_ched_alert = alert_cast<state_changed_alert>(alert))  
		on_state_change(state_ched_alert);
	if (torrent_deleted_alert * deleted_alert = alert_cast<torrent_deleted_alert>(alert)) 
		on_deleted(deleted_alert);
	if (state_update_alert * state_alert = alert_cast<state_update_alert>(alert)) 
		on_update(state_alert);
	if (tracker_error_alert * te_alert = alert_cast<tracker_error_alert>(alert))
		on_tracker_error(te_alert);
}

void sequential_torrent_controller::setup_torrent(libtorrent::torrent_handle & handle) 
{
	if (settings_.partial_files_download) {
		std::vector<int> file_priorities = handle.file_priorities();
		std::fill(file_priorities.begin(), file_priorities.end(), 0);
		handle.prioritize_files(file_priorities);
	}

	handle.set_max_connections(settings_.max_connections_per_torrent);
	// disabling settings.auto_upload_slots and setting max_uploads to INT_MAX
	// turns all choking off
	handle.set_max_uploads(settings_.max_uploads); 	
	// set to no limits
	handle.set_upload_limit(settings_.upload_limit); 
	handle.set_download_limit(settings_.download_limit); 
	handle.set_sequential_download(settings_.sequential_download);

#if !defined(TORRENT_NO_DEPRECATE)
	handle.set_local_upload_rate_limit(settings_.upload_limit);
	handle.set_local_download_rate_limit(settings_.download_limit);
#endif // TORRENT_NO_DEPRECATE

#if !defined(TORRENT_DISABLE_RESOLVE_COUNTRIES)
	handle.resolve_countries(settings_.resolve_countries); 
#endif
}

void sequential_torrent_controller::update_settings() 
{
	/* Just get all settings from the settings manager */
	settings_.max_partial_download_size = setting_manager_->get_value<int>("tc_max_partial_download_size");
	settings_.tc_root = setting_manager_->get_value<std::string>("tc_root");
	settings_.auto_error_resolving = setting_manager_->get_value<bool>("tc_auto_error_resolving");
	settings_.resolve_checkout = setting_manager_->get_value<std::size_t>("tc_resolve_checkout");
	settings_.resolve_countries = setting_manager_->get_value<bool>("tc_resolve_countries");
	settings_.sequential_download = setting_manager_->get_value<bool>("tc_sequential_download");
	settings_.download_limit = setting_manager_->get_value<int>("tc_download_limit");
	settings_.upload_limit = setting_manager_->get_value<int>("tc_upload_limit");
	settings_.max_uploads = setting_manager_->get_value<int>("tc_max_uploads");
	settings_.max_connections_per_torrent = setting_manager_->get_value<std::size_t>("tc_max_connections_per_torrent");
	settings_.partial_files_download = setting_manager_->get_value<bool>("tc_partial_files_download");
	settings_.futures_timeouts.torrent_add_timeout = setting_manager_->get_value<std::size_t>("tc_futures_timeout");
}

/**
 * Private sequential_torrent_controller api
 */

void sequential_torrent_controller::on_metadata_recv(libtorrent::metadata_received_alert * alert) 
{	
	std::vector<char> buffer;
	libtorrent::torrent_handle & handle = alert->handle;
	libtorrent::torrent_info const & torrent_info = handle.get_torrent_info();
	libtorrent::create_torrent torrent(torrent_info);
	libtorrent::entry torrent_enrty = torrent.generate();
		
	libtorrent::bencode(std::back_inserter(buffer), torrent_enrty);
	std::string torrent_file_path = torrent_info.name() + "." + 
		libtorrent::to_hex(torrent_info.info_hash().to_string()) + 
		".torrent";

	torrent_file_path = libtorrent::combine_path(settings_.tc_root, torrent_file_path);
	if (details::save_file(torrent_file_path, buffer) == -1) {
		TCORE_WARNING("can not save torrent meta to file, torrent name '%s'",
			alert->handle.get_torrent_info().name().c_str())
		return;
	}		
} 

void sequential_torrent_controller::on_add_torrent(libtorrent::add_torrent_alert * alert) 
{
	using details::future_cast;
	using libtorrent::torrent_info;	
	using libtorrent::torrent_handle;
	using details::add_torrent_future_ptr;
	using details::scoped_future_promise_init;	

	torrent_handle handle = alert->handle;		
	details::add_torrent_future_ptr add_future;
	
	if (alert->error || !handle.is_valid()) { 
		TCORE_WARNING("Torrent '%s' add failed with error '%i'", alert->handle.save_path().c_str(), alert->error.value())
		return;
	}

	// TODO may be need to re-add torrent in two follow cases?	
	details::torrent_ex_info_ptr ex_info = shared_buffer_ref_->get(handle.save_path());
	if (!ex_info) {
		TCORE_WARNING("Torrent '%s' add failed, can not find extended info", handle.save_path().c_str())
		torrent_remove(handle);
		return;
	}

	if (!ex_info->future) {
		TCORE_WARNING("Torrent '%s' have not future promise", handle.save_path().c_str())
		torrent_remove(handle);
		return;
	} 
	
	int index = 0;
	torrent_info const & ti = handle.get_torrent_info();
	for (torrent_info::file_iterator first = ti.begin_files(); 
		first != ti.end_files(); 
		++first, ++index) 
	{
		details::file_info_ptr fi = 
			details::file_info_add(ex_info->avaliables_files, 
				ti.files().at(first), 
				ti, 
				handle, 
				index,
				settings_.max_partial_download_size);
		event_handler_->on_file_add(fi->path, fi->size);
	} // for
	
	details::scoped_future_release future_release(ex_info->future);	
	details::add_torrent_future_ptr atf_ptr = details::future_cast<details::add_torrent_future>(ex_info->future);
	setup_torrent(handle);
	atf_ptr->handle = handle;
}

void sequential_torrent_controller::on_finished(libtorrent::torrent_finished_alert * alert) 
{
	TCORE_TRACE("Torrent finished '%s'", alert->handle.save_path().c_str())
} 

void sequential_torrent_controller::on_pause(libtorrent::torrent_paused_alert * alert) 
{
	details::torrent_ex_info_ptr ex_info = shared_buffer_ref_->get(alert->handle.save_path());
	if (!ex_info) {
		TCORE_WARNING("paused failed, args '%s'", alert->handle.save_path().c_str())
		torrent_remove(alert->handle);
		return;
	}

	for (details::file_info::list_type::const_iterator first = ex_info->avaliables_files.begin(), 
				last = ex_info->avaliables_files.end();
		first != last; 
		++first) 
	{
		event_handler_->on_pause((*first)->path);
	}
}

void sequential_torrent_controller::on_update(libtorrent::state_update_alert * alert) 
{
	using namespace libtorrent;
	
	typedef std::vector<torrent_status> statuses_type;
	
	details::torrent_ex_info_ptr ex_info;
	statuses_type statuses = alert->status;

	for (statuses_type::iterator it = statuses.begin(), last = statuses.end();
		it != last;
		++it) 
	{
#if defined(T2H_DEEP_DEBUG)
		details::log_state_update_alerts(*it);
#endif
		if (!(ex_info = shared_buffer_ref_->get(it->handle.save_path()))) {
			TCORE_WARNING("Can not find extended info for torrent '%s'", it->handle.save_path().c_str())
			torrent_remove(it->handle);
			continue;
		}
		// TODO improve auto resolving logic
		if (settings_.auto_error_resolving && 
			(utility::get_current_time() >= ex_info->last_resolve_checkout)) 
		{
			if (!ex_info->resolver)
				ex_info->last_resolve_checkout = 
					utility::get_current_time() + boost::posix_time::seconds(settings_.resolve_checkout);
			details::lookup_error lookuper(ex_info, *it);
		}

		on_torrent_status_changes(ex_info);	
	} // for
}

void sequential_torrent_controller::on_torrent_status_failure(details::torrent_ex_info_ptr ex_info) 
{
	// TODO add failure callbacks & actions	
}

void sequential_torrent_controller::on_torrent_status_changes(details::torrent_ex_info_ptr ex_info) 
{
	// TODO add progress callbacks
}

void sequential_torrent_controller::on_piece_finished(libtorrent::piece_finished_alert * alert) 
{
	// TODO may be in case of failure better way it resresh torrent not remove?
	details::file_info_ptr info;

	details::torrent_ex_info_ptr ex_info = shared_buffer_ref_->get(alert->handle.save_path());
	if (!ex_info) {
		TCORE_WARNING("get extended info failed, args '%s', '%i'", 
			alert->handle.save_path().c_str(), alert->piece_index)
		torrent_remove(alert->handle);
		return;
	} // if
	
	info = details::file_info_update(ex_info->avaliables_files, alert->handle, alert->piece_index);
	if (info)  
		event_handler_->on_progress_update(info->path, 
			(info->avaliable_bytes > info->size) ? info->size : info->avaliable_bytes);
}

void sequential_torrent_controller::on_file_complete(libtorrent::file_completed_alert * alert)
{
	details::file_info_ptr info;
	details::torrent_ex_info_ptr ex_info = shared_buffer_ref_->get(alert->handle.save_path());
	if (!ex_info) {
		TCORE_WARNING("get extended info failed, args '%s', '%i'", 
			alert->handle.save_path().c_str(), alert->index)
		torrent_remove(alert->handle);
		return;
	} // if

	// add update file_info
	info = details::file_info_search_by_index(ex_info->avaliables_files, alert->index);
	if (info) { 
		event_handler_->on_file_complete(info->path, info->size);
		details::file_info_reinit(info);
	}
	else
		TCORE_WARNING("cannot get file by index '%i', set complete state failed", alert->index)
}

void sequential_torrent_controller::on_deleted(libtorrent::torrent_deleted_alert * alert) 
{
	using details::file_info;
	
	details::torrent_ex_info_ptr ex_info = shared_buffer_ref_->get(alert->handle.save_path());
	if (ex_info) {
		for (file_info::list_type::const_iterator first = ex_info->avaliables_files.begin(), 
				last = ex_info->avaliables_files.end();
			first != last; 
			++first) 
		{
			event_handler_->on_file_remove((*first)->path);
		} // for
		shared_buffer_ref_->remove(alert->handle.save_path());
	} // if
}

void sequential_torrent_controller::on_tracker_error(libtorrent::tracker_error_alert * alert) 
{
	TCORE_WARNING("time '%i', status '%i', error '%i', msg '%s'", 
		alert->times_in_row, alert->status_code, alert->error.value(), alert->msg.c_str())
	alert->handle.clear_error();
	if (alert->status_code != 200 || alert->error.value() > 0) 
		alert->handle.force_reannounce();
}

void sequential_torrent_controller::torrent_remove(libtorrent::torrent_handle & handle)
{
	session_ref_->remove_torrent(handle);
}


void sequential_torrent_controller::on_state_change(libtorrent::state_changed_alert * alert) 
{
	// TODO investigate this case
}	

}// namespace t2h_core 

