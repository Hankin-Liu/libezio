/****************************************************************************************
 * @file complete_action.cpp
 * @brief store callback for completion event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <liburing.h>
#include <cstring>
#include <vector>
#include "../../include/event/complete_action.h"
#include "../../include/util/macros_func.h"

namespace ezio {
    namespace event {
        msghdrs::~msghdrs()
        {
            if (read_msghdr_ptr_ != nullptr) {
                if (read_msghdr_ptr_->msg_name != nullptr) {
                    delete (sockaddr_storage*)read_msghdr_ptr_->msg_name;
                }
                delete read_msghdr_ptr_;
            }
            if (write_msghdr_ptr_ != nullptr) {
                if (write_msghdr_ptr_->msg_name != nullptr) {
                    delete (sockaddr_storage*)write_msghdr_ptr_->msg_name;
                }
                delete write_msghdr_ptr_;
            }
        }

        struct msghdr* msghdrs::get_read_msghdr()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(read_msghdr_ptr_ != nullptr, read_msghdr_ptr_);
            read_msghdr_ptr_ = new msghdr{};
            read_msghdr_ptr_->msg_name = new sockaddr_storage{};
            read_msghdr_ptr_->msg_namelen = sizeof(struct sockaddr_storage);
            return read_msghdr_ptr_;
        }

        struct msghdr* msghdrs::get_write_msghdr()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(write_msghdr_ptr_ != nullptr, write_msghdr_ptr_);
            write_msghdr_ptr_ = new msghdr{};
            return write_msghdr_ptr_;
        }

        complete_action::~complete_action()
        {
            if (msghdr_ptr_ != nullptr) {
                delete msghdr_ptr_;
            }
        }
                
        struct msghdr* complete_action::get_read_msghdr()
        {
            if (msghdr_ptr_ == nullptr) {
                msghdr_ptr_ = new msghdrs{};
            }
            return msghdr_ptr_->get_read_msghdr();
        }
        
        struct msghdr* complete_action::get_write_msghdr()
        {
            if (msghdr_ptr_ == nullptr) {
                msghdr_ptr_ = new msghdrs{};
            }
            return msghdr_ptr_->get_write_msghdr();
        }
                
        void complete_action::complete_callback(const ::io_uring_cqe* cqe, void* buffer) {
            if (callback_ == nullptr) {
                return;
            }
            ::iovec iov = {
                .iov_base = buffer,
                .iov_len = 0
            };
            if (cqe->res > 0) {
                if (is_offset_enabled_) {
                    offset_ += cqe->res;
                }
                if (cqe->flags & IORING_CQE_F_BUFFER) {
                    iov.iov_len = cqe->res;
                }
            }
            callback_(cqe->res, &iov, 1, nullptr);
        }
    }
}
