/****************************************************************************************
 * @file signal_event.h
 * @brief signal logic
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <sys/signalfd.h>
#include <memory>
#include "../platform_define.h"
#include "./event_base.h"
#include "../util/macros_func.h"

namespace ezio {
    namespace event {
        class event_service;
        typedef std::function<void(const signo_t)> signal_callback_t;
        class signal_event : public event_base
        {
            constexpr signal_event(signal_event&) = delete;
            signal_event& operator=(signal_event&) = delete;
            public:
                signal_event();
                virtual ~signal_event();
            public:
                int32_t open(event_service* evt_service_ptr, const signal_callback_t& cb);
                int32_t close();
            private:
                virtual void handle_read_ready(fd_t fd) override;
                void handle_signal(int32_t res);
                void submit_read_request(const std::function<void(int32_t)>& cb = nullptr);
            private:
                fd_t fd_{};
                // close signal_event is a asynchronous action. after calling close function,
                // epoll maybe still trigger handler, use this flag to process this scenario
                bool is_active_{ true };
                struct signalfd_siginfo sig_info_{};
                ::iovec iov_{};
                signal_callback_t signal_handler_{ nullptr };
        };
    }
}
