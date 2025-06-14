/****************************************************************************************
 * @file inotify.cpp
 * @brief event for inode, monitor file's status such as readable
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/event/inotify.h"
#ifdef INOTIFY_IS_SUPPORTED
#include <sys/inotify.h>
#include <memory>
#include <string>
#include "../../include/util/util.h"
#include "../../include/util/macros_func.h"
#include "../../include/common/const_variable.h"
#include "../../include/event_service.h"

namespace ezio {
    namespace event {
        inotify::inotify()
        {
        }

        inotify::~inotify()
        {
            if (fd_ != INVALID_FD) {
                ezio::util::util_closesocket(fd_);
            }
        }

        int32_t inotify::open(event_service* evt_service_ptr, uint32_t buffer_size)
        {
            STABLE_INFRA_CHECK_SUC(evt_service_ptr_ == nullptr && evt_service_ptr != nullptr
                                   && evt_state_ == EVENT_STATE::DEF, RET_ERR);
            auto tmp_fd = inotify_init1(IN_NONBLOCK);
            fd_ = fd_t{ tmp_fd, FD_TYPE::INOTIFY_FD };
            STABLE_INFRA_CHECK_SUC(fd_.is_valid(), -1);

            uint8_t* buf = new (std::nothrow) uint8_t[buffer_size];
            STABLE_INFRA_CHECK_SUC(buf != nullptr, -1);
            std::shared_ptr<uint8_t> tmp_buf(buf, std::default_delete<uint8_t[]>());
            buffer_ = std::move(tmp_buf);

            iov_.iov_base = buffer_.get();
            iov_.iov_len = buffer_size;

            auto read_cb = std::bind(&inotify::handle_notify,
                    std::dynamic_pointer_cast<inotify>(shared_from_this()), std::placeholders::_1);
            set_read_callback(read_cb);
            uint16_t evt = EV_READ;
            auto tp = std::make_tuple(fd_, evt);
            fds_evts_.push_back(tp);
            // start to monitor this timer event
            set_evt_service_ptr(evt_service_ptr);
            evt_service_ptr_->add_event(shared_from_this());
            evt_state_ = EVENT_STATE::OPENED;
            return RET_SUC;
        }

        void inotify::handle_read_ready(fd_t fd)
        {
            const auto& read_cb = this->get_read_callback();
            submit_read_request(read_cb);
        }

        // thread 2 got the notification
        void inotify::handle_notify(int32_t res)
        {
            STABLE_INFRA_IF_TRUE_RETURN(res <= 0);
            uint32_t offset = 0;
            const uint32_t data_len = res;
            while (offset < data_len) {
                auto event = reinterpret_cast<struct inotify_event *>(buffer_.get() + offset);
                auto iter = watch_fd_to_cb_.find(event->wd);
                if (iter != watch_fd_to_cb_.end()) {
                    const auto& cb = iter->second;
                    cb();
                }
                offset += sizeof(struct inotify_event) + event->len;
            }
            submit_read_request();
        }

        int32_t inotify::add_watch(const std::string& file_name, uint32_t events,
                                   const std::function<void(void)>& cb)
        {
            STABLE_INFRA_CHECK_SUC(! file_name.empty() && events != 0 && cb != nullptr, -1);
            int watch_fd = inotify_add_watch(fd_, file_name.c_str(), events);
            STABLE_INFRA_CHECK_SUC(watch_fd > 0, -1);
            watch_fd_to_cb_[watch_fd] = cb;
            return 0;
        }

        int32_t inotify::remove_watch(int32_t watch_fd)
        {
            auto iter = watch_fd_to_cb_.find(watch_fd);
            STABLE_INFRA_CHECK_SUC(iter != watch_fd_to_cb_.end(), -1);
            auto ret = inotify_rm_watch(fd_, watch_fd);
            STABLE_INFRA_CHECK_SUC(ret != -1, -1);
            return 0;
        }

        void inotify::submit_read_request(const std::function<void(int32_t)>& cb)
        {
            if (cb == nullptr) {
                auto ret = evt_service_ptr_->submit_async_read(fd_, &iov_, 1, nullptr, &read_options().set_offset(0));
                STABLE_INFRA_ASSERT(ret == 0);
                return;
            }
            auto tmp_cb = [cb](int32_t res, const ::iovec*, uint32_t, void*) {
                cb(res);
            };
            auto ret = evt_service_ptr_->submit_async_read(fd_, &iov_, 1, tmp_cb, &read_options().set_offset(0));
            STABLE_INFRA_ASSERT(ret == 0);
        }

        int32_t inotify::close()
        {
            STABLE_INFRA_CHECK_SUC(evt_state_ == EVENT_STATE::OPENED, RET_ERR);
            event_base::close();
            fd_.close();
            watch_fd_to_cb_.clear();
            evt_state_ = EVENT_STATE::CLOSED;
            return RET_SUC;
        }
    }
}
#endif
