/**
 * @file epoll.h
 * @brief encapsulation of epoll
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include "../platform_define.h"
#ifdef EPOLL_IS_SUPPORTED
#include <sys/epoll.h>
#include <memory>
#include <set>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <vector>
#include "poll_base.h"
#include "../data_struct/opt_map.h"
#include "../common/const_variable.h"
#include "event_action.h"
#include "../data_struct/ring_buffer.h"

/// event count receive from epoll once
#define EVENT_CNT 1024

/// max fd which can be optimized 
#define MAX_FD 100000

#if !defined(EPOLL_CLOEXEC)
/// Flags for epoll_create1
#define EPOLL_CLOEXEC O_CLOEXEC
#endif

/**
 * @brief stable_infra namespace
 */
namespace ezio {
    /**
     * @brief event namespace
     * All event driven codes are in this namespace
     */
    namespace event {
        class event_action;
        class timer;
        /**
         * @brief event information
         */
        class event_info
        {
            public:
                using pointer_t = std::shared_ptr<event_info>;
                /**
                 * @brief construction function
                 */
                event_info(fd_t fd) : fd_(fd)
                {
                    event_action_ptr_ = std::make_shared<event_action>(fd);
                }
            public:
                fd_t fd_{};                              ///< file discriptor
                uint16_t events_{ 0 };                   ///< set changed event
                bool is_in_epoll_{ false };              ///< if this fd is in epoll
                bool is_in_change_list_{ false };        ///< if this event is in change list
                std::shared_ptr<event_action> event_action_ptr_{ nullptr }; ///< event action pointer
        };

        struct ring_buffer_info
        {
            inline void register_fd(uint32_t fd)
            {
                fd_set_.insert(fd);
            }
            inline void unregister_fd(uint32_t fd)
            {
                fd_set_.erase(fd);
            }
            inline uint64_t get_block_cnt() const {
                return buf_->get_block_cnt();
            }
            ezio::data_struct::ring_buffer::unique_pointer_t buf_{ nullptr };
            std::unordered_set<uint32_t> fd_set_{};
        };

        /**
         * @brief encapsulation of epoll mechanism
         */
        class epoll : public poll_base
        {
            public:
                /**
                 * @brief construction function
                 */
                epoll();
                /**
                 * @brief destruction function
                 */
                virtual ~epoll();
            public:
                /**
                 * @brief init epoll
                 * @return result of Initialization
                 * @retval true successful
                 * @retval false failed
                 */
                virtual bool init(uint32_t entry_cnt = 0) override;
                
                virtual int32_t register_ring_buffer(uint32_t block_size, uint32_t block_count) override;
                
                virtual int32_t unregister_ring_buffer(uint16_t buf_group_id) override;

                /**
                 * @brief register io buffer
                 * @param[in] buffer io buffer address
                 * @param[in] size size of buffer
                 * @return result of register
                 * @retval > 0 index of this buffer
                 * @retval -1 failed
                 */

                /**
                 * @brief remove io buffer
                 * @param[in] buffer_index index which is returned from register_buffer interface
                 * @return result of unregister
                 * @retval 0 success
                 * @retval -1 failed
                 */

                /**
                 * @brief Dispatch event
                 * Dispatch events and invoke callback functions
                 * @return result of dispatching
                 * @retval 0 successful
                 * @retval -1 failed
                 */
                virtual int32_t dispatch(int32_t timeout) override;
                /**
                 * @brief Close interface
                 * Close this epoll
                 */
                virtual void close() override;

                virtual int32_t submit_async_read(const fd_t& fd,
                                                  uint32_t buffer_group_id,
                                                  const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                                  const read_options* opt = nullptr) override;
                virtual int32_t submit_async_read(const fd_t& fd,
                                                  ::iovec* buffer,
                                                  uint32_t buffer_iov_cnt,
                                                  const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                                  const read_options* opt = nullptr
                                                  ) override;
                virtual int32_t cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb = nullptr) override;

                virtual int32_t submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb = nullptr) override;
                virtual int32_t cancel_async_accept(const fd_t& listen_fd, const std::function<void(int32_t)>& cb) override;

                virtual int32_t submit_async_write(const fd_t& fd,
                                                   ::iovec* buffer,
                                                   uint32_t buffer_iov_cnt,
                                                   const std::function<void(int32_t)>& cb = nullptr,
                                                   const write_options* opt = nullptr) override;
                virtual int32_t cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb) override;

                virtual int32_t create_timer(uint64_t interval_s, uint64_t interval_ns, const std::function<void(uint64_t)>& cb) override;

                virtual int32_t close_timer(int32_t timer_id) override;

                virtual int32_t submit() override;
            private:
                inline void push_to_change_list(event_info* evt_info_ptr) {
                    STABLE_INFRA_IF_TRUE_RETURN(evt_info_ptr->is_in_change_list_);
                    evt_change_lst_.push_back(evt_info_ptr);
                    evt_info_ptr->is_in_change_list_ = true;
                }

                inline bool is_inited() {
                    return (epfd_ != INVALID_FD);
                }
                /**
                 * @brief make changed event effective
                 */
                void apply_changes();
                /**
                 * @brief make changed event effective for one fd
                 * @param evt_info_ptr event_info object for one fd
                 */
                void apply_one_change(event_info* evt_info_ptr);
                void do_ready_tasks();
                void do_read(const task& t);
                void remove_event_info(const fd_t& fd);
                inline bool is_buffer_effective(uint32_t buffer_index) {
                    return (buffer_index < buffers_.size() && buffers_[buffer_index] != nullptr);
                }

                // parameter can't use const &, if use, it leads to remove this set when iterator it.
                void remove_multishot_task(const std::unordered_set<uint32_t> fd_set);
            private:
                std::unique_ptr<epoll_event[]> events_ptr_; ///< used for receive active events
                fd_t epfd_{}; ///< epoll fd
                /**< event information for each fd */
                ezio::data_struct::opt_map<event_info::pointer_t, uint32_t, MAX_FD> fd_to_event_info_;
                std::vector<event_info::pointer_t> removed_event_info_{};
                std::list<event_info*> evt_change_lst_; ///< event_info which has been changed
                int32_t errno_{ 0 };
                std::deque<std::shared_ptr<event_action>> ready_events_{};
                std::vector<std::shared_ptr<ring_buffer_info>> buffers_{};
                uint32_t timer_id_{ 0 };
                std::unordered_map<uint32_t, std::shared_ptr<timer>> id_to_timer_{};
                std::set<std::shared_ptr<event_action>> non_submit_events_{};
        };
    }
}
#endif
