/****************************************************************************************
 * @file timer.cpp
 * @brief timer
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <sys/timerfd.h>
#include <memory>
#include "../../include/event/timer.h"
#include "../../include/util/util.h"
#include "../../include/event/event_loop.h"

namespace ezio {
    namespace event {
        timer::timer() {
            iov_ptr_ = std::make_shared<::iovec>();
            iov_ptr_->iov_base = &callback_para_;
            iov_ptr_->iov_len = 8;
        }

        timer::~timer() {
            if (fd_ != INVALID_FD) {
                close();
            }
        }

        int32_t timer::open(poll_base* poll_ptr, uint64_t interval_s, uint64_t interval_ns)
        { 
            STABLE_INFRA_CHECK_SUC(poll_ptr_ == nullptr && poll_ptr != nullptr, RET_ERR);
            constexpr int64_t NANOS_OF_ONE_SECONDS = (1000 * 1000 * 1000);
            const int32_t type = CLOCK_MONOTONIC;
            struct timespec now = { 0, 0 };
            struct itimerspec new_value = { {0, 0}, {0, 0} };
            new_value.it_value.tv_sec = now.tv_sec + interval_s + (now.tv_nsec + interval_ns) / NANOS_OF_ONE_SECONDS;
            new_value.it_value.tv_nsec = (now.tv_nsec + interval_ns) % NANOS_OF_ONE_SECONDS;
            new_value.it_interval.tv_sec = interval_s + interval_ns / NANOS_OF_ONE_SECONDS;
            new_value.it_interval.tv_nsec = interval_ns % NANOS_OF_ONE_SECONDS;
            STABLE_INFRA_CHECK_SUC(new_value.it_interval.tv_sec != 0 || new_value.it_interval.tv_nsec != 0, RET_ERR);

            auto fd = timerfd_create(type, 0); 
            fd_ = fd_t{ fd, FD_TYPE::TIMER_FD };
            STABLE_INFRA_CHECK_SUC(fd_ != INVALID_FD, RET_ERR);
            auto ret = ezio::util::util_make_fd_nonblocking(fd_);
            STABLE_INFRA_CHECK_SUC(ret == 0, RET_ERR);

            const int32_t flags = 0;
            ret = timerfd_settime(fd_, flags, &new_value, nullptr);
            STABLE_INFRA_CHECK_SUC(ret == 0, RET_ERR);

            poll_ptr_ = poll_ptr;
            callback_t read_cb = std::bind(&timer::handle_timeout,
                    shared_from_this(), std::placeholders::_1);
            auto tmp_cb = [read_cb](int32_t ret, const ::iovec*, uint32_t, void*) {
                read_cb(ret);
            };
            auto ret_read = poll_ptr_->submit_async_read(fd_, iov_ptr_.get(), 1, tmp_cb);
            STABLE_INFRA_CHECK_SUC(ret_read == 0, RET_ERR);

            return RET_SUC;
        }

        int32_t timer::close()
        {
            is_active_ = false;
            timer_handler_ = nullptr;
            STABLE_INFRA_IF_TRUE_RETURN_CODE(fd_ == INVALID_FD, RET_SUC);
            auto tmp_fd = fd_;
            fd_.close();
            auto ret = poll_ptr_->cancel_async_read(tmp_fd);
            STABLE_INFRA_CHECK_SUC(ret == 0, RET_ERR);
            return RET_SUC;
        }

        int32_t timer::reg_timer_handler(const timer_callback& cb)
        {
            if (timer_handler_ == nullptr) {
                timer_handler_ = cb;
                return RET_SUC;
            }
            return RET_ERR;
        }
        
        void timer::submit_read_request()
        {
            STABLE_INFRA_IF_TRUE_RETURN(! is_active_);
            auto ret = poll_ptr_->submit_async_read(fd_, iov_ptr_.get(), 1);
            STABLE_INFRA_ASSERT(ret == 0);
        }

        void timer::handle_timeout(int32_t ret)
        {
            STABLE_INFRA_IF_TRUE_RETURN(ret <= 0 || ! is_active_);
            call_handler(*(uint64_t*)iov_ptr_->iov_base);
            submit_read_request();
        }

        void timer::call_handler(uint64_t count)
        {
            STABLE_INFRA_IF_TRUE_RETURN(timer_handler_ == nullptr);
            timer_handler_(count);
        }
    }
}
