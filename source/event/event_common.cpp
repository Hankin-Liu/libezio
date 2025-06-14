/****************************************************************************************
 * @file event_common.cpp
 * @brief common defines
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <memory>
#include "../../include/platform_define.h"
#include "../../include/event/event_common.h"
#include "../../include/event/epoll.h"
#include "../../include/event/io_uring.h"
#include "../../include/util/util.h"

namespace ezio {
    namespace event {
        std::shared_ptr<poll_base> get_poll_obj(uint32_t poll_type)
        {
            if (poll_type != UINT32_MAX) {
                switch (poll_type) {
                    case 0: {
#ifdef EPOLL_IS_SUPPORTED
                                return std::make_shared<ezio::event::epoll>();
#else
                                return nullptr;
#endif
                            }
                    case 1: {
#ifdef IOURING_IS_SUPPORTED
                                return std::make_shared<ezio::event::io_uring>();
#else
                                return nullptr;
#endif
                            }
                    default:
                            return nullptr;
                }
            }
#ifdef IOURING_IS_SUPPORTED
            return std::make_shared<ezio::event::io_uring>();
#else

#ifdef EPOLL_IS_SUPPORTED
            return std::make_shared<ezio::event::epoll>();
#else
            // TODO other poller
            return nullptr;
#endif

#endif
        }

        int32_t fd_t::close()
        {
            if (fd_ == INVALID_FD) {
                return -1;
            }
            ezio::util::util_closesocket(*this);
            fd_ = INVALID_FD;
            fd_type_ = FD_TYPE::UNKNOWN_FD;
            return 0;
        }
    }
}
