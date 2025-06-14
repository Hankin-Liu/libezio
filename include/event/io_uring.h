/****************************************************************************************
 * @file io_uring.h
 * @brief encapsulation of iouring
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include "../platform_define.h"
#ifdef IOURING_IS_SUPPORTED
#include <memory>
#include <vector>
#include <unordered_map>
#include "./poll_base.h"
#include "../util/macros_func.h"
#include "../data_struct/opt_map.h"
#include "io_uring_wrapper.h"

/// max fd which can be optimized 
#define MAX_FD 100000

/**
 * @brief stable_infra namespace
 */
namespace ezio {
    /**
     * @brief event namespace
     * All event driven codes are in this namespace
     */
    namespace event {
        class io_uring_wrapper;
        class complete_action;
        /**
         * @brief event information
         */
        class iouring_event_info
        {
            public:
                using pointer_t = std::shared_ptr<iouring_event_info>;
                /**
                 * @brief construction function
                 */
                iouring_event_info() = default;
                const std::shared_ptr<complete_action>& get_read_complete_action(const fd_t& fd);
                const std::shared_ptr<complete_action>& get_write_complete_action();
                const std::shared_ptr<complete_action>& get_cancel_read_complete_action();
                const std::shared_ptr<complete_action>& get_cancel_write_complete_action();
            public:
                std::shared_ptr<complete_action> read_complete_action_ptr_{ nullptr };
                std::shared_ptr<complete_action> write_complete_action_ptr_{ nullptr };
                std::shared_ptr<complete_action> cancel_read_complete_action_ptr_{ nullptr };
                std::shared_ptr<complete_action> cancel_write_complete_action_ptr_{ nullptr };
        };

        struct last_buffer_info
        {
            public:
                inline uint32_t get_offset(uint32_t buf_index) {
                    return (buf_index_ == buf_index) ? offset_ : 0;
                }
                void update_offset(uint32_t buf_index, int32_t res) {
                    if (buf_index_ == buf_index) {
                        if (res > 0) {
                            offset_ += res;
                        }
                        return;
                    }
                    buf_index_ = buf_index;
                    offset_ = (res > 0) ? res : 0;
                }
            public:
                uint32_t offset_{ 0 };
                uint32_t buf_index_{ UINT32_MAX };
        };

        struct buffer_info
        {
            public:
                buffer_info(::iovec iov, struct io_uring_buf_ring* ptr, const std::shared_ptr<last_buffer_info>& last_buf, uint32_t mask)
                    : iov_(iov), buf_rings_(ptr), last_buf_info_(last_buf),
                    mask_(mask) {
                } 
            public:
                ::iovec iov_{};
                struct io_uring_buf_ring* buf_rings_{ nullptr };
                std::shared_ptr<last_buffer_info> last_buf_info_{ nullptr };
                uint32_t mask_{ 0 };
        };

        struct buffer_ring_info
        {
            public:
                buffer_ring_info(struct io_uring_buf_ring* ptr,
                        uint32_t entry_cnt, uint32_t start_index)
                    : buf_ring_ptr_(ptr), entry_cnt_(entry_cnt),
                    start_index_(start_index)
                {
                }
                struct io_uring_buf_ring* buf_ring_ptr_{ nullptr };
                uint32_t entry_cnt_{ 0 };
                uint32_t start_index_{ UINT32_MAX };
        };

        /**
         * @brief encapsulation of io_uring mechanism
         */
        class io_uring : public poll_base
        {
            public:
                /**
                 * @brief construction function
                 */
                io_uring();
                /**
                 * @brief destruction function
                 */
                virtual ~io_uring();
            public:
                /**
                 * @brief init epoll
                 * @return result of Initialization
                 * @retval true successful
                 * @retval false failed
                 */
                virtual bool init(uint32_t entry_cnt) override;

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
                 * @param[in] buffer_group_id buffer id which is returned from register_buffer interface
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
                                                  const read_options* opt = nullptr) override;
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
                inline bool is_inited() {
                    return (ring_ptr_ != nullptr);
                }
                inline bool is_buffer_valid(uint32_t buffer_index) {
                    return (buffer_index < buffers_.size() && buffers_[buffer_index].iov_.iov_base != nullptr);
                }
                int32_t submit_timeout(uint64_t interval_s, uint64_t interval_ns, complete_action* complete_action_ptr);
                inline io_uring_sqe* get_io_uring_sqe() {
                    auto sqe = ring_ptr_->get_sqe();
                    if (STABLE_INFRA_LIKELY(sqe != nullptr)) {
                        return sqe;
                    }
                    auto ret = ring_ptr_->submit();
                    STABLE_INFRA_CHECK_SUC(ret == 0, nullptr);
                    sqe = ring_ptr_->get_sqe();
                    STABLE_INFRA_CHECK_SUC(sqe != nullptr, nullptr);
                    return sqe;
                }
            private:
                static socklen_t SOCK_LEN;
            private:
                std::unique_ptr<io_uring_wrapper> ring_ptr_{ nullptr };
                /**< event information for each fd */
                ezio::data_struct::opt_map<iouring_event_info::pointer_t, uint32_t, MAX_FD> fd_to_event_info_;
                int32_t last_timeout_value_{ -1 };
                struct __kernel_timespec tm_out_{ 1000000000, 0};
                std::vector<buffer_info> buffers_{};
                std::unordered_map<uint32_t, buffer_ring_info> grp_id_to_buf_rings_{};
                uint32_t buffer_group_id_{ 0 };
                uint32_t timer_id_{ 0 };
                std::unordered_map<uint32_t, std::shared_ptr<complete_action>> id_to_timer_{};
        };
    }
}
#endif
