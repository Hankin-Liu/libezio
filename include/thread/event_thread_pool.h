/****************************************************************************************
 * @file event_thread_pool.h
 * @brief one thread pool with event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>
#include "../type_def.h"
#include "./event_thread.h"

namespace ezio {
    namespace event {
    class event_service;
    }
    namespace thread {
        class event_thread_pool final
        {
            constexpr event_thread_pool(event_thread_pool&) = delete;
            event_thread_pool& operator=(event_thread_pool&) = delete;
            public:
            using pointer_t = std::shared_ptr<event_thread_pool>;
            event_thread_pool();
            ~event_thread_pool();
            std::shared_ptr<ezio::event::event_service> get_evt_service(const std::string& name);
            bool add_thread(const std::string& thread_name, const ezio::event::poll_param& param);
            void start();
            void join();
            private:
            std::map<std::string, std::unique_ptr<event_thread>> thread_info_{};
        };
    }
}
