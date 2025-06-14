/****************************************************************************************
 * @file event_thread.cpp
 * @brief one thread with event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/thread/event_thread.h"
#include <string>
#include <memory>
#include "../../include/event_service.h"
#include "../../include/util/macros_func.h"

namespace ezio {
    namespace thread {
        event_thread::event_thread(const std::string& evt_loop_name, const ezio::event::poll_param& param)
            : param_(param), evt_loop_name_(evt_loop_name)
        {
            evt_service_ptr_ = std::make_shared<ezio::event::event_service>();
            auto ret = evt_service_ptr_->open(param);
            STABLE_INFRA_ASSERT(ret == 0);
        }

        event_thread::~event_thread()
        {
            if (nullptr != evt_service_ptr_) {
                evt_service_ptr_->close();
            }
            if (nullptr != thread_ptr_) {
                thread_ptr_->join();
            }
        }

        void event_thread::join()
        {
            if (nullptr != thread_ptr_) {
                thread_ptr_->join();
                thread_ptr_ = nullptr;
            }
        }

        void event_thread::start()
        {
            thread_ptr_ = std::make_shared<std::thread>(std::bind(&event_thread::thread_func, this));
        }

        void event_thread::thread_func()
        {
            evt_service_ptr_->start_loop();
        }
    }
}
