/****************************************************************************************
 * @file complete_action.h
 * @brief store callback for each io uring completion event
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <vector>
#include <deque>
#include "event_common.h"
#include "../type_def.h"

namespace ezio {
    namespace event {
        class sock_info;
        class msghdrs final
        {
        public:
            msghdrs() = default;
            ~msghdrs();

            struct msghdr* get_read_msghdr();
            struct msghdr* get_write_msghdr();
        private:
            struct msghdr* read_msghdr_ptr_{ nullptr };
            struct msghdr* write_msghdr_ptr_{ nullptr };
        };
        class complete_action
        {
            public:
                complete_action() = default;
                ~complete_action();

                inline void set_callback(const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb) {
                    callback_ = cb;
                }
                void complete_callback(const ::io_uring_cqe* cqe, void* buffer);
                inline void set_addr_buffer(sock_info* addr_buffer) {
                    addr_buffer_ = addr_buffer;
                }
                inline sock_info*& get_addr_buffer() {
                    return addr_buffer_;
                }
                inline void cleanup() {
                    callback_ = nullptr;
                }
                inline uint64_t get_offset() const {
                    return offset_;
                }
                inline void set_offset(uint64_t offset) {
                    offset_ = (is_offset_enabled_) ? offset : 0;
                }
                inline void disable_offset() {
                    is_offset_enabled_ = false;
                }
                inline void set_resubmit_multishot_func(const std::function<void(void)>&& func) {
                    resubmit_multishot_func_ = func;
                }
                inline void resubmit_multishot() {
                    if (resubmit_multishot_func_ != nullptr) {
                        resubmit_multishot_func_();
                    }
                }
                struct msghdr* get_read_msghdr();
                struct msghdr* get_write_msghdr();
            private:
                std::function<void(int32_t, const ::iovec*, uint32_t, void*)> callback_{nullptr};
                sock_info* addr_buffer_{ nullptr };
                bool is_offset_enabled_{ true };
                uint64_t offset_{ 0 };
                std::function<void(void)> resubmit_multishot_func_{ nullptr };
                msghdrs* msghdr_ptr_{ nullptr };
        };
    }
}
