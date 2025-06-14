/****************************************************************************************
 * @file event_base.cpp
 * @brief base class for events
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/

#include "../../include/event_service.h"
#include "../../include/event/event_base.h"

namespace ezio {
    namespace event {
        void event_base::close()
        {
            if (evt_service_ptr_ != nullptr) {
                evt_service_ptr_->remove_event(shared_from_this());
            }
            clear_fds_and_evts();
            clear_read_callback();
            clear_write_callback();
            clear_cancel_read_callback();
            clear_cancel_write_callback();
        }

        void event_base::handle_event_ready(int32_t ret, fd_t fd, bool is_read)
        {
            if (ret < 0) {
                // TODO error callback
                return;
            }
            if (is_read) {
                handle_read_ready(fd);
            } else {
                handle_write_ready(fd);
            }
        }
    }
}
