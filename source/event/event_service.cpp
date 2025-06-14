/****************************************************************************************
 * @file event_service.cpp
 * @brief event_service class
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/event_service.h"
#include <string>
#include <memory>
#include "../../include/event/event_loop.h"
#include "../../include/event/inotify.h"
#include "../../include/event/notifier.h"

namespace ezio {
    namespace event {
        event_service::event_service() 
        {
            evt_loop_ = std::make_shared<event_loop>();
        }

        event_service::~event_service()
        {
        }

        int32_t event_service::register_ring_buffer(uint32_t block_size, uint32_t block_count)
        {
            return evt_loop_->get_poll()->register_ring_buffer(block_size, block_count);
        }
        
        int32_t event_service::unregister_ring_buffer(uint16_t buf_group_id)
        {
            return evt_loop_->get_poll()->unregister_ring_buffer(buf_group_id);
        }

        int32_t event_service::open(const poll_param& param)
        {
            return evt_loop_->open(param);
        }

        int32_t event_service::close()
        {
            return evt_loop_->close();
        }

        std::thread::id event_service::get_thread_id() const
        {
            return evt_loop_->get_thread_id();
        }

        void event_service::start_loop()
        {
            evt_loop_->start_loop();
        }

        int32_t event_service::run_job(const std::function<void(void)>& job)
        {
            return evt_loop_->run_in_event_loop(job);
        }

        void event_service::add_event(const std::shared_ptr<event_base>& eh)
        {
            evt_loop_->add_event(eh);
        }

        void event_service::remove_event(const std::shared_ptr<event_base>& eh)
        {
            evt_loop_->remove_event(eh);
        }

        int32_t event_service::create_timer(uint64_t interval_s, uint64_t interval_ns,
                const std::function<void(uint64_t)>& cb)
        {
            return evt_loop_->get_poll()->create_timer(interval_s, interval_ns, cb);
        }

        int32_t event_service::close_timer(int32_t timer_id)
        {
            return evt_loop_->get_poll()->close_timer(timer_id);
        }

        int32_t event_service::submit_async_read(const fd_t& fd,
                ::iovec* buffer,
                uint32_t buffer_iov_cnt,
                const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                const read_options* opt)
        {
            return evt_loop_->get_poll()->submit_async_read(fd, buffer, buffer_iov_cnt, cb, opt);
        }

        int32_t event_service::submit_async_read(const fd_t& fd,
                                                 uint32_t buffer_group_id,
                                                 const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb,
                                                 const read_options* opt)
        {
            return evt_loop_->get_poll()->submit_async_read(fd, buffer_group_id, cb, opt);
        }

        int32_t event_service::cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            return evt_loop_->get_poll()->cancel_async_read(fd, cb);
        }

        int32_t event_service::submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb)
        {
            return evt_loop_->get_poll()->submit_async_accept(listen_fd, addr_buffer, cb);
        }

        int32_t event_service::cancel_async_accept(const fd_t& listen_fd, const std::function<void(int32_t)>& cb)
        {
            return evt_loop_->get_poll()->cancel_async_accept(listen_fd, cb);
        }

        int32_t event_service::submit_async_write(const fd_t& fd,
                ::iovec* buffer,
                uint32_t buffer_iov_cnt,
                const std::function<void(int32_t)>& cb,
                const write_options* opt)
        {
            return evt_loop_->get_poll()->submit_async_write(fd, buffer, buffer_iov_cnt, cb, opt);
        }

        int32_t event_service::cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb)
        {
            return evt_loop_->get_poll()->cancel_async_write(fd, cb);
        }

        int32_t event_service::submit()
        {
            return evt_loop_->get_poll()->submit();
        }

        std::shared_ptr<inotify> event_service::create_watch_obj(uint32_t buffer_size)
        {
            auto inotify_ptr = std::make_shared<inotify>();
            auto ret = inotify_ptr->open(this, buffer_size);
            STABLE_INFRA_CHECK_SUC(ret == 0, nullptr);
            return inotify_ptr;
        }

        int32_t event_service::watch_file(const std::shared_ptr<inotify>& inotify_ptr,
                                          const std::string& file_name, uint32_t events,
                                          const std::function<void(void)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(inotify_ptr != nullptr, -1);
            return inotify_ptr->add_watch(file_name, events, cb);
        }

        int32_t event_service::remove_watch(const std::shared_ptr<inotify>& inotify_ptr,
                                            int32_t watch_fd)
        {
            STABLE_INFRA_CHECK_SUC(inotify_ptr != nullptr, -1);
            return inotify_ptr->remove_watch(watch_fd);
        }
                
        int32_t event_service::close_watch_obj(const std::shared_ptr<inotify>& inotify_ptr)
        {
            STABLE_INFRA_CHECK_SUC(inotify_ptr != nullptr, -1);
            return inotify_ptr->close();
        }

        std::shared_ptr<ezio::event::notifier> event_service::create_notifier(const std::function<void(void)>& cb, const std::function<void(void)>& cb_for_stop)
        {
            auto ntf_ptr = std::make_shared<ezio::event::notifier>();
            auto ret = ntf_ptr->open(this, cb, cb_for_stop);
            STABLE_INFRA_CHECK_SUC(ret == 0, nullptr);
            return ntf_ptr;
        }

        void event_service::notify(const std::shared_ptr<ezio::event::notifier>& ntf_ptr)
        {
            ntf_ptr->notify();
        }

        int32_t event_service::close_notifier(const std::shared_ptr<ezio::event::notifier>& ntf_ptr)
        {
            return ntf_ptr->close();
        }
    }
}
