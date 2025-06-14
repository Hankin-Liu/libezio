/****************************************************************************************
 * @file event_loop.h
 * @brief event driver class, loop to process events
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <tuple>
#include <vector>
#include <deque>
#include <thread>
#include "event_base.h"
#include "poll_base.h"
#include "../util/macros_func.h"

namespace ezio {
    namespace event {
        class event_loop;
        class poll_param;
        class internal_notifier
        {
            using notify_callback_t = std::function<void(void)>;
            friend class event_loop;
        public:
            internal_notifier() = default;
            ~internal_notifier() = default;
        private:
            int32_t open(event_loop* evt_loop, const notify_callback_t& cb);
            void notify();
            void submit_read_request(const std::function<void(int32_t)>& cb = nullptr);
        private:
            fd_t fd_{};
            event_loop* evt_loop_ptr_{ nullptr };
            std::shared_ptr<::iovec> iov_ptr_{ nullptr };
            uint64_t callback_para_{ 0 };
            notify_callback_t notify_handler_{ nullptr };
        };

        class timer;
        class event_loop : public std::enable_shared_from_this<event_loop>
        {
            friend class event_base;
            friend class timer;

            enum class EL_INSTRUCTION : uint8_t
            {
                EI_ADD = 0,
                EI_REMOVE,
            };

            enum class EL_STATE : uint8_t
            {
                ES_DEF = 0,
                ES_INIT,
                ES_START,
                ES_STOP,
            };

            struct event_info
            {
                event_base::pointer_t evt_ptr_{ nullptr };
                EL_INSTRUCTION instruction_{ EL_INSTRUCTION::EI_ADD };
                std::vector<std::tuple<fd_t, uint16_t>> fds_evts_;
            };

            constexpr event_loop(event_loop&) = delete;
            event_loop& operator=(event_loop&) = delete;
            public:
                using pointer_t =  std::shared_ptr<event_loop>;
                typedef std::vector<event_info> evt_handlers_cache_t;
                typedef std::unordered_set<event_base::pointer_t> evt_handlers_t;
            public:
                event_loop();
                ~event_loop();
            public:
                /** open event loop
                 * @param param, parameters for event_loop. 
                 * @return 0 - success, otherwise failed
                 */
                int32_t open(const poll_param& param);

                /** close event loop
                */
                int32_t close();

                /** get the thread id of the event loop
                */
                inline std::thread::id get_thread_id() const { return thread_id_; }

                /** start loop
                */
                void start_loop();

                int32_t run_in_event_loop(const pending_func& job);
                void add_event(const event_base::pointer_t& eh);
                void remove_event(const event_base::pointer_t &eh);
                inline const std::shared_ptr<poll_base>& get_poll() {
                    return poll_;
                }
            private:
                /** driver function for all event loop
                 * @param timeout_ms, time out
                 * @return 0 - success, otherwise failed
                 */
                void loop_once(int32_t timeout_ms = -1);

                void wake_up();
                void handle_notify();
                void handle_close();
                int32_t on_timer();
                void process_other_thread_job();
                void process_new_instruction();
                void process_add_instruction(const event_info& evt_info);
                void process_remove_instruction(const event_info& evt_info);
            private:
                int32_t polling_ms_{ -1 };
                std::shared_ptr<poll_base> poll_{ nullptr };
                evt_handlers_t evt_handlers_{};         ///< store all normal handler
                evt_handlers_t busy_poll_evts_{};    ///< store all high priority handler
                evt_handlers_t removed_evt_handlers_cache_{}; ///< cache for handlers which are removed
                evt_handlers_cache_t evt_handlers_cache_{}; ///< cache, store instructions from application layer.
                std::mutex event_handler_mtx_{}; ///< lock for evt_handlers_
                std::thread::id thread_id_{ 0 }; ///< thread id
                volatile EL_STATE ee_state_{EL_STATE::ES_DEF}; ///< event_loop's current state
                std::mutex pending_jobs_mtx_{}; ///< lock for pending_funcs_
                std::vector<pending_func> other_thread_pending_funcs_{}; ///< other thread's functions need to be ran in this event_loop
                std::deque<pending_func> same_thread_pending_funcs_{}; ///< same thread's functions need to be ran in this event_loop
                std::vector<pending_func> other_thread_job_swap_cache_vec_{};
                //std::shared_ptr<notifier> ntf_evt_ptr_{ nullptr }; ///< other thread notify this thread
                std::shared_ptr<internal_notifier> ntf_evt_ptr_{ nullptr }; ///< other thread notify this thread
                int32_t internal_timer_id_{ -1 };
        };
    }
}
