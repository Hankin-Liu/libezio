/****************************************************************************************
 * @file event_common.h
 * @brief common defines
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <functional>
#include <memory>

namespace ezio {
    namespace event {
        /*
         * @breif get one io multiplexing object, such as epoll, poll, select, iocp
         * @param[in] poll_type 0 - epoll, 1 - io_uring, UINT32_MAX - auto
         * @return io multiplexing object pointer
         */
        class poll_base;
        std::shared_ptr<poll_base> get_poll_obj(uint32_t poll_type = UINT32_MAX);
    }
}
