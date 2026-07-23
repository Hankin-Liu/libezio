/****************************************************************************************
 * @file notify_event.cpp
 * @brief one thread notifies event to other thread
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/platform_define.h"
#if defined _OS_LINUX
#include <sys/eventfd.h>
#include <unistd.h>
#endif
#include <memory>
#include "../../include/util/util.h"
#include "../../include/util/macros_func.h"
#include "../../include/event/notifier.h"
#include "../../include/event_service.h"
#include "../../include/common/internal_type_def.h"

using namespace ezio::util;

namespace ezio {
    namespace event {
        notifier::notifier()
        {
            iov_ptr_ = std::make_shared<::iovec>();
            iov_ptr_->iov_base = &callback_para_;
            iov_ptr_->iov_len = sizeof(callback_para_);
        }

        notifier::~notifier()
        {
            if (fd_ != INVALID_FD) {
                util_closesocket(fd_);
            }
        }

        //void notify_event_producer::remove_all_consumers()
        //{
        //    std::lock_guard<std::mutex> tmp(mtx_consumers_);
        //    STABLE_INFRA_IF_TRUE_RETURN(consumers_.empty());
        //    for (auto& kv : consumers_) {
        //        kv.second->close();
        //    }
        //    consumers_.clear();
        //}
        //
        //int32_t notify_event_producer::remove_consumer(uint32_t consumer_id)
        //{
        //    std::lock_guard<std::mutex> tmp(mtx_consumers_);
        //    STABLE_INFRA_IF_TRUE_RETURN_CODE(consumers_.empty(), -1);
        //    auto iter = consumers_.find(consumer_id);
        //    STABLE_INFRA_CHECK_SUC(iter != consumers_.end(), -1);
        //    iter->second->close();
        //    consumers_.erase(iter);
        //    if (! consumers_.empty()) {
        //        if (shared_data_ptr_->raise_priority_consumer_id_ == consumer_id) {
        //            auto iter_first_consumer = consumers_.begin();
        //            shared_data_ptr_->raise_priority_consumer_id_ = iter_first_consumer->first;
        //        }
        //    } else {
        //        shared_data_ptr_->raise_priority_consumer_id_ = 0;
        //    }
        //    return 0;
        //}

        int32_t notifier::open(event_service* evt_service_ptr, const notify_callback_t& cb,
                               const notify_callback_t& cb_for_stop)
        {
            STABLE_INFRA_CHECK_SUC(evt_service_ptr_ == nullptr && evt_service_ptr != nullptr
                                   && cb != nullptr && evt_state_ == EVENT_STATE::DEF, RET_ERR);
#if defined _OS_LINUX
            {
                auto fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                fd_ = fd_t{ fd, FD_TYPE::EVENT_FD };
            }
#endif
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), RET_ERR);
            auto read_cb = std::bind(&notifier::handle_notify,
                    std::dynamic_pointer_cast<notifier>(shared_from_this()), std::placeholders::_1);
            set_read_callback(read_cb);
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.push_back(tp);
            // start to monitor this timer event
            set_evt_service_ptr(evt_service_ptr);
            notify_handler_ = cb;
            stop_handler_ = cb_for_stop;
            evt_service_ptr_->add_event(shared_from_this());
            evt_state_ = EVENT_STATE::OPENED;
            return RET_SUC;
        }

        void notifier::handle_read_ready(fd_t fd)
        {
            const auto& read_cb = this->get_read_callback();
            submit_read_request(read_cb);
        }

        // thread 2 got the notification
        void notifier::handle_notify(int32_t ret)
        {
            STABLE_INFRA_IF_TRUE_RETURN(ret <= 0);
            // call user registered function
            notify_handler_();
            submit_read_request();
        }

        void notifier::submit_read_request(const std::function<void(int32_t)>& cb)
        {
            if (cb == nullptr) {
                auto ret = evt_service_ptr_->submit_async_read(fd_, iov_ptr_.get(), 1);
                STABLE_INFRA_ASSERT(ret == 0);
                return;
            }
            auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                cb(res);
            };
            auto ret = evt_service_ptr_->submit_async_read(fd_, iov_ptr_.get(), 1, tmp_cb);
            STABLE_INFRA_ASSERT(ret == 0);
        }


//        int notify_event_producer::reset()
//        {
//            remove_all_consumers();
//            shared_data_ptr_ = std::make_shared<shared_data>();
//            return RET_SUC;
//        }

//        int32_t notify_event_producer::add_consumer(event_loop* evt_loop, const notify_callback_t& cb, const notify_callback_t& cb_for_stop)
//        {
//            std::lock_guard<std::mutex> tmp(mtx_consumers_);
//            uint32_t consumer_id = 1; // consumer id is start from 1
//            if (! consumers_.empty()) {
//                auto iter = consumers_.rbegin();
//                consumer_id = iter->first + 1;
//            }
//
//            auto consumer = std::make_shared<notify_event_consumer>(consumer_id, shared_data_ptr_);
//            auto ret = consumer->open(evt_loop, cb, cb_for_stop);
//            STABLE_INFRA_CHECK_SUC(ret == RET_SUC, RET_ERR);
//            auto fd = consumer->get_fd();
//            fds_.push_back(fd);
//
//            consumers_[consumer_id] = consumer;
//            shared_data_ptr_->raise_priority_consumer_id_ = consumer_id;
//            return consumer_id;
//        }

        // thread 1 call this to notify other threads
        void notifier::notify(uint32_t weight)
        {
            //if (0 == weight) {
            //    ++shared_data_ptr_->weight_;
            //} else {
            //    shared_data_ptr_->weight_ += weight;
            //}
            //STABLE_INFRA_IF_TRUE_RETURN(shared_data_ptr_->priority_state_ == RUN_MODE::HIGH_PRIORITY_STATE_2);
#if defined _OS_LINUX
            uint64_t count = 1;
            int32_t done_size = 0;
            while (done_size != sizeof(uint64_t)) {
                done_size = write(fd_, &count, sizeof(uint64_t));
                if (done_size == -1) {
                    STABLE_INFRA_IF_TRUE_CONTINUE(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
                    return;
                }
            }
#endif
        }

        //bool notify_event_consumer::can_raise_priority(uint64_t weight)
        //{
        //    STABLE_INFRA_CHECK_SUC(weight >= last_weight_, false);
        //    return ((weight - last_weight_) >= shared_data_ptr_->threshold_to_raise_priority_);
        //}

        //bool notify_event_consumer::try_leave_priority()
        //{
        //    ++leave_priority_ticks_;
        //    if (leave_priority_ticks_ >= LEAVE_PRIORITY_THRESHOLD) {
//      //          LOG_BASE_EVENT("Try to leave HIGH priority! leave_priority_ticks_ = %hu, threshold = %hu,"
 //     //                  " real leave!", leave_priority_ticks_, LEAVE_PRIORITY_THRESHOLD);
        //        clear_leave_priority_ticks();
        //        return true;
        //    } else {
  //    //          LOG_BASE_EVENT("Try to leave HIGH priority! leave_priority_ticks_ = %hu, threshold = %hu,"
   //   //                  " leaving!", leave_priority_ticks_, LEAVE_PRIORITY_THRESHOLD);
        //        return false;
        //    }
        //}

        int32_t notifier::close()
        {
            STABLE_INFRA_CHECK_SUC(evt_state_ == EVENT_STATE::OPENED, RET_ERR);
            event_base::close();
            fd_.close();
            evt_state_ = EVENT_STATE::CLOSED;
            return RET_SUC;
        }

        void notifier::on_started(bool is_success)
        {
            event_base::on_started(is_success);
//            LOG_BASE_DEBUG("Notify event:%p is started %s!", this, (is_success) ? "successfully" : "failed");
        }

        void notifier::on_stopped(bool is_success)
        {
            if (is_success) {
                if (stop_handler_ != nullptr) {
                    stop_handler_();
                }
            }
            event_base::on_stopped(is_success);
 //           LOG_BASE_DEBUG("Notify event:%p is stopped %s!", this, (is_success) ? "successfully" : "failed");
        }

        void notifier::handle_loop_once()
        {
            STABLE_INFRA_IF_TRUE_RETURN(evt_state_ != EVENT_STATE::OPENED);
            // call user registered function
            notify_handler_();
        }

        notify_callback_t notifier::exchange_callback(const notify_callback_t& new_cb)
        {
            auto old = std::move(notify_handler_);
            notify_handler_ = std::move(new_cb);
            return old;
        }
    }
}
