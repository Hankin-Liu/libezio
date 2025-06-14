/****************************************************************************************
 * @file event_thread.h
 * @brief one thread with event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <thread>
#include <string>
#include <memory>
#include "../../include/type_def.h"

namespace ezio {
    namespace event {
    class event_service;
    }
    namespace thread {
        class event_thread final : public std::enable_shared_from_this<event_thread>
        {
            constexpr event_thread(event_thread&) = delete;
            event_thread& operator=(event_thread&) = delete;
            public:
            using pointer_t = std::shared_ptr<event_thread>;
            event_thread(const std::string& evt_loop_name, const ezio::event::poll_param& param);
            ~event_thread();
            inline const std::shared_ptr<ezio::event::event_service>& get_evt_service() {
                return evt_service_ptr_;
            }
            void start();
            void join();
            private:
            void thread_func();
            private:
            std::shared_ptr<ezio::event::event_service> evt_service_ptr_{ nullptr };
            ezio::event::poll_param param_{};
            std::string evt_loop_name_{ "" };
            std::shared_ptr<std::thread> thread_ptr_{ nullptr };
        };
    }
}
