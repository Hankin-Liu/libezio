/****************************************************************************************
 * @file notifier.h
 * @brief one thread notifies event to other thread
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include "./event_base.h"
#include "../util/macros_func.h"
#include "../common/const_variable.h"

namespace ezio {
    namespace event {
//#define LEAVE_PRIORITY_THRESHOLD 5

        using notify_callback_t = std::function<void(void)>;
        enum class RUN_MODE : uint8_t
        {
            NORMAL = 0,
            HIGH_PRIORITY_STATE_1 = 1, ///< trying to go into high priority mode
            HIGH_PRIORITY_STATE_2 = 2, ///< real in high priority mode
        };

        class notifier : public event_base
        {
            constexpr notifier(notifier&) = delete;
            notifier& operator=(notifier&) = delete;
            public:
                using pointer_t = std::shared_ptr<notifier>;
            public:
                notifier();
                virtual ~notifier();
                virtual EVENT_TYPE get_event_type() const override {
                    return evt_type_;
                }
                virtual void handle_notify(int32_t ret);
                virtual void handle_read_ready(fd_t fd) override;
                // thread safe
//                virtual bool try_raise_priority() override;
                virtual void on_started(bool is_success) override;
                virtual void on_stopped(bool is_success) override;
                /* @brief notify who cares about this event
                 * @param[in] weight if weight = 0, add 1 to weight_, otherwise, set this value to weight_
                 * thread safe
                 */
                void notify(uint32_t weight = 0);

                // same thread callable
                int32_t open(event_service* evt_service_ptr, const notify_callback_t& cb,
                             const notify_callback_t& cb_for_stop = nullptr);
                virtual void handle_loop_once() override;

                // same thread callable
                //inline void set_weight_thredhold(uint32_t threshold) {
                //    shared_data_ptr_->threshold_to_raise_priority_ = threshold;
                //}
                // thread safe
                int32_t close();
            protected:
                void submit_read_request(const std::function<void(int32_t)>& cb = nullptr);
            private:
                fd_t fd_{};
                EVENT_TYPE evt_type_{EVENT_TYPE::NORMAL};
                std::shared_ptr<::iovec> iov_ptr_{ nullptr };
                uint64_t callback_para_{ 0 };
                notify_callback_t notify_handler_{ nullptr };
                notify_callback_t stop_handler_{ nullptr };
                //std::mutex mtx_consumers_;
        };
    }
}
