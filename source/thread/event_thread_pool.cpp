/****************************************************************************************
 * @file event_thread_pool.h
 * @brief one thread pool with event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/thread/event_thread_pool.h"
#include <memory>
#include <string>
#include "../../include/common/const_variable.h"
#include "../../include/util/macros_func.h"

namespace ezio {
    namespace thread {
        event_thread_pool::event_thread_pool()
        {
        }

        event_thread_pool::~event_thread_pool()
        {
        }

        std::shared_ptr<ezio::event::event_service> event_thread_pool::get_evt_service(const std::string& name)
        {
            auto iter = thread_info_.find(name);
            STABLE_INFRA_CHECK_SUC(iter != thread_info_.end(), nullptr);
            const auto& evt_thread_ptr = iter->second;
            return evt_thread_ptr->get_evt_service();
        }

        void event_thread_pool::start()
        {
            for (const auto& kv : thread_info_) {
                const auto& evt_thread_ptr = kv.second;
                evt_thread_ptr->start();
            }
        }

        void event_thread_pool::join()
        {
            for (const auto& kv : thread_info_) {
                const auto& evt_thread_ptr = kv.second;
                evt_thread_ptr->join();
            }
        }

        bool event_thread_pool::add_thread(const std::string& thread_name, const ezio::event::poll_param& param)
        {
            auto iter = thread_info_.find(thread_name);
            STABLE_INFRA_CHECK_SUC(iter == thread_info_.end(), false);
            auto evt_thread_ptr = STABLE_INFRA_MAKE_UNIQUE(event_thread, thread_name, param);
            thread_info_[thread_name] = std::move(evt_thread_ptr);
            return true;
        }
    }
}
