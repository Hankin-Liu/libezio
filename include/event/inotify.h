/****************************************************************************************
 * @file inotify.h
 * @brief event for inode, monitor file's status such as readable
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <unordered_map>
#include "../platform_define.h"
#ifdef INOTIFY_IS_SUPPORTED
#include "./event_base.h"

namespace ezio {
    namespace event {
        class inotify : public event_base
        {
            constexpr inotify(inotify&) = delete;
            inotify& operator=(inotify&) = delete;
            public:
                using pointer_t = std::shared_ptr<inotify>;
            public:
                inotify();
                virtual ~inotify();
                void handle_notify(int32_t res);
                virtual void handle_read_ready(fd_t fd) override;
                int32_t open(event_service* evt_service_ptr, uint32_t buffer_size = 4096);
                int32_t add_watch(const std::string& file_name, uint32_t events,
                                  const std::function<void(void)>& cb);
                int32_t remove_watch(int32_t watch_fd);
                int32_t close();
            private:
                void submit_read_request(const std::function<void(int32_t)>& cb = nullptr);
            private:
                fd_t fd_{};
                std::unordered_map<int32_t, std::function<void(void)>> watch_fd_to_cb_;
                std::shared_ptr<uint8_t> buffer_{ nullptr };
                ::iovec iov_{};
        };
    }
}
#endif
