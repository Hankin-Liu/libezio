/****************************************************************************************
 * @file epoll.cpp
 * @brief encapsulation of epoll
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/platform_define.h"
#ifdef EPOLL_IS_SUPPORTED
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <memory>
#include "../../include/event/epoll.h"
#include "../../include/event/event_action.h"
#include "../../include/util/util.h"
#include "../../include/util/macros_func.h"
#include "../../include/event/timer.h"
#include "../../include/common/internal_type_def.h"

namespace ezio {
    namespace event {
        epoll::epoll() {
            events_ptr_ = std::unique_ptr<epoll_event[]>(new epoll_event[EVENT_CNT]);
        }

        epoll::~epoll() {
            close();
        }

        bool epoll::init(uint32_t)
        {
            // First, try the new epoll_create1 interface.
            auto fd = epoll_create1(EPOLL_CLOEXEC);
            epfd_ = fd_t{ fd, FD_TYPE::FILE_FD };
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! epfd_.is_valid(), false);
            return true;
        }

        int32_t epoll::register_ring_buffer(uint32_t block_size, uint32_t block_count)
        {
            auto buf = ezio::data_struct::ring_buffer::get_unique_ptr();
            auto ret = buf->init(block_count, block_size);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            auto buf_info = std::make_shared<ring_buffer_info>();
            buf_info->buf_ = std::move(buf);
            auto buf_grp_id = buffers_.size();
            buffers_.emplace_back(std::move(buf_info));
            return buf_grp_id;
        }

        void epoll::remove_multishot_task(const std::unordered_set<uint32_t> fd_set)
        {
            for (const auto fd : fd_set) {
                const auto& evt_ptr = fd_to_event_info_.find(fd);
                STABLE_INFRA_IF_TRUE_CONTINUE(evt_ptr == nullptr || evt_ptr->event_action_ptr_ == nullptr);
                evt_ptr->event_action_ptr_->clear_multishot_task();
            }
        }
        
        int32_t epoll::unregister_ring_buffer(uint16_t buf_group_id)
        {
            STABLE_INFRA_CHECK_SUC(is_buffer_effective(buf_group_id), -1);
            remove_multishot_task(buffers_[buf_group_id]->fd_set_);
            buffers_[buf_group_id] = nullptr;
            return 0;
        }

        int32_t epoll::create_timer(uint64_t interval_s, uint64_t interval_ns, const std::function<void(uint64_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC((interval_s != 0 || interval_ns != 0) && cb != nullptr, -1);
            auto timer_ptr = std::make_shared<ezio::event::timer>();
            auto ret = timer_ptr->open(this, interval_s, interval_ns);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            ret = timer_ptr->reg_timer_handler(cb);
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            int32_t timer_id = (int32_t)timer_id_;
            id_to_timer_[timer_id_] = timer_ptr;
            ++timer_id_;
            return timer_id;
        }

        int32_t epoll::close_timer(int32_t timer_id)
        {
            auto iter = id_to_timer_.find(timer_id);
            STABLE_INFRA_CHECK_SUC(iter != id_to_timer_.end(), -1);
            iter->second->close();
            id_to_timer_.erase(iter);
            return 0;
        }

        int32_t epoll::submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && listen_fd.is_valid(), -1);
            event_info* evt_info_ptr{ nullptr };
            const auto& evt_ptr = fd_to_event_info_.find(listen_fd);
            if (STABLE_INFRA_UNLIKELY(nullptr == evt_ptr)) {
                auto new_evt_info_ptr = std::make_shared<event_info>(listen_fd);
                evt_info_ptr = new_evt_info_ptr.get();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(listen_fd, new_evt_info_ptr));
            } else {
                evt_info_ptr = evt_ptr.get();
            }
            if (addr_buffer != nullptr) {
                evt_info_ptr->event_action_ptr_->set_addr_buffer(addr_buffer);
            }
            if (cb != nullptr) {
                auto& addr_buf = evt_info_ptr->event_action_ptr_->get_addr_buffer();
                auto tmp_cb = [cb, addr_buf](int32_t ret, const ::iovec*, uint32_t, void*) {
                    cb(ret, *addr_buf);
                };
                evt_info_ptr->event_action_ptr_->set_read_callback(tmp_cb);
            }
            if ((evt_info_ptr->events_ & EV_READ) == 0) {
                // event changed
                evt_info_ptr->events_ |= EV_READ | EV_ET;
                push_to_change_list(evt_info_ptr);
            }
            evt_info_ptr->event_action_ptr_->add_accept_task();
            if (evt_info_ptr->event_action_ptr_->is_readable()) {
                // let epoll to trigger
                ready_events_.push_back(evt_info_ptr->event_action_ptr_);
            }
            return 0;
        }

        int32_t epoll::cancel_async_accept(const fd_t& listen_fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && listen_fd.is_valid(), -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(listen_fd);
            STABLE_INFRA_CHECK_SUC(nullptr != evt_info_ptr, -1);
            evt_info_ptr->event_action_ptr_->disable_read();
            STABLE_INFRA_CHECK_SUC(nullptr != cb, -1);
            cb(0);
            return 0;
        }

        int32_t epoll::submit_async_write(const fd_t& fd,
                                          ::iovec* buffer,
                                          uint32_t buffer_iov_cnt,
                                          const std::function<void(int32_t)>& cb,
                                          const write_options* opt)
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0 || (buffer_iov_cnt > 0 && buffer == nullptr), -1);
            event_info* evt_info_ptr{ nullptr };
            const auto& evt_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_ptr) {
                auto new_evt_info_ptr = std::make_shared<event_info>(fd);
                evt_info_ptr = new_evt_info_ptr.get();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_evt_info_ptr));
            } else {
                evt_info_ptr = evt_ptr.get();
            }
            const auto& evt_action_ptr = evt_info_ptr->event_action_ptr_;
            if (cb != nullptr) {
                evt_action_ptr->set_write_callback(cb);
            }
            if ((evt_info_ptr->events_ & EV_WRITE) == 0
                && fd.get_fd_type() != FD_TYPE::FILE_FD) {
                // epoll not support file fd
                // event changed
                evt_info_ptr->events_ |= EV_WRITE | EV_ET;
                push_to_change_list(evt_info_ptr);
            }
            uint64_t offset = evt_action_ptr->get_write_offset();
            bool is_submit = true;
            struct sockaddr_storage* sockaddr = nullptr; // for udp fd
            if (opt != nullptr) {
                if (STABLE_INFRA_UNLIKELY(opt->get_offset() != UINT64_MAX)) {
                    offset = opt->get_offset();
                    evt_action_ptr->set_write_offset(offset);
                }
                is_submit = opt->get_submit();
                if (opt->get_sockaddr() != nullptr
                    && fd.get_fd_type() == FD_TYPE::UDP_FD) {
                    sockaddr = opt->get_sockaddr();
                }
            }
            task t(buffer, buffer_iov_cnt, sockaddr); 
            if (is_submit) {
                evt_action_ptr->add_write_task(t);
                if (evt_action_ptr->is_writable()) {
                    // let epoll to trigger
                    ready_events_.push_back(evt_info_ptr->event_action_ptr_);
                }
            } else {
                evt_action_ptr->add_non_submit_write_task(t);
                non_submit_events_.insert(evt_action_ptr);
            }
            return 0;
        }

        int32_t epoll::cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && fd > 0, -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            STABLE_INFRA_CHECK_SUC(nullptr != evt_info_ptr, -1);
            evt_info_ptr->event_action_ptr_->disable_write();
            STABLE_INFRA_CHECK_SUC(nullptr != cb, -1);
            cb(0);
            return 0;
        }

        int32_t epoll::submit_async_read(const fd_t& fd, uint32_t buffer_group_id,
                                         const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                                         const read_options* opt)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && fd > 0
                    && is_buffer_effective(buffer_group_id), -1);
            STABLE_INFRA_CHECK_SUC(fd.get_fd_type() == ezio::event::FD_TYPE::TCP_FD || fd.get_fd_type() == ezio::event::FD_TYPE::UDP_FD, -1);
            event_info* evt_info_ptr{ nullptr };
            const auto& evt_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_ptr) {
                auto new_evt_info_ptr = std::make_shared<event_info>(fd);
                evt_info_ptr = new_evt_info_ptr.get();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_evt_info_ptr));
            } else {
                evt_info_ptr = evt_ptr.get();
            }
            if (cb != nullptr) {
                evt_info_ptr->event_action_ptr_->set_read_callback(cb);
            }
            if ((evt_info_ptr->events_ & EV_READ) == 0) {
                // event changed
                evt_info_ptr->events_ |= EV_READ | EV_ET;
                push_to_change_list(evt_info_ptr);
            }
            bool is_submit = true;
            bool need_sock_detail = false;
            if (opt != nullptr) {
                is_submit = opt->get_submit();
                need_sock_detail = opt->get_need_socket_detail();
            }
            multishot_task t(buffers_[buffer_group_id].get(), need_sock_detail, is_submit); 
            evt_info_ptr->event_action_ptr_->add_multishot_read_task(t);
            if (is_submit) {
                if (evt_info_ptr->event_action_ptr_->is_readable()) {
                    // let epoll to trigger
                    ready_events_.push_back(evt_info_ptr->event_action_ptr_);
                }
            } else {
                non_submit_events_.insert(evt_info_ptr->event_action_ptr_);
            }
            return 0;
        }

        int32_t epoll::submit()
        {
            STABLE_INFRA_CHECK_SUC(! non_submit_events_.empty(), -1);
            for (const auto& evt_action_ptr : non_submit_events_) {
                evt_action_ptr->submit();
                if (evt_action_ptr->is_readable() || evt_action_ptr->is_writable()) {
                    // let epoll to trigger
                    ready_events_.push_back(evt_action_ptr);
                }
            }
            non_submit_events_.clear();
            return 0;
        }

        int32_t epoll::submit_async_read(const fd_t& fd,
                ::iovec* buffer,
                uint32_t buffer_iov_cnt,
                const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                const read_options* opt
                )
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(! is_inited() || fd < 0 || (buffer_iov_cnt > 0 && buffer == nullptr), -1);
            event_info* evt_info_ptr{ nullptr };
            const auto& evt_ptr = fd_to_event_info_.find(fd);
            if (nullptr == evt_ptr) {
                auto new_evt_info_ptr = std::make_shared<event_info>(fd);
                evt_info_ptr = new_evt_info_ptr.get();
                STABLE_INFRA_ASSERT(fd_to_event_info_.insert(fd, new_evt_info_ptr));
            } else {
                evt_info_ptr = evt_ptr.get();
            }
            const auto& evt_action_ptr = evt_info_ptr->event_action_ptr_;
            if (cb != nullptr) {
                evt_action_ptr->set_read_callback(cb);
            }
            if ((evt_info_ptr->events_ & EV_READ) == 0
                && fd.get_fd_type() != FD_TYPE::FILE_FD) {
                // epoll not support file fd
                // event changed
                evt_info_ptr->events_ |= EV_READ | EV_ET;
                push_to_change_list(evt_info_ptr);
            }
            task t(buffer, buffer_iov_cnt); 
            uint64_t offset = evt_action_ptr->get_read_offset();
            bool is_submit = true;
            bool need_sock_detail = false;
            if (opt != nullptr) {
                if (STABLE_INFRA_UNLIKELY(opt->get_offset() != UINT64_MAX)) {
                    offset = opt->get_offset();
                    evt_action_ptr->set_read_offset(offset);
                }
                is_submit = opt->get_submit();
                need_sock_detail = opt->get_need_socket_detail();
                t.set_need_sock_detail(need_sock_detail);
            }
            if (is_submit) {
                evt_action_ptr->add_read_task(t);
                if (evt_action_ptr->is_readable()) {
                    // let epoll to trigger
                    ready_events_.push_back(evt_action_ptr);
                }
            } else {
                evt_action_ptr->add_non_submit_read_task(t);
                non_submit_events_.insert(evt_action_ptr);
            }
            return 0;
        }

        int32_t epoll::cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(is_inited() && fd > 0, -1);
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            STABLE_INFRA_CHECK_SUC(nullptr != evt_info_ptr, -1);
            STABLE_INFRA_CHECK_SUC(evt_info_ptr->event_action_ptr_->is_reading(), -1);
            const auto& evt_action_ptr = evt_info_ptr->event_action_ptr_;
            evt_action_ptr->disable_read();
            if (evt_action_ptr->is_none_evt()) {
                // event changed
                push_to_change_list(evt_info_ptr.get());
            }
            STABLE_INFRA_CHECK_SUC(nullptr != cb, -1);
            if (cb != nullptr) {
                cb(0);
            }
            return 0;
        }

        void epoll::remove_event_info(const fd_t& fd)
        {
            const auto& evt_info_ptr = fd_to_event_info_.find(fd);
            STABLE_INFRA_IF_TRUE_RETURN(nullptr == evt_info_ptr);
            removed_event_info_.emplace_back(std::move(evt_info_ptr));
            fd_to_event_info_.erase(fd);
        }

        void epoll::apply_one_change(event_info* evt_info_ptr)
        {
            STABLE_INFRA_ASSERT(nullptr != evt_info_ptr);
            int op = EPOLL_CTL_ADD;
            auto events = evt_info_ptr->event_action_ptr_->events();
            if (! evt_info_ptr->event_action_ptr_->is_none_evt()) {
                if (evt_info_ptr->is_in_epoll_) {
                    op = EPOLL_CTL_MOD;
                }
            } else {
                if (evt_info_ptr->is_in_epoll_) {
                    op = EPOLL_CTL_DEL;
                } else {
                    remove_event_info(evt_info_ptr->fd_);
                    return;
                }
            }
            if (evt_info_ptr->events_ & EV_ET) {
                events |= EPOLLET;
            }

            struct epoll_event ep_evt{};
            ep_evt.events = events;
            ep_evt.data.ptr = (void*)(evt_info_ptr->event_action_ptr_.get());
            if (epoll_ctl(epfd_, op, evt_info_ptr->fd_, &ep_evt) == 0) {
                if (EPOLL_CTL_DEL != op) {
                    evt_info_ptr->is_in_epoll_ = true;
                    evt_info_ptr->is_in_change_list_ = false;
                } else {
                    remove_event_info(evt_info_ptr->fd_);
                }
                return;
            }
            switch (op) {
                case EPOLL_CTL_MOD:
                    if (errno == ENOENT) {
                        //If a MOD operation fails with ENOENT, the fd was probably closed and re-opened.
                        //We should retry the operation as an ADD.
                        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, evt_info_ptr->fd_, &ep_evt) == 0) {
                            evt_info_ptr->is_in_epoll_ = true;
                        }
                    }
                    break;
                case EPOLL_CTL_ADD:
                    if (errno == EEXIST) {
                        // If an ADD operation fails with EEXIST, either the operation was redundant
                        // (as with a precautionary add), or we ran into a fun kernel bug where using
                        // dup*() to duplicate the same file into the same fd gives you the same epitem
                        // rather than a fresh one.  For the second case, we must retry with MOD.
                        if (epoll_ctl(epfd_, EPOLL_CTL_MOD, evt_info_ptr->fd_, &ep_evt) == 0) {
                            evt_info_ptr->is_in_epoll_ = true;
                        }
                    }
                    break;
                case EPOLL_CTL_DEL:
                    if (errno == ENOENT || errno == EBADF || errno == EPERM) {
                        // If a delete fails with one of these errors, that's fine too: we closed the fd
                        // before we got around to calling epoll_dispatch.
                        fd_to_event_info_.erase(evt_info_ptr->fd_);
                        return;
                    }
                    break;
                default:
                    break;
            }

            evt_info_ptr->is_in_change_list_ = false;
        }

        void epoll::apply_changes()
        {
            for (auto& evt_info_ptr : evt_change_lst_) {
                apply_one_change(evt_info_ptr);
            }
        }

        int32_t epoll::dispatch(int32_t timeout)
        {
            if (! evt_change_lst_.empty()) {
                apply_changes();
                evt_change_lst_.clear();
            }
            if (! ready_events_.empty()) {
                timeout = 0;
            }
            auto res = epoll_wait(epfd_, events_ptr_.get(), EVENT_CNT, timeout);

            if (res == -1) {
                STABLE_INFRA_CHECK_SUC(errno == EINTR, -1);
                return (0);
            }
            STABLE_INFRA_ASSERT(res <= EVENT_CNT);
            for (auto i = 0; i < res; ++i) {
                event_action* cb = static_cast<event_action*>(events_ptr_[i].data.ptr);
                STABLE_INFRA_ASSERT(nullptr != cb);
                cb->set_ready_events(events_ptr_[i].events);
            }
            do_ready_tasks();
            if (! removed_event_info_.empty()) {
                removed_event_info_.clear();
            }
            return 0;
        }

        void epoll::do_ready_tasks()
        {
            constexpr bool is_event_triggered = false;
            uint32_t ready_cnt = ready_events_.size();
            for (uint32_t i = 0; i < ready_cnt; ++i) {
                const auto& evt_action_ptr = ready_events_.front();
                evt_action_ptr->handle_events(is_event_triggered);
                ready_events_.pop_front();
            }
        }

        void epoll::close()
        {
            if (epfd_ != INVALID_FD) {
                epfd_.close();
                fd_to_event_info_.clear();
                evt_change_lst_.clear();
            }
        }
    }
}
#endif
