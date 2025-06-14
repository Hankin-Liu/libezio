/****************************************************************************************
 * @file timer.h
 * @brief timer
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <memory>
#include "../platform_define.h"
#include "./event_base.h"
#include "../util/macros_func.h"

namespace ezio {
    namespace event {
#ifdef EPOLL_IS_SUPPORTED
        class epoll;
#endif
        typedef std::function<void(uint64_t)> timer_callback;
        class timer : public std::enable_shared_from_this<timer>
        {
#ifdef EPOLL_IS_SUPPORTED
            friend class ::ezio::event::epoll;
#endif
            constexpr timer(timer&) = delete;
            timer& operator=(timer&) = delete;
            public:
                timer();
                ~timer();
            private:
                void handle_timeout(int32_t ret);
                int32_t open(poll_base* poll_ptr, uint64_t interval_s, uint64_t interval_ns);
                int32_t close();
                int32_t reg_timer_handler(const timer_callback& cb);
            private:
                void call_handler(uint64_t count);
                void submit_read_request();
            private:
                // close timer is a asynchronous action. after calling close function,
                // epoll maybe still trigger handler, use this flag to process this scenario
                bool is_active_{ true };
                poll_base* poll_ptr_{ nullptr };
                fd_t fd_{};
                //EVENT_TYPE evt_type_{EVENT_TYPE::NORMAL};
                timer_callback timer_handler_{ nullptr };
                std::shared_ptr<::iovec> iov_ptr_{ nullptr };
                uint64_t* callback_para_{ nullptr };
        };
    }
}
