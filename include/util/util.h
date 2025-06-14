/****************************************************************************************
 * @file util.h
 * @brief common functions for general use
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>
#if defined(_OS_LINUX)
#include <unistd.h>
#include <dirent.h>
#endif
#include <string>
#include <thread>
#include <vector>
#include "../platform_define.h"
#include "../util/macros_func.h"
#include "../common/const_variable.h"
#include "../type_def.h"
#include "../event/event_common.h"

namespace ezio {
    namespace util {
        /*
         *  Determine whether some value is a power of two, where zero is
         * *not* considered a power of two.
         */
        bool is_power_of_2(uint64_t n);

        uint64_t roundup_power_of_two(uint64_t val);

        inline uint64_t roundup_align(uint64_t val, uint64_t align_size)
        {
            return (((val) + align_size - 1) & ~(align_size - 1));
        }

        int32_t util_make_fd_close_on_exec(ezio::event::fd_t fd);

        int32_t util_closesocket(ezio::event::fd_t sock);
        
        int32_t util_make_fd_nonblocking(ezio::event::fd_t fd);

        int32_t move_iov(::iovec*& iov, uint32_t& iov_cnt, uint32_t move_size);

        ezio::event::FD_TYPE get_fd_type(int fd);

        void* alloc_aligned_mem(uint64_t buffer_size, uint64_t align_size);

        std::vector<std::string> split(const std::string& str, const std::string& delim);

        int32_t util_make_listen_socket_reuseable_port(int32_t fd);

        int32_t util_make_listen_socket_reuseable(int32_t fd);
    }
}
