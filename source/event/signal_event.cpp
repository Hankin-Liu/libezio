/****************************************************************************************
 * @file signal_event.cpp
 * @brief signal logic
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <sys/signal.h>
#include <memory>
#include "../../include/event/signal_event.h"
#include "../../include/util/macros_func.h"
#include "../../include/event_service.h"
#include "../../include/util/util.h"

namespace ezio {
    namespace event {
        signal_event::signal_event()
        {
            iov_.iov_base = &sig_info_;
            iov_.iov_len = sizeof(sig_info_);
        }

        signal_event::~signal_event()
        {
        }

        int32_t signal_event::open(event_service* evt_service_ptr, const signal_callback_t& cb)
        {
            STABLE_INFRA_CHECK_SUC(evt_service_ptr_ == nullptr && evt_service_ptr != nullptr, -1);
            STABLE_INFRA_CHECK_SUC(cb != nullptr, -1);
            auto read_cb = std::bind(&signal_event::handle_signal,
                    std::dynamic_pointer_cast<signal_event>(shared_from_this()), std::placeholders::_1);
            this->set_read_callback(read_cb);

            sigset_t mask;
            sigfillset(&mask);
            auto ret = pthread_sigmask(SIG_BLOCK, &mask, nullptr);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);

            auto sig_fd = ::signalfd(-1, &mask, 0); 
            fd_ = fd_t{ sig_fd, ezio::event::FD_TYPE::SIGNAL_FD };
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);

            auto ret1 = ezio::util::util_make_fd_nonblocking(fd_);
            STABLE_INFRA_CHECK_SUC(ret1 == 0, -1);
            ret1 = util::util_make_fd_close_on_exec(fd_);
            STABLE_INFRA_CHECK_SUC(ret1 == 0, -1);
                
            signal_handler_ = cb;
            
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.push_back(tp);

            // start to monitor this signal event
            evt_service_ptr_ = evt_service_ptr;
            evt_service_ptr_->add_event(shared_from_this());
            return 0;
        }

        void signal_event::handle_read_ready(fd_t fd)
        {
            const auto& read_cb = this->get_read_callback();
            submit_read_request(read_cb);
        }

        void signal_event::submit_read_request(const std::function<void(int32_t)>& cb)
        {
            if (STABLE_INFRA_LIKELY(cb == nullptr)) {
                auto ret = evt_service_ptr_->submit_async_read(fd_, &iov_, 1);
                STABLE_INFRA_ASSERT(ret == 0);
                return;
            }
            auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                cb(res);
            };
            auto ret = evt_service_ptr_->submit_async_read(fd_, &iov_, 1, tmp_cb);
            STABLE_INFRA_ASSERT(ret == 0);
        }

        void signal_event::handle_signal(int32_t res)
        {
            STABLE_INFRA_IF_TRUE_RETURN(res <= 0 || ! is_active_);
            // call user registered function
            signal_handler_(sig_info_.ssi_signo);
            submit_read_request();
        }

        int32_t signal_event::close()
        {
            ezio::event::event_base::close();
            is_active_ = false;
            evt_service_ptr_ = nullptr;
            signal_handler_ = nullptr;
            fd_.close();
            return 0;
        }
    }
}
