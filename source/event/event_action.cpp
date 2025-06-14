/****************************************************************************************
 * @file event_action.cpp
 * @brief store callback for each event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <sys/epoll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../include/event/event_action.h"
#include "../../include/util/macros_func.h"
#include "../../include/event/fd_io_operation.h"
#include "../../include/event/poll_base.h"
#include "../../include/event/epoll.h"

namespace ezio {
    namespace event {
 //       const int32_t event_action::none_event_ = 0;
        const int32_t event_action::read_event_ = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
        const int32_t event_action::write_event_ = EPOLLOUT;
  //      const int32_t event_action::close_event_ = EPOLLHUP;
  //      const int32_t event_action::error_event_ = EPOLLERR;

        event_action::event_action(ezio::event::fd_t fd) : fd_(fd)
        {
            set_fd_ops(fd.get_fd_type());
        }

        event_action::~event_action()
        {
            clear_multishot_task();
            if (mmsghdr_for_udp_sock_ != nullptr) {
                delete mmsghdr_for_udp_sock_;
            }

        }
        
        void event_action::add_multishot_read_task(const multishot_task& t)
        {
            if (multishot_read_task_ == nullptr) {
                multishot_read_task_ = new multishot_task{};
            }
            *multishot_read_task_ = t;
            multishot_read_task_->buf_ring_->register_fd(fd_.get_fd());
            if (fd_.get_fd_type() == FD_TYPE::TCP_FD) {
                fd_ops_.read = &fd_io_operation<FD_TYPE_TCP>::read_fd_for_multishot;
            } else if (fd_.get_fd_type() == FD_TYPE::UDP_FD) {
                fd_ops_.read = &fd_io_operation<FD_TYPE_UDP>::read_fd_for_multishot;
                if (mmsghdr_for_udp_multishot_ == nullptr) {
                    mmsghdr_for_udp_multishot_ = new mmsghdr_buf_info{};
                    auto block_cnt = multishot_read_task_->buf_ring_->get_block_cnt();
                    mmsghdr_for_udp_multishot_->set_param(block_cnt, t.need_sock_detail_);
                    auto buf_info = multishot_read_task_->buf_ring_->buf_->writer_get_buffer();
                    STABLE_INFRA_ASSERT(block_cnt == buf_info->iov_cnt_);
                    for (uint32_t i = 0; i < block_cnt; ++i) {
                        if (t.need_sock_detail_) {
                            mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_name = &mmsghdr_for_udp_multishot_->sock_addr_[i];
                            mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
                        } else {
                            mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_name = nullptr;
                            mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_namelen = 0;
                        }
                        mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_iov = &buf_info->iov_[i];
                        mmsghdr_for_udp_multishot_->buf_[i].msg_hdr.msg_iovlen = 1;
                    }
                }
            }
        }

        void event_action::clear_multishot_task()
        {
            if (multishot_read_task_ == nullptr) {
                return;
            }
            multishot_read_task_->buf_ring_->unregister_fd(fd_.get_fd());
            delete multishot_read_task_;
            multishot_read_task_ = nullptr;
            if (mmsghdr_for_udp_multishot_ != nullptr) {
                delete mmsghdr_for_udp_multishot_;
            }
            set_fd_ops(fd_.get_fd_type());   // update multishot ops to normal ops
        }

        void event_action::submit()
        {
            if (! non_submit_read_task_.empty()) {
                auto size = non_submit_read_task_.size();
                for (uint32_t i = 0; i < size; ++i) {
                    const auto& t = non_submit_read_task_.front();
                    pending_read_task_.push_back(t);
                    non_submit_read_task_.pop_front();
                }
                STABLE_INFRA_ASSERT(non_submit_read_task_.empty());
            }
            if (! non_submit_write_task_.empty()) {
                auto size = non_submit_write_task_.size();
                for (uint32_t i = 0; i < size; ++i) {
                    const auto& t = non_submit_write_task_.front();
                    pending_write_task_.push_back(t);
                    non_submit_write_task_.pop_front();
                }
                STABLE_INFRA_ASSERT(non_submit_write_task_.empty());
            }
            if (multishot_read_task_ == nullptr) {
                multishot_read_task_->is_submit_ = true;
            }
        }

        void event_action::disable_read() 
        {
            events_ &= ~read_event_;
            read_buf_idx_callback_ = nullptr;
            pending_read_task_.clear();
            non_submit_read_task_.clear();
            clear_multishot_task();
            has_accept_task_ = false;
        }

        void event_action::disable_write()
        {
            events_ &= ~write_event_; 
            write_callback_ = nullptr;
            pending_write_task_.clear();
            non_submit_write_task_.clear();
        }

        void event_action::set_ready_events(uint32_t events)
        {
            if (! is_readable_ && events & read_event_) {
                is_readable_ = true;
            }
            if (! is_writable_ && events & write_event_) {
                is_writable_ = true;
            }
            constexpr bool is_event_triggered = true;
            handle_events(is_event_triggered);
        }

        int32_t event_action::read_udp(mmsghdr_buf_info* buf_info, uint32_t real_iov_cnt,
                                       bool need_sock_detail)
        {
            bool is_empty = false;
            auto ret = fd_ops_.read(fd_, nullptr, real_iov_cnt, is_empty, buf_info);
            if (STABLE_INFRA_LIKELY(ret > 0)) {
                //if (ret == INT32_MAX && is_empty) {
                if (is_empty) {
                    is_readable_ = false;
                }
                STABLE_INFRA_IF_TRUE_RETURN_CODE(ret == INT32_MAX, ret);
                socket_more_info info{};
                for (int32_t i = 0; i < ret; ++i) {
                    auto& iov_ptr = buf_info->buf_[i].msg_hdr.msg_iov;
                    auto data_len = buf_info->buf_[i].msg_len;
                    if (need_sock_detail) {
                        info.set_sockaddr_storage((struct sockaddr_storage*)buf_info->buf_[i].msg_hdr.msg_name);
                        read_buf_idx_callback_(data_len, iov_ptr, 1, &info);
                        continue;
                    }
                    read_buf_idx_callback_(data_len, iov_ptr, 1, nullptr);
                }
                return 0;
            }
                    
            read_buf_idx_callback_(ret, nullptr, 0, nullptr);
            return -1;
        }

        int32_t event_action::read_tcp(::iovec* iov, uint32_t iov_cnt, bool need_sock_detail)
        {
            struct msghdr msg{};
            struct sockaddr_storage sockaddr{};
            socket_more_info info{};
            socket_more_info* info_ptr{ nullptr };
            if (need_sock_detail) {
                msg.msg_name = &sockaddr;
                msg.msg_namelen = sizeof(sockaddr);
                info_ptr = &info;
                info.set_sockaddr_storage(&sockaddr);
            }
            bool is_empty = false;
            auto ret = fd_ops_.read(fd_, iov, iov_cnt, is_empty, &msg);
            if (STABLE_INFRA_LIKELY(ret > 0)) {
                if (ret == INT32_MAX && is_empty) {
                    is_readable_ = false;
                    return -1;
                }
                read_buf_idx_callback_(ret, iov, iov_cnt, info_ptr);
                return 0;
            }
                    
            read_buf_idx_callback_(ret, nullptr, 0, nullptr);
            return -1;
        }

        void event_action::do_multishot_read_task(bool is_event_triggered)
        {
            auto& buf_ring = multishot_read_task_->buf_ring_->buf_;
            int32_t ret = -1;
            const bool need_sock_detail = multishot_read_task_->need_sock_detail_;
            while (true) {
                auto buf_info = buf_ring->writer_get_buffer();
                STABLE_INFRA_ASSERT(buf_info != nullptr);
                if (fd_.get_fd_type() == ezio::event::FD_TYPE::UDP_FD) {
                    ret = read_udp(mmsghdr_for_udp_multishot_, 0, need_sock_detail);
                } else {
                    ret = read_tcp(buf_info->iov_, buf_info->iov_cnt_, need_sock_detail);
                }
                if (ret < 0) {
                    break;
                }
            }
        }

        void event_action::handle_events(bool is_event_triggered)
        {
            if (is_readable_) {
                if (multishot_read_task_ != nullptr) {
                    STABLE_INFRA_IF_TRUE_RETURN(! multishot_read_task_->is_submit_);
                    do_multishot_read_task(is_event_triggered);
                } else {
                    if (! pending_read_task_.empty()) {
                        auto size = pending_read_task_.size();
                        for (uint32_t i = 0; i < size; ++i) {
                            const auto& t = pending_read_task_.front();
                            auto ret = do_read_task(t, is_event_triggered);
                            if (ret == INT32_MAX) {
                                pending_read_task_.push_back(t);
                            }
                            pending_read_task_.pop_front();
                        }
                    }
                }
                if (has_accept_task_) {
                    do_accept_task();
                }
            }
            if (is_writable_ && ! pending_write_task_.empty()) {
                auto size = pending_write_task_.size();
                for (uint32_t i = 0; i < size; ++i) {
                    auto& t = pending_write_task_.front();
                    auto ret = do_write_task(t);
                    if (ret == INT32_MAX) {
                        is_writable_ = false;
                        pending_write_task_.push_back(t);
                    }
                    pending_write_task_.pop_front();
                }
            }
        }

        void event_action::set_fd_ops(const FD_TYPE type)
        {
            STABLE_INFRA_ASSERT(type != FD_TYPE::UNKNOWN_FD);
            fd_operations ops;
            switch(type) {
            case FD_TYPE::TCP_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_TCP>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_TCP>::write_fd;
                    break;
                }
            case FD_TYPE::UDP_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_UDP>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_UDP>::write_fd;
                    break;
                }
            case FD_TYPE::INOTIFY_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_INOTIFY>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_INOTIFY>::write_fd;
                    break;
                }
            case FD_TYPE::FILE_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_FILE>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_FILE>::write_fd;
                    // file fds are always readable and writable
                    is_writable_ = true;
                    is_readable_ = true;
                    break;
                }
            case FD_TYPE::TIMER_FD:
            case FD_TYPE::EVENT_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_EVENT_FD>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_EVENT_FD>::write_fd;
                    break;
                }
            case FD_TYPE::SIGNAL_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_SIGNAL_FD>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_SIGNAL_FD>::write_fd;
                    break;
                }
            case FD_TYPE::UDS_STREAM_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_UDS_STREAM>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_UDS_STREAM>::write_fd;
                    break;
                }
            case FD_TYPE::UDS_DGRAM_FD:
                {
                    ops.read = &fd_io_operation<FD_TYPE_UDS_DGRAM>::read_fd;
                    ops.write = &fd_io_operation<FD_TYPE_UDS_DGRAM>::write_fd;
                    break;
                }
            default:
                {
                    return;
                }
            }
            fd_ops_ = ops;
        }

        int32_t event_action::do_accept_task()
        {
            socklen_t len = sizeof(sock_info::sock_addr_);
            int32_t ret = 0;
            while (true) {
                ret = accept4(fd_, (sockaddr*)&addr_buffer_->sock_addr_, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (STABLE_INFRA_UNLIKELY(ret < 0)) {
                    if (STABLE_INFRA_LIKELY(errno == EAGAIN || errno == EWOULDBLOCK)) {
                        is_readable_ = false;
                        break;
                    }
                    STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                    read_buf_idx_callback_(ret, nullptr, 0, nullptr);
                    break;
                } else if (STABLE_INFRA_UNLIKELY(ret == 0)) {
                    continue;
                }
                fd_t tmp_fd{ret, FD_TYPE::TCP_FD};
                addr_buffer_->fd_ = tmp_fd;
                read_buf_idx_callback_(ret, nullptr, 0, nullptr);
                break;
            }
            return 0;
        }

        int32_t event_action::do_read_task(const task& t, bool is_event_triggered)
        {
            if (t.buffer_iov_cnt_ == 0) {
                // just detect is readable or not, do not need to read fd
                read_buf_idx_callback_(RET_SUC, t.buffer_, t.buffer_iov_cnt_, nullptr);
                return 0;
            }
            if (STABLE_INFRA_UNLIKELY(t.buffer_iov_cnt_ > iov_buffer_.size())) {
                iov_buffer_.resize(t.buffer_iov_cnt_);
            }
            std::memcpy((void*)iov_buffer_.data(), (const void*)t.buffer_, sizeof(::iovec) * t.buffer_iov_cnt_);
            if (fd_.get_fd_type() == FD_TYPE::FILE_FD && 
                STABLE_INFRA_UNLIKELY(read_offset_ != UINT64_MAX)) {
                auto ret = lseek(fd_, read_offset_, SEEK_SET);
                if (ret < 0) {
                    read_buf_idx_callback_(ret, t.buffer_, t.buffer_iov_cnt_, nullptr);
                    return 0;
                }
                read_offset_ = UINT64_MAX; // next time, continue to read at current offset
            }
            int32_t ret = -1;
            if (fd_.get_fd_type() == ezio::event::FD_TYPE::UDP_FD) {
                if (STABLE_INFRA_UNLIKELY(mmsghdr_for_udp_sock_ == nullptr)) {
                    mmsghdr_for_udp_sock_ = new mmsghdr_buf_info{};
                }
                mmsghdr_for_udp_sock_->set_param(t.buffer_iov_cnt_, t.need_sock_detail_);
                for (uint32_t i = 0; i < t.buffer_iov_cnt_; ++i) {
                    if (t.need_sock_detail_) {
                        mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_name = &mmsghdr_for_udp_sock_->sock_addr_[i];
                        mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
                    } else {
                        mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_name = nullptr;
                        mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_namelen = 0;
                    }
                    mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_iov = &t.buffer_[i];
                    mmsghdr_for_udp_sock_->buf_[i].msg_hdr.msg_iovlen = 1;
                }
                ret = read_udp(mmsghdr_for_udp_sock_, t.buffer_iov_cnt_, t.need_sock_detail_);
                STABLE_INFRA_IF_TRUE_RETURN_CODE(ret == INT32_MAX, ret);
            } else {
                void* custom_data = nullptr;
                bool is_empty = false;
                ret = fd_ops_.read(fd_, iov_buffer_.data(), t.buffer_iov_cnt_, is_empty, custom_data);
                if (is_empty) {
                    is_readable_ = false;
                    STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_event_triggered && ret == 0, INT32_MAX);
                }
                read_buf_idx_callback_(ret, t.buffer_, t.buffer_iov_cnt_, custom_data);
            }
            return 0;
        }

        int32_t event_action::do_write_task(const task& t)
        {
            if (t.buffer_iov_cnt_ == 0) {
                // just detect is writable or not, do not need to write fd
                write_callback_(RET_SUC);
                return 0;
            }
            if (STABLE_INFRA_UNLIKELY(t.buffer_iov_cnt_ > iov_buffer_.size())) {
                iov_buffer_.resize(t.buffer_iov_cnt_);
            }
            memcpy((void*)iov_buffer_.data(), t.buffer_, sizeof(::iovec) * t.buffer_iov_cnt_);
            if (STABLE_INFRA_UNLIKELY(write_offset_ != UINT64_MAX)) {
                auto ret = lseek(fd_, write_offset_, SEEK_SET);
                if (ret < 0) {
                    write_callback_(ret);
                    return 0;
                }
            }
            bool is_full = false;
            void* custom_data = nullptr;
            if (t.sockaddr_ != nullptr) {
                custom_data = t.sockaddr_;
            }
            auto ret = fd_ops_.write(fd_, iov_buffer_.data(), t.buffer_iov_cnt_, is_full, custom_data);
            write_offset_ = UINT64_MAX; // next time, continue to write at current offset
            STABLE_INFRA_IF_TRUE_RETURN_CODE(is_full && ret == 0, INT32_MAX);
            write_callback_(ret);
            return 0;
        }
    }
}
