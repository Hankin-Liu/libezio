/****************************************************************************************
 * @file event_loop.cpp
 * @brief event driver class, loop to process events
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <mutex>
#include <thread>
#include <memory>
#include "../../include/platform_define.h"
#include "../../include/event/event_loop.h"
#include "../../include/common/const_variable.h"
#include "../../include/common/internal_type_def.h"
#include "../../include/event/timer.h"
#include "../../include/event/notifier.h"
#include "../../include/event/event_common.h"
#if defined _OS_LINUX
#include <sys/eventfd.h>
#include <unistd.h>
#endif
using namespace std;

namespace ezio {
    namespace event {
        int32_t internal_notifier::open(event_loop* evt_loop, const notify_callback_t& cb)
        {
            STABLE_INFRA_CHECK_SUC(evt_loop != nullptr && evt_loop_ptr_ == nullptr, RET_ERR);
            evt_loop_ptr_ = evt_loop;
#if defined _OS_LINUX
            {
                auto fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                fd_ = fd_t{ fd, FD_TYPE::EVENT_FD };
            }
#endif
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), RET_ERR);
            iov_ptr_ = std::make_shared<::iovec>();
            iov_ptr_->iov_base = &callback_para_;
            iov_ptr_->iov_len = sizeof(callback_para_);
            auto tmp_cb = [cb, this](int32_t ret) {
                cb();
                this->submit_read_request();
            };
            submit_read_request(tmp_cb);
            return RET_SUC;
        }

        void internal_notifier::notify()
        {
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

        void internal_notifier::submit_read_request(const std::function<void(int32_t)>& cb)
        {
            return;
            const auto& poll_ptr = evt_loop_ptr_->get_poll();
            if (cb == nullptr) {
                auto ret = poll_ptr->submit_async_read(fd_, iov_ptr_.get(), 1, nullptr, &read_options().set_offset(0));
                STABLE_INFRA_ASSERT(ret == 0);
                return;
            }
            auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                cb(res);
            };
            auto ret = poll_ptr->submit_async_read(fd_, iov_ptr_.get(), 1, tmp_cb, &read_options().set_offset(0));
            STABLE_INFRA_ASSERT(ret == 0);
        }

        event_loop::event_loop() 
        {
        }

        event_loop::~event_loop()
        {
        }

        void event_loop::add_event(const event_base::pointer_t& eh)
        {
            if (ee_state_ == EL_STATE::ES_INIT || ee_state_ == EL_STATE::ES_START) {
                event_info evt_info;
                evt_info.evt_ptr_ = eh;
                evt_info.instruction_ = EL_INSTRUCTION::EI_ADD;
                evt_info.fds_evts_ = eh->get_fds_and_evts();
                std::lock_guard<std::mutex> lock(event_handler_mtx_);
                evt_handlers_cache_.emplace_back(evt_info);
            }
        }

        void event_loop::remove_event(const event_base::pointer_t& eh)
        {
            if (ee_state_ == EL_STATE::ES_INIT || ee_state_ == EL_STATE::ES_START) {
                event_info evt_info;
                evt_info.evt_ptr_ = eh;
                evt_info.instruction_ = EL_INSTRUCTION::EI_REMOVE;
                evt_info.fds_evts_ = eh->get_fds_and_evts();
                std::lock_guard<std::mutex> lock(event_handler_mtx_);
                evt_handlers_cache_.emplace_back(evt_info);
            }
        }

        void event_loop::start_loop() {
            if (ee_state_ != EL_STATE::ES_INIT && ee_state_ != EL_STATE::ES_STOP) {
                return;
            }
            if(thread_id_ != this_thread::get_id()) {
                thread_id_ = this_thread::get_id();
            }
            ee_state_ = EL_STATE::ES_START;
            while(true) {
                if (ee_state_ == EL_STATE::ES_START) {
                    this->loop_once(polling_ms_);
                } else {
                    break;
                }
            }
        }

        void event_loop::process_remove_instruction(const event_info& evt_info)
        {
            auto& ptr = evt_info.evt_ptr_;
            auto& fd_evts = evt_info.fds_evts_;
            // the event pointer can't be destructed here, maybe it will be used in dispatch function,
            // so move it to cache named removed_evt_handlers_cache_
            removed_evt_handlers_cache_.insert(ptr);
            // if this is a high priority event_base, or this event_base is in busy polling now.
            auto cnt = busy_poll_evts_.erase(ptr);
            cnt += (ptr->get_event_type() == event_base::EVENT_TYPE::NORMAL ?
                    evt_handlers_.erase(ptr) : 0);
            bool is_suc = true;
            if (1 <= cnt) {
                for (const auto &fd_evt : fd_evts) {
                    const auto& fd = std::get<0>(fd_evt);
                    const auto& evt = std::get<1>(fd_evt);
                    if (evt & EV_READ) {
                        const auto& cb = ptr->get_cancel_read_callback();
                        auto ret = poll_->cancel_async_read(fd, cb);
                        if (ret != 0) {
                            is_suc = false;
                            continue;
                        }
                    }
                    if (evt & EV_WRITE) {
                        const auto& cb = ptr->get_cancel_write_callback();
                        auto ret = poll_->cancel_async_write(fd, cb);
                        if (ret != 0) {
                            is_suc = false;
                            continue;
                        }
                    }
                }
            }
            if (is_suc) {
                ptr->on_stopped(true);
            } else {
                ptr->on_stopped(false);
            }
        }

        void event_loop::process_add_instruction(const event_info& evt_info)
        {
            auto& ptr = evt_info.evt_ptr_;
            auto& fd_evts = evt_info.fds_evts_;
            auto ret = (ptr->get_event_type() == event_base::EVENT_TYPE::NORMAL ?
                        evt_handlers_.insert(ptr) : busy_poll_evts_.insert(ptr));
            STABLE_INFRA_IF_TRUE_RETURN(! ret.second);
            for (const auto &fd_evt : fd_evts) {
                const auto& fd = std::get<0>(fd_evt);
                const auto& evts = std::get<1>(fd_evt);
                int32_t ret = -1;
                if (evts & EV_READ) {
                    auto cb = std::bind(&event_base::handle_event_ready, ptr, std::placeholders::_1, fd, true);
                    ret = poll_->submit_async_read(fd, nullptr, 0, std::move(cb));
                    STABLE_INFRA_ASSERT(ret == 0);
                }
                if (evts & EV_WRITE) {
                    auto cb = std::bind(&event_base::handle_event_ready, ptr, std::placeholders::_1, fd, false);
                    ret = poll_->submit_async_write(fd, nullptr, 0, std::move(cb));
                    STABLE_INFRA_ASSERT(ret == 0);
                }
            }
            ptr->on_started(true);
        }

        void event_loop::process_new_instruction()
        {
            evt_handlers_cache_t new_event_vec;
            {
                std::lock_guard<std::mutex> lock(event_handler_mtx_);
                evt_handlers_cache_.swap(new_event_vec);
            }
            for (const auto &evt_info : new_event_vec) {
                auto& instruction = evt_info.instruction_;
                if (EL_INSTRUCTION::EI_REMOVE == instruction) {
                    process_remove_instruction(evt_info);
                } else { // EL_INSTRUCTION::EI_ADD == instruction
                    process_add_instruction(evt_info);
                }
            }
        }

        void event_loop::process_other_thread_job()
        {
            {
                std::lock_guard<std::mutex> lock(pending_jobs_mtx_);
                other_thread_pending_funcs_.swap(other_thread_job_swap_cache_vec_);
            }
            for (auto& job : other_thread_job_swap_cache_vec_) {
                job();
            }
            other_thread_job_swap_cache_vec_.clear();
        }

        void event_loop::loop_once(int32_t timeout_ms)
        {
            if (STABLE_INFRA_UNLIKELY(! evt_handlers_cache_.empty())) {
                process_new_instruction();
            }

            // run busy poll jobs
            if (! busy_poll_evts_.empty()) {
                for (const auto &ptr : busy_poll_evts_) {
                    ptr->handle_loop_once();
                }
                timeout_ms = 0;
            }

            // do same thread's pending jobs
            if (! same_thread_pending_funcs_.empty()) {
                uint32_t pending_job_cnt = same_thread_pending_funcs_.size();
                for (uint32_t i = 0; i < pending_job_cnt; ++i) {
                    const auto& pending_func = same_thread_pending_funcs_.front();
                    pending_func();
                    same_thread_pending_funcs_.pop_front();
                }
                timeout_ms = 0;
            }

            poll_->dispatch(timeout_ms);

            // do other thread's pending jobs
            // if other thread add a job, it will wake up from dispatch
            if (! other_thread_pending_funcs_.empty()) {
                process_other_thread_job();
            }

            // remove event handlers which are removed before, remove after dispatch is safe
            if (! removed_evt_handlers_cache_.empty()) {
                removed_evt_handlers_cache_.clear();
            }
        }

        int32_t event_loop::run_in_event_loop(const pending_func& job)
        {
            if (job == nullptr) {
                return RET_ERR;
            }
            if (this_thread::get_id() != thread_id_) {
                std::lock_guard<std::mutex> lock(pending_jobs_mtx_);
                other_thread_pending_funcs_.push_back(job);
                wake_up();
            } else {
                same_thread_pending_funcs_.push_back(job);
            }
            return RET_SUC;
        }

        int32_t event_loop::open(const poll_param& param)
        {
            STABLE_INFRA_CHECK_SUC(ee_state_ == EL_STATE::ES_DEF, RET_ERR);
            poll_ = get_poll_obj(param.poll_type_);
            STABLE_INFRA_CHECK_SUC(poll_ != nullptr, RET_ERR);
            STABLE_INFRA_CHECK_SUC(poll_->init(param.entry_cnt_), RET_ERR);
            ee_state_ = EL_STATE::ES_INIT;
            if (param.polling_ms_ >= 0) {
                polling_ms_ = param.polling_ms_;
            } // else use default value -1

            ntf_evt_ptr_ = std::make_shared<internal_notifier>();
            auto cb = std::bind(&event_loop::handle_notify, this);
            auto ret_ntf = ntf_evt_ptr_->open(this, cb);
            STABLE_INFRA_CHECK_SUC(ret_ntf == RET_SUC, RET_ERR);

            auto cb_timer = std::bind(&event_loop::on_timer, this);
            internal_timer_id_ = poll_->create_timer(1, 0, cb_timer);
            STABLE_INFRA_CHECK_SUC(internal_timer_id_ >= 0, RET_ERR);
            return RET_SUC;
        }

        int32_t event_loop::on_timer() {
            // check if event raise to high priority event
            for (const auto& evt : evt_handlers_) {
                bool ret = evt->try_raise_priority();
                if (ret) {
            //        LOG_BASE_EVENT("change priority from NORMAL to HIGH");
                    busy_poll_evts_.insert(evt);
                } else {
                    if (! busy_poll_evts_.empty()) {
                        auto cnt = busy_poll_evts_.erase(evt);
                        if (cnt > 0) {
             //               LOG_BASE_EVENT("change priority from HIGH to NORMAL");
                        }
                    }
                }
                evt->clear_weight();
            }
            return RET_SUC;
        }

        void event_loop::wake_up()
        {
            if (ntf_evt_ptr_ != nullptr) {
                ntf_evt_ptr_->notify();
            }
        }

        void event_loop::handle_notify()
        {
            // do nothing
        }

        void event_loop::handle_close()
        {
            ee_state_ = EL_STATE::ES_STOP; 
        }

        int32_t event_loop::close() {
            ee_state_ = EL_STATE::ES_STOP; // if calls this in other thread, this thread may not see it immediately
            pending_func pf = std::bind(&event_loop::handle_close, this);
            run_in_event_loop(pf);
            return 0;
        }
    }
}
