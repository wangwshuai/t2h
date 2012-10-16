#include "notification_unit.hpp"

#include <iterator>
#include <boost/assert.hpp>
#include <boost/functional/hash.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace common { namespace details {

/**
 *	Public notification_unit api
 */

notification_unit::notification_unit(notification_unit_param const & param) : 
	unit_name_(), 
	unit_id_(), 
	notifications_(),
	pending_notifications_(),
	lock_(),
	pn_lock_(),
	waiters_(),
	execution_loop_thread_(NULL),
	stop_work_(true),
	p_(param)
{
	BOOST_ASSERT(p_.recv) 
	boost::hash<std::string> unit_name_hasher;	
	unit_name_ = p_.recv->get_name();
	unit_id_ = unit_name_hasher(unit_name_);
}

notification_unit::~notification_unit() 
{
	boost::lock_guard<boost::mutex> guard(pn_lock_);
	if (execution_loop_thread_) {
		stop_work_ = true;
		waiters_.notify_all();
		execution_loop_thread_->join();
		delete execution_loop_thread_;
	} // execution_loop_thread_
}

void notification_unit::add_notification(notification_ptr notification) 
{
	boost::lock_guard<boost::mutex> guard(pn_lock_);
	if (!stop_work_ && pending_notifications_.size() <= p_.max_notifications) {
		pending_notifications_.push_back(notification);
		waiters_.notify_all();
	}
}

void notification_unit::execution_loop_run() 
{
	boost::lock_guard<boost::mutex> guard(pn_lock_);
	if (stop_work_) {
		stop_work_ = false;
		execution_loop_thread_ = 
			new boost::thread(&notification_unit::execution_loop, this);
	}
}

/**
 *	Private notification_unit api
 */

bool notification_unit::copy_pending_notifications_unsafe() 
{
	if (pending_notifications_.empty())
		return false;
	for(notifications_type::iterator first = pending_notifications_.begin(), 
			last = pending_notifications_.end();
		first != last; ++first) 
	{
		notifications_.push_back(*first);
		pending_notifications_.erase(first);
	}
	return true;
}

void notification_unit::notify_receiver() 
{
	for(notifications_type::iterator first = notifications_.begin(), 
			last = notifications_.end();
		first != last; ++first) 
	{
		if (first->get_state() != base_notification::ignore || 
			first->get_state() != base_notification::done) 
		{
			p_.recv->on_notify(*first);
			if(p_.change_state_auto)
				first->set_state(base_notification::done);
		} // (test)get_state
		notifications_.erase(first);
	}
}

void notification_unit::execution_loop() 
{
	boost::unique_lock<boost::mutex> guard(lock_);
	boost::system_time const check_timeout = 	
		boost::get_system_time() + boost::posix_time::milliseconds(p_.notification_check_timeout);
	for (bool has_pending_notifications = false;
		;
		has_pending_notifications = copy_pending_notifications()) 
	{
		{ // stop_work_ lock zone
		boost::lock_guard<boost::mutex> guard(pn_lock_);
		if (stop_work_) break;
		} // stop_work_ lock zone ned
		if (has_pending_notifications) 
			notify_receiver();
		else if (!has_pending_notifications) 	
			waiters_.timed_wait(guard, check_timeout);
	} // loop

	if (p_.execute_all_notification_at_exit) {
		if (copy_pending_notifications_unsafe()) 
			notify_receiver();
	} // execute_all_notification_at_exit
}

} } // namespace common, details
