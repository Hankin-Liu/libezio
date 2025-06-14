/**
 * @file io_uring.cpp
 * @brief encapsulation of io_uring
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#include "../../include/event/io_uring.h"
#ifdef IOURING_IS_SUPPORTED
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <vector>
#include <memory>
#include "../../include/event/complete_action.h"
#include "../../include/util/macros_func.h"
#include "../../include/util/util.h"

namespace ezio {
    namespace event {
        socklen_t io_uring::SOCK_LEN = sizeof(sock_info::sock_addr_);

        const std::shared_ptr<complete_action>& iouring_event_info::get_read_complete_action(const fd_t& fd)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(read_complete_action_ptr_ != nullptr, read_complete_action_ptr_);
            read_complete_action_ptr_ = std::make_shared<complete_action>();
            if (fd.get_fd_type() == ezio::event::FD_TYPE::SIGNAL_FD
                || fd.get_fd_type() == ezio::event::FD_TYPE::TIMER_FD
                || fd.get_fd_type() == ezio::event::FD_TYPE::EVENT_FD) {
                read_complete_action_ptr_->disable_offset();
            }
            return read_complete_action_ptr_;
        }

        const std::shared_ptr<complete_action>& iouring_event_info::get_write_complete_action()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(write_complete_action_ptr_ != nullptr, write_complete_action_ptr_);
            write_complete_action_ptr_ = std::make_shared<complete_action>();
            return write_complete_action_ptr_;
        }

        const std::shared_ptr<complete_action>& iouring_event_info::get_cancel_read_complete_action()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(cancel_read_complete_action_ptr_ != nullptr, cancel_read_complete_action_ptr_);
            cancel_read_complete_action_ptr_ = std::make_shared<complete_action>();
            return cancel_read_complete_action_ptr_;
        }

        const std::shared_ptr<complete_action>& iouring_event_info::get_cancel_write_complete_action()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(cancel_write_complete_action_ptr_ != nullptr, cancel_write_complete_action_ptr_);
            cancel_write_complete_action_ptr_ = std::make_shared<complete_action>();
            return cancel_write_complete_action_ptr_;
        }

        io_uring::io_uring() {
        }

        io_uring::~io_uring() {
            close();
            for (const auto& buf : buffers_) {
                if (buf.iov_.iov_base != nullptr) {
                    free(buf.iov_.iov_base);
                }
            }
            for (const auto& kv : grp_id_to_buf_rings_) {
                auto ptr = kv.second.buf_ring_ptr_;
                if (ptr != nullptr) {
                    free(ptr);
                }
            }
        }

        bool io_uring::init(uint32_t entry_cnt)
        {
            ring_ptr_ = STABLE_INFRA_MAKE_UNIQUE(ezio::event::io_uring_wrapper);
            struct io_uring_params params{};
            memset(&params, 0, sizeof(params));
            params.flags = IORING_SETUP_CQE32 | IORING_SETUP_NO_SQARRAY;
            auto ret = ring_ptr_->init(entry_cnt, params);
            STABLE_INFRA_CHECK_SUC(ret == 0, false);
            return true;
        }

        int32_t io_uring::submit_timeout(uint64_t interval_s, uint64_t interval_ns, complete_action* complete_action_ptr)
        {
            struct __kernel_timespec timeout = {
                .tv_sec = (int64_t)interval_s,
                .tv_nsec = (int64_t)interval_ns
            };
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            uint32_t flags = 0;
#ifdef IORING_TIMEOUT_MULTISHOT
            flags = IORING_TIMEOUT_MULTISHOT;
#endif
            ring_ptr_->prepare_timeout(sqe, &timeout, 1, flags);
            ring_ptr_->set_user_data(sqe, reinterpret_cast<uint64_t>(complete_action_ptr));
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::create_timer(uint64_t interval_s, uint64_t interval_ns, const std::function<void(uint64_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC((interval_s != 0 || interval_ns != 0) && cb != nullptr, -1);
            auto complete_action_ptr = std::make_shared<complete_action>();
            auto tmp_ptr = complete_action_ptr.get();
            auto tmp_cb = [cb, interval_s, interval_ns,
#ifndef IORING_TIMEOUT_MULTISHOT
                 this,
#endif
                 tmp_ptr](int32_t res, const ::iovec*, uint32_t, void*) {
#ifndef IORING_TIMEOUT_MULTISHOT
                 auto ret = this->submit_timeout(interval_s, interval_ns, tmp_ptr);
                 STABLE_INFRA_ASSERT(ret == 0);
#endif
                 if (STABLE_INFRA_LIKELY(res == -ETIME)) {
                     res = 0;
                 }
                 cb(res);
            };
            complete_action_ptr->set_callback(tmp_cb);
            auto ret = submit_timeout(interval_s, interval_ns, tmp_ptr);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            int32_t timer_id = (int32_t)timer_id_;
            id_to_timer_[timer_id_] = std::move(complete_action_ptr);
            ++timer_id_;
            return timer_id;
        }

        int32_t io_uring::close_timer(int32_t timer_id)
        {
            auto iter = id_to_timer_.find(timer_id);
            STABLE_INFRA_CHECK_SUC(iter != id_to_timer_.end(), -1);
            iter->second->cleanup();
#ifdef IORING_TIMEOUT_MULTISHOT
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            uint64_t user_data = 0;
            ring_ptr_->prepare_cancel(sqe, (void*)user_data, 0);
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
#endif
            id_to_timer_.erase(iter);
            return 0;
        }

        int32_t io_uring::register_ring_buffer(uint32_t block_size, uint32_t block_count)
        {
            STABLE_INFRA_CHECK_SUC(block_size > 0 && block_count > 0, -1);
            auto is_power_of_two = ezio::util::is_power_of_2(block_count);
            STABLE_INFRA_CHECK_SUC(is_power_of_two, -1);
            const uint32_t mask = ring_ptr_->buf_ring_mask(block_count);
            constexpr uint32_t aligned_value = 4096;

            uint16_t buf_grp_id = buffer_group_id_;
            ++buffer_group_id_;

            int32_t ret = 0;
            uint32_t flags = 0;
            //flags |= IOU_PBUF_RING_INC;
            auto br = ring_ptr_->setup_buf_ring(block_count, buf_grp_id, flags, ret);
            STABLE_INFRA_CHECK_SUC(br != nullptr, -1);
            std::vector<void*> addrs;
            addrs.reserve(block_count);
            bool is_alloc_failed = false;
            for (uint32_t i = 0; i < block_count; ++i) {
                auto addr = ezio::util::alloc_aligned_mem(block_size, aligned_value);
                if (addr == nullptr) {
                    is_alloc_failed = true;
                    break;
                }
                addrs.push_back(addr);
            }
            if (is_alloc_failed) {
                free(br);
                for (const auto& addr : addrs) {
                    free(addr);
                }
                return -1;
            }
            auto last_buf = std::make_shared<last_buffer_info>();
            int32_t start_index = -1;
            for (uint32_t i = 0; i < addrs.size(); ++i) {
                uint32_t bid = buffers_.size();
                if (start_index == -1) {
                    start_index = buffers_.size();
                }
                ::iovec iov = {
                    .iov_base = addrs[i],
                    .iov_len = block_size
                };
                buffer_info buf(iov, br, last_buf, mask);
                buffers_.push_back(buf);
                ring_ptr_->add_buf_ring(br, addrs[i], block_size,
                                        bid, mask, i);
            }
            ring_ptr_->buf_ring_advance(br, block_count, mask);
            buffer_ring_info info(br, block_count, start_index);
            grp_id_to_buf_rings_.emplace(std::make_pair(buf_grp_id, info));
            return buf_grp_id;
        }
        
        int32_t io_uring::unregister_ring_buffer(uint16_t buf_group_id)
        {
            auto iter = grp_id_to_buf_rings_.find(buf_group_id);
            STABLE_INFRA_CHECK_SUC(iter != grp_id_to_buf_rings_.end(), -1);
            const auto& buf_ring_info = iter->second;
            auto ret = ring_ptr_->free_buf_ring(buf_ring_info.buf_ring_ptr_,
                                                buf_ring_info.entry_cnt_, buf_group_id);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            bool has_freed = false;
            const auto& start_index = buf_ring_info.start_index_;
            for (auto idx = start_index; idx < buffers_.size(); ++idx) {
                auto& buf = buffers_[start_index];
                STABLE_INFRA_IF_TRUE_CONTINUE(buf_ring_info.buf_ring_ptr_ != buf.buf_rings_);
                STABLE_INFRA_CHECK_SUC(buf.iov_.iov_base != nullptr, -1);
                free(buf.iov_.iov_base);
                buf.iov_.iov_base = nullptr;
                has_freed = true;
            }
            STABLE_INFRA_CHECK_SUC(has_freed, -1);
            grp_id_to_buf_rings_.erase(iter);
            return 0;
        }

        int32_t io_uring::submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || listen_fd < 0 || addr_buffer == nullptr, -1);
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            complete_action* complete_action_ptr{ nullptr };
            const auto& evt_info_ptr = fd_to_event_info_.find(listen_fd);
            if (nullptr == evt_info_ptr) {
                auto new_info_ptr = std::make_shared<iouring_event_info>();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(listen_fd, new_info_ptr));
                complete_action_ptr = new_info_ptr->get_read_complete_action(listen_fd).get();
            } else {
                complete_action_ptr = evt_info_ptr->get_read_complete_action(listen_fd).get();
            }
            if (addr_buffer != nullptr) {
                complete_action_ptr->set_addr_buffer(addr_buffer);
            }
            ring_ptr_->prepare_accept_multishot(sqe, listen_fd, (struct sockaddr *)&addr_buffer->sock_addr_, &SOCK_LEN, 0);
            ring_ptr_->set_user_data(sqe, reinterpret_cast<uint64_t>(complete_action_ptr));
            if (cb != nullptr) {
                auto& real_buffer = complete_action_ptr->get_addr_buffer();
                auto tmp_cb = [cb, real_buffer](int32_t ret, const ::iovec*, uint32_t, void*) {
                    real_buffer->fd_ = (ret > 0) ? fd_t{ ret, FD_TYPE::TCP_FD } : fd_t{};
                    cb(ret, *real_buffer);
                };
                complete_action_ptr->set_callback(tmp_cb);
            }
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::cancel_async_accept(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0, -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(nullptr == evt_info_ptr, -1);
            auto complete_action_ptr = evt_info_ptr->get_cancel_read_complete_action().get();
            auto tmp_cb = [cb](int32_t ret, const ::iovec*, uint32_t, void*) {
                cb(ret);
            };
            complete_action_ptr->set_callback(tmp_cb);
            auto cancel_sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(cancel_sqe != nullptr, -1);
            ring_ptr_->prepare_cancel(cancel_sqe, complete_action_ptr, 0);
            auto ret = ring_ptr_->submit();
            //io_uring_prep_cancel(cancel_sqe, complete_action_ptr, 0);
            //auto ret = io_uring_submit(io_uring_ptr_.get());
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::submit_async_write(const fd_t& fd,
                                             ::iovec* buffer,
                                             uint32_t buffer_iov_cnt,
                                             const std::function<void(int32_t)>& cb,
                                             const write_options* opt)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0 || (buffer_iov_cnt > 0 && buffer == nullptr), -1);
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            complete_action* complete_action_ptr{ nullptr };
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_info_ptr) {
                auto new_info_ptr = std::make_shared<iouring_event_info>();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_info_ptr));
                complete_action_ptr = new_info_ptr->get_write_complete_action().get();
            } else {
                complete_action_ptr = evt_info_ptr->get_write_complete_action().get();
            }
            if (cb != nullptr) {
                auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                    cb(res);
                };
                complete_action_ptr->set_callback(tmp_cb);
            }
            uint64_t offset = complete_action_ptr->get_offset();
            bool is_submit = true;
            if (opt != nullptr) {
                if (STABLE_INFRA_UNLIKELY(opt->get_offset() != UINT64_MAX)) {
                    offset = opt->get_offset();
                    complete_action_ptr->set_offset(opt->get_offset());
                }
                is_submit = opt->get_submit();
            }
            if (buffer != nullptr) {
                if (fd.get_fd_type() == FD_TYPE::TCP_FD
                    || fd.get_fd_type() == FD_TYPE::UDP_FD) {
                    auto msg = complete_action_ptr->get_write_msghdr();
                    msg->msg_iov = buffer;
                    msg->msg_iovlen = buffer_iov_cnt;
                    ring_ptr_->prepare_sendmsg(sqe, fd.get_fd(), msg, MSG_DONTWAIT);
                    //io_uring_prep_sendmsg(sqe, fd.get_fd(), msg, MSG_DONTWAIT);
                } else {
                    //io_uring_prep_writev(sqe, fd.get_fd(), buffer, buffer_iov_cnt, offset);
                    ring_ptr_->prepare_writev(sqe, fd.get_fd(), buffer, buffer_iov_cnt, offset);
                }
            } else {
                ring_ptr_->prepare_poll(sqe, fd, POLLOUT);
                //io_uring_prep_poll_add(sqe, fd, POLLOUT);
            }
            ring_ptr_->set_user_data(sqe, reinterpret_cast<uint64_t>(complete_action_ptr));
            //sqe->user_data = reinterpret_cast<uint64_t>(complete_action_ptr);
            if (is_submit) {
                auto ret = ring_ptr_->submit();
                STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            }
            return 0;
        }

        int32_t io_uring::cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0, -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(nullptr == evt_info_ptr, -1);
            auto complete_action_ptr = evt_info_ptr->get_cancel_write_complete_action().get();
            auto tmp_cb = [cb](int32_t ret, const ::iovec*, uint32_t, void*) {
                cb(ret);
            };
            complete_action_ptr->set_callback(tmp_cb);
            auto cancel_sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(cancel_sqe != nullptr, -1);
            ring_ptr_->prepare_cancel(cancel_sqe, complete_action_ptr, 0);
            //io_uring_prep_cancel(cancel_sqe, complete_action_ptr, 0);
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::submit()
        {
            STABLE_INFRA_CHECK_SUC(is_inited(), -1);
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::submit_async_read(const fd_t& fd,
                                            uint32_t buffer_group_id,
                                            const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                                            const read_options* opt)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && fd.is_valid() && is_buffer_valid(buffer_group_id), -1);
            STABLE_INFRA_CHECK_SUC(fd.get_fd_type() == ezio::event::FD_TYPE::TCP_FD || fd.get_fd_type() == ezio::event::FD_TYPE::UDP_FD, -1);
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            complete_action* complete_action_ptr{ nullptr };
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_info_ptr) {
                auto new_info_ptr = std::make_shared<iouring_event_info>();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_info_ptr));
                complete_action_ptr = new_info_ptr->get_read_complete_action(fd).get();
            } else {
                complete_action_ptr = evt_info_ptr->get_read_complete_action(fd).get();
            }
            bool is_submit = true;
            bool need_socket_detail = false;
            if (opt != nullptr) {
                is_submit = opt->get_submit();
                need_socket_detail = opt->get_need_socket_detail();
            }
            struct msghdr* msghdr_ptr = nullptr;
            if (cb != nullptr) {
                msghdr_ptr = complete_action_ptr->get_read_msghdr();
                auto tmp_cb = [cb, msghdr_ptr, need_socket_detail](int32_t res, const ::iovec* iov, uint32_t iov_cnt, void*) {
                    if (! need_socket_detail || STABLE_INFRA_UNLIKELY(res <= 0)) {
                        cb(res, iov, iov_cnt, nullptr);
                        return;
                    }
                    auto recvmsg_out_ptr = (struct io_uring_recvmsg_out*)(iov->iov_base);
                    auto sockaddr = (struct sockaddr_storage*)(&recvmsg_out_ptr[1]);
                    socket_more_info m_info{};
                    m_info.set_sockaddr_storage(sockaddr);
                    ::iovec new_iov = {
                        .iov_base = (char*)iov->iov_base + sizeof(struct io_uring_recvmsg_out) + sizeof(struct sockaddr_storage),
                        .iov_len = recvmsg_out_ptr->payloadlen
                    };
                    cb(res, &new_iov, iov_cnt, &m_info);
                };
                complete_action_ptr->set_callback(tmp_cb);
            }
            auto tmp_fd = fd.get_fd();
            auto resubmit_func = [this, tmp_fd, buffer_group_id, msghdr_ptr, need_socket_detail]() {
                auto sqe = this->get_io_uring_sqe();
                if (need_socket_detail) {
                    this->ring_ptr_->prepare_recvmsg_multishot(sqe, tmp_fd, msghdr_ptr, 0, buffer_group_id);
                } else {
                    this->ring_ptr_->prepare_recv_multishot(sqe, tmp_fd, msghdr_ptr, 0, buffer_group_id);
                }
                this->ring_ptr_->submit();
            };
            complete_action_ptr->set_resubmit_multishot_func(std::move(resubmit_func));
            if (need_socket_detail) {
                ring_ptr_->prepare_recvmsg_multishot(sqe, fd.get_fd(), msghdr_ptr, 0, buffer_group_id);
            } else {
                ring_ptr_->prepare_recv_multishot(sqe, fd.get_fd(), msghdr_ptr, 0, buffer_group_id);
            }
            ring_ptr_->set_user_data(sqe, reinterpret_cast<uint64_t>(complete_action_ptr));
            if (is_submit) {
                auto ret = ring_ptr_->submit();
                STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            }
            return 0;
        }

        int32_t io_uring::submit_async_read(const fd_t& fd,
                ::iovec* buffer,
                uint32_t buffer_iov_cnt,
                const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                const read_options* opt)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0 || (buffer_iov_cnt > 0 && buffer == nullptr), -1);
            auto sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(sqe != nullptr, -1);
            complete_action* complete_action_ptr{ nullptr };
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_info_ptr) {
                auto new_info_ptr = std::make_shared<iouring_event_info>();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_info_ptr));
                complete_action_ptr = new_info_ptr->get_read_complete_action(fd).get();
            } else {
                complete_action_ptr = evt_info_ptr->get_read_complete_action(fd).get();
            }
            struct msghdr* msghdr_ptr = nullptr;
            if (cb != nullptr) {
                if (fd.get_fd_type() == ezio::event::FD_TYPE::TCP_FD
                        || fd.get_fd_type() == ezio::event::FD_TYPE::UDP_FD) {
                    msghdr_ptr = complete_action_ptr->get_read_msghdr();
                }
                auto tmp_cb = [buffer, buffer_iov_cnt, cb, msghdr_ptr](int32_t res, const ::iovec*, uint32_t, void*) {
                    cb(res, buffer, buffer_iov_cnt, msghdr_ptr);
                };
                complete_action_ptr->set_callback(tmp_cb);
            }

            uint64_t offset = complete_action_ptr->get_offset();
            bool is_submit = true;
            if (opt != nullptr) {
                if (STABLE_INFRA_UNLIKELY(opt->get_offset() != UINT64_MAX)) {
                    offset = opt->get_offset();
                    complete_action_ptr->set_offset(offset);
                }
                is_submit = opt->get_submit();
            }
            if (buffer != nullptr) {
                if (fd.get_fd_type() == ezio::event::FD_TYPE::TCP_FD
                        || fd.get_fd_type() == ezio::event::FD_TYPE::UDP_FD) {
                    msghdr_ptr = complete_action_ptr->get_read_msghdr();
                    msghdr_ptr->msg_iov = buffer;
                    msghdr_ptr->msg_iovlen = buffer_iov_cnt;
                    ring_ptr_->prepare_recvmsg(sqe, fd.get_fd(), msghdr_ptr, 0);
                    //io_uring_prep_recvmsg(sqe, fd.get_fd(), msghdr_ptr, 0);
                } else {
                    ring_ptr_->prepare_readv(sqe, fd.get_fd(), buffer, buffer_iov_cnt, offset);
                    //io_uring_prep_readv(sqe, fd.get_fd(), buffer, buffer_iov_cnt, offset);
                }
            } else {
                ring_ptr_->prepare_poll(sqe, fd.get_fd(), POLLIN);
                //io_uring_prep_poll_add(sqe, fd.get_fd(), POLLIN);
            }
            ring_ptr_->set_user_data(sqe, reinterpret_cast<uint64_t>(complete_action_ptr));
            //sqe->user_data = reinterpret_cast<uint64_t>(complete_action_ptr);

            if (is_submit) {
                auto ret = ring_ptr_->submit();
                STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            }
            return 0;
        }

        int32_t io_uring::cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || ! fd.is_valid(), -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(fd.get_fd());
            STABLE_INFRA_IF_TRUE_RETURN_CODE(nullptr == evt_info_ptr, -1);
            auto complete_action_ptr = evt_info_ptr->get_cancel_read_complete_action().get();
            auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                if (cb != nullptr) {
                    cb(res);
                }
            };
            complete_action_ptr->set_callback(tmp_cb);
            auto cancel_sqe = get_io_uring_sqe();
            STABLE_INFRA_CHECK_SUC(cancel_sqe != nullptr, -1);
            ring_ptr_->prepare_cancel(cancel_sqe, complete_action_ptr, 0);
            //io_uring_prep_cancel(cancel_sqe, complete_action_ptr, 0);
            //auto ret = io_uring_submit(io_uring_ptr_.get());
            auto ret = ring_ptr_->submit();
            STABLE_INFRA_CHECK_SUC(ret > 0, -1);
            return 0;
        }

        int32_t io_uring::dispatch(int32_t timeout)
        {
            if (STABLE_INFRA_UNLIKELY(timeout != last_timeout_value_)) {
                struct __kernel_timespec tmp_tm_out = {
                    .tv_sec = 0,
                    .tv_nsec = 0
                };
                if (STABLE_INFRA_UNLIKELY(timeout != 0)) {
                    if (timeout < 0) {
                        tmp_tm_out.tv_sec = 1000000000;
                    } else {
                        tmp_tm_out.tv_sec = timeout / 1000;
                        tmp_tm_out.tv_nsec = (timeout % 1000) * 1000000; 
                    }
                }
                // memorize current value for next time use
                tm_out_ = tmp_tm_out;
                last_timeout_value_ = timeout;
            }
            auto ret = ring_ptr_->wait_cqe_timeout(&tm_out_, 1);
            STABLE_INFRA_CHECK_SUC(ret > 0, -1); //-EAGAIN or other exceptions, ignore it
            for (int32_t i = 0; i < ret; ++i) {
                auto cqe = ring_ptr_->get_cqe(i);
                STABLE_INFRA_IF_TRUE_CONTINUE(STABLE_INFRA_UNLIKELY(cqe->user_data == 0));
                auto complete_action_ptr = reinterpret_cast<complete_action*>(cqe->user_data);
                STABLE_INFRA_ASSERT(nullptr != complete_action_ptr);
                if (cqe->flags & IORING_CQE_F_BUFFER) {
                    uint32_t buf_index = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
                    const auto& buf_info = buffers_[buf_index];
                    if (STABLE_INFRA_LIKELY(buf_info.iov_.iov_base != nullptr)) {
                        auto offset = buf_info.last_buf_info_->get_offset(buf_index);
                        void* data_buf = (char*)(buf_info.iov_.iov_base) + offset;
                        //std::cout << "BUF_INDEX = " << buf_index << ", len = " << cqe->res
                        //    << ", OFFSET = " << offset << std::endl;
                        if (offset < buf_info.iov_.iov_len) {
                            complete_action_ptr->complete_callback(cqe, data_buf);
                            if (STABLE_INFRA_LIKELY(buf_info.iov_.iov_base != nullptr)) {
                                buf_info.last_buf_info_->update_offset(buf_index, cqe->res);
                                ring_ptr_->add_buf_ring(buf_info.buf_rings_,
                                        buf_info.iov_.iov_base,
                                        buf_info.iov_.iov_len,
                                        buf_index, buf_info.mask_, 0);
                                ring_ptr_->buf_ring_advance(buf_info.buf_rings_, 1, buf_info.mask_);
                            }
                        }
                    }
                    if (!(cqe->flags & IORING_CQE_F_MORE)) {
                        complete_action_ptr->resubmit_multishot();
                    }
                } else {
                    void* buffer = nullptr;
                    complete_action_ptr->complete_callback(cqe, buffer);
                }
            }
            ring_ptr_->cq_consume(ret);
            return 0;
        }

        void io_uring::close()
        {
            STABLE_INFRA_IF_TRUE_RETURN(ring_ptr_ == nullptr);
            //STABLE_INFRA_IF_TRUE_RETURN(io_uring_ptr_ == nullptr);
            //io_uring_queue_exit(io_uring_ptr_.get());
            fd_to_event_info_.clear();
            //io_uring_ptr_ = nullptr;
            ring_ptr_ = nullptr;
        }
    }
}
#endif
