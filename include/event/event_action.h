/****************************************************************************************
 * @file event_action.h
 * @brief store callback for each event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <vector>
#include <deque>
#include <cstring>
#include "event_common.h"
#include "../common/internal_type_def.h"

namespace ezio {
    namespace event {
        class ring_buffer_info;
        struct fd_operations
        {
            int32_t (*read)(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void* customized_data);
            int32_t (*write)(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void* customized_data);
        };
                
        struct mmsghdr_buf_info
        {
        public:
            void set_param(uint32_t block_cnt, bool need_sock_detail)
            {
                set_block_cnt(block_cnt);
                if (need_sock_detail) {
                    set_need_sock_detail();
                }
            }
            inline uint32_t get_block_cnt() const {
                return buf_.size();
            }
        private:
            void set_need_sock_detail()
            {
                auto block_cnt = buf_.size();
                if (sock_addr_.size() >= block_cnt) {
                    return;
                }
                sock_addr_.resize(block_cnt);
                memset(sock_addr_.data(), 0, sizeof(sockaddr_storage) * block_cnt);
            }

            void set_block_cnt(uint32_t block_cnt)
            {
                if (buf_.size() >= block_cnt) {
                    return;
                }
                buf_.resize(block_cnt);
                memset(buf_.data(), 0, sizeof(mmsghdr) * block_cnt);
            }
        public:
            std::vector<struct mmsghdr> buf_{};
            std::vector<struct sockaddr_storage> sock_addr_{};
        };

        class task
        {
            public:
                task() {}
                task(::iovec* buffer, uint32_t buffer_iov_cnt)
                    : buffer_(buffer), buffer_iov_cnt_(buffer_iov_cnt)
                {
                }
                task(::iovec* buffer, uint32_t buffer_iov_cnt,
                     struct sockaddr_storage* sockaddr)
                    : buffer_(buffer), buffer_iov_cnt_(buffer_iov_cnt),
                    sockaddr_(sockaddr)
                {
                }
                inline void set_need_sock_detail(bool need_sock_detail) {
                    need_sock_detail_ = need_sock_detail;
                }
                ::iovec* buffer_{ nullptr };
                uint32_t buffer_iov_cnt_{ 0 };
                struct sockaddr_storage* sockaddr_{ nullptr };
                bool need_sock_detail_{ false };
        };

        class multishot_task
        {
            public:
                multishot_task() {}
                multishot_task(ezio::event::ring_buffer_info* buf,
                               bool need_sock_detail, bool is_submit)
                    : buf_ring_(buf), need_sock_detail_(need_sock_detail),
                    is_submit_(is_submit)
                {
                }
                ring_buffer_info* buf_ring_{ nullptr };
                bool need_sock_detail_{ false };
                bool is_submit_{ true };
        };

        class sock_info;
        class event_action
        {
            public:
                event_action(ezio::event::fd_t fd);
                ~event_action();

                void handle_events(bool is_event_triggered);
                inline void set_read_callback(const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb) {
                    events_ |= read_event_; 
                    read_buf_idx_callback_ = cb;
                }
                inline void set_write_callback(const callback_t& cb) {
                    events_ |= write_event_;
                    write_callback_ = cb;
                }
                inline void set_addr_buffer(sock_info* addr_buffer) {
                    addr_buffer_ = addr_buffer;
                }
                inline sock_info*& get_addr_buffer() {
                    return addr_buffer_;
                }
                inline const callback_t& get_write_callback() {
                    return write_callback_;
                }
                void disable_read();
                void disable_write();
                void add_multishot_read_task(const multishot_task& t);
                void clear_multishot_task();
                inline void add_read_task(const task& t)
                {
                    pending_read_task_.push_back(t);
                }
                inline void add_non_submit_read_task(const task& t)
                {
                    non_submit_read_task_.push_back(t);
                }
                inline void add_accept_task()
                {
                    has_accept_task_ = true;
                }
                inline void add_write_task(const task& t)
                {
                    pending_write_task_.push_back(t);
                }
                inline void add_non_submit_write_task(const task& t)
                {
                    non_submit_write_task_.push_back(t);
                }
                inline bool is_readable() const {
                    return is_readable_;
                }
                inline bool is_writable() const {
                    return is_writable_;
                }
                void set_ready_events(uint32_t events);
                inline int32_t events() const { return events_; }
                inline bool is_writing() const { return events_ & write_event_; }
                inline bool is_reading() const { return events_ & read_event_; }
                inline bool is_none_evt() const {
                    return ! (is_writing() || is_reading());
                }
                void submit();
                inline uint64_t get_read_offset() const {
                    return read_offset_;
                }
                inline void set_read_offset(uint64_t read_offset) {
                    read_offset_ = read_offset;
                }
                inline uint64_t get_write_offset() const {
                    return write_offset_;
                }
                inline void set_write_offset(uint64_t write_offset) {
                    write_offset_ = write_offset;
                }
                //void disable_closing();
                //void disable_error();
                //void disable_all();
            private:
                void set_fd_ops(const FD_TYPE type);
                //void do_cancel_read();
                //void do_cancel_write();
                //void do_cancel_accept();
                int32_t do_read_task(const task& t, bool is_event_triggered);
                void do_multishot_read_task(bool is_event_triggered);
                int32_t do_write_task(const task& t);
                int32_t do_accept_task();
                int32_t read_tcp(::iovec* iov, uint32_t iov_cnt, bool need_sock_detail);
                int32_t read_udp(mmsghdr_buf_info* buf_info, uint32_t real_iov_cnt,
                                 bool need_sock_detail);
            private:
                //static const int32_t none_event_;
                static const int32_t read_event_;
                static const int32_t write_event_;
                //static const int32_t error_event_;
                //static const int32_t close_event_;
                fd_t fd_{};
                int32_t events_{ 0 };
                std::function<void(int32_t, const ::iovec*, uint32_t, void*)> read_buf_idx_callback_{nullptr};
                callback_t write_callback_{nullptr};
                //callback_t cancel_read_callback_{nullptr};
                //callback_t cancel_write_callback_{nullptr};
                bool has_accept_task_{ false };
                sock_info* addr_buffer_{ nullptr };
                std::deque<task> pending_read_task_{};
                std::deque<task> non_submit_read_task_{};
                std::deque<task> pending_write_task_{};
                std::deque<task> non_submit_write_task_{};
                multishot_task* multishot_read_task_{ nullptr };
                mmsghdr_buf_info* mmsghdr_for_udp_multishot_{ nullptr };
                //std::vector<struct mmsghdr> udp_sock_{};
                mmsghdr_buf_info* mmsghdr_for_udp_sock_{};
                bool is_readable_{ false };
                bool is_writable_{ false };
                fd_operations fd_ops_{};
                std::vector<::iovec> iov_buffer_;
                uint64_t read_offset_{ UINT64_MAX };
                uint64_t write_offset_{ UINT64_MAX };
        };
    }
}
