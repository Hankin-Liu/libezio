/****************************************************************************************
 * @file event_service.h
 * @brief event_service class
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <cstdint>
#include <thread>
#include <memory>
#include <string>
#include <functional>
#include "type_def.h"

namespace ezio {
    namespace event {
        class event_loop;
        class event_base;
        class inotify;
        class notifier;
        class event_service final
        {
            constexpr event_service(event_service&) = delete;
            event_service& operator=(event_service&) = delete;
            public:
                event_service();
                ~event_service();
            public:
                /** open event service
                 * @param param, parameters for opening. 
                 * @return 0 - success, otherwise failed
                 */
                int32_t open(const poll_param& param);

                /** close event service
                */
                int32_t close();

                /** get the thread id of the event_service
                */
                std::thread::id get_thread_id() const;

                /** start loop
                */
                void start_loop();

                int32_t run_job(const std::function<void(void)>& job);
                void add_event(const std::shared_ptr<event_base>& eh);
                void remove_event(const std::shared_ptr<event_base> &eh);
            public:
                int32_t register_ring_buffer(uint32_t block_size, uint32_t block_count);
                int32_t unregister_ring_buffer(uint16_t buf_group_id);
                int32_t submit_async_read(const fd_t& fd,
                                          ::iovec* buffer,
                                          uint32_t buffer_iov_cnt,
                                          const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                          const read_options* opt = nullptr);
                int32_t submit_async_read(const fd_t& fd,
                                          uint32_t buffer_group_id,
                                          const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                          const read_options* opt = nullptr);
                int32_t cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb = nullptr);

                int32_t submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb = nullptr);
                int32_t cancel_async_accept(const fd_t& listen_fd, const std::function<void(int32_t)>& cb);

                int32_t submit_async_write(const fd_t& fd,
                        ::iovec* buffer,
                        uint32_t buffer_iov_cnt,
                        const std::function<void(int32_t)>& cb = nullptr,
                        const write_options* opt = nullptr);
                int32_t cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb);

                int32_t create_timer(uint64_t interval_s, uint64_t interval_ns,
                        const std::function<void(uint64_t)>& cb);
                int32_t close_timer(int32_t timer_id);

                int32_t submit();

                std::shared_ptr<inotify> create_watch_obj(uint32_t buffer_size = 4096);
                int32_t watch_file(const std::shared_ptr<inotify>& inotify_ptr,
                                   const std::string& file_name, uint32_t events,
                                   const std::function<void(void)>& cb);
                int32_t remove_watch(const std::shared_ptr<inotify>& inotify_ptr,
                                     int32_t watch_fd);
                int32_t close_watch_obj(const std::shared_ptr<inotify>& inotify_ptr);

                std::shared_ptr<ezio::event::notifier> create_notifier(const std::function<void(void)>& cb,
                             const std::function<void(void)>& cb_for_stop = nullptr);
                void notify(const std::shared_ptr<ezio::event::notifier>& ntf_ptr);
                int32_t close_notifier(const std::shared_ptr<ezio::event::notifier>& ntf_ptr);

            private:
                std::shared_ptr<ezio::event::event_loop> evt_loop_{ nullptr };
        };
    }
}
