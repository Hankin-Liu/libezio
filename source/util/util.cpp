/****************************************************************************************
 * @file util.cpp
 * @brief common functions for general use
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include "../../include/platform_define.h"
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <netinet/tcp.h>
#endif
#include <stdio.h>
#include <chrono>
#include "../../include/util/util.h"

namespace ezio {
    namespace util {

        bool is_power_of_2(uint64_t n)
        {
            return (n != 0 && ((n & (n - 1)) == 0));
        }

        uint64_t roundup_power_of_two(uint64_t val)
        {
            if((val & (val-1)) == 0)
                return val;

            uint64_t maxulong = (uint64_t)((uint64_t)~0);
            uint64_t andv = ~(maxulong&(maxulong>>1));
            while((andv & val) == 0)
                andv = andv>>1;

            return andv<<1;
        }

        int32_t util_make_fd_close_on_exec(ezio::event::fd_t fd)
        {
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
            int flags;
            if ((flags = fcntl(fd, F_GETFD, NULL)) < 0) {
                return RET_ERR;
            }
            if (!(flags & FD_CLOEXEC)) {
                if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
                    return RET_ERR;
                }
            }
#endif
            return RET_SUC;
        }

        int32_t util_closesocket(ezio::event::fd_t sock)
        {
            int32_t ret = 0;
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
            ret = close(sock);
#else
            ret = closesocket(sock);
#endif
            if (0 != ret) {
                return RET_ERR;
            } else {
                return RET_SUC;
            }
        }

        int32_t util_make_fd_nonblocking(ezio::event::fd_t fd)
        {
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
            {
                int flags;
                if ((flags = fcntl(fd, F_GETFL, NULL)) < 0) {
                    return RET_ERR;
                }
                if (!(flags & O_NONBLOCK)) {
                    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                        return RET_ERR;
                    }
                }
            }
#else
            {
                unsigned long nonblocking = 1;
                if (ioctlsocket(fd, FIONBIO, &nonblocking) == SOCKET_ERROR) {
                    LOG_BASE_WARN(fd, "fcntl(%d, F_GETFL)", (int32_t)fd);
                    return RET_ERR;
                }
            }
#endif
            return RET_SUC;
        }

        int32_t move_iov(::iovec*& iov, uint32_t& iov_cnt, uint32_t move_size)
        {
            if (iov_cnt == 0) {
                return -1;
            }
            uint32_t tmp_iov_cnt = iov_cnt;
            for (uint32_t i = 0; i < tmp_iov_cnt; ++i) {
                if (move_size >= iov->iov_len) {
                    move_size -= iov->iov_len;
                    ++iov;
                    --iov_cnt;
                    continue;
                }
                iov->iov_base = (char*)iov->iov_base + move_size;
                iov->iov_len -= move_size;
                return 0;
            }
            return -1;
        }

        ezio::event::FD_TYPE get_fd_type(int fd)
        {
            struct stat st;
            if (fstat(fd, &st) == 0) {
                if (S_ISSOCK(st.st_mode)) {
                    int type;
                    socklen_t len = sizeof(type);
                    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0) {
                        if (type == SOCK_STREAM) {
                            return ezio::event::FD_TYPE::TCP_FD;
                        } else if (type == SOCK_DGRAM) {
                            return ezio::event::FD_TYPE::UDP_FD;
                        }
                    }
                    return ezio::event::FD_TYPE::UNKNOWN_FD;
                } else {
                    // FILE_FD, SIGNAL_FD, EVENT_FD, TIMER_FD -> OTHER_FD
                    return ezio::event::FD_TYPE::FILE_FD;
                }
            }
            return ezio::event::FD_TYPE::UNKNOWN_FD;
        }

        void* alloc_aligned_mem(uint64_t buffer_size, uint64_t align_size)
        {
            void* result{ nullptr };
            auto is_valid = is_power_of_2(align_size);
            STABLE_INFRA_CHECK_SUC(is_valid, nullptr);
#if __cplusplus >= 201703L
            result = std::aligned_alloc(align_size, buffer_size);
#else
#if defined(_OS_LINUX)
            {
                auto ret = posix_memalign((void**)(&result), align_size, buffer_size);
                STABLE_INFRA_CHECK_SUC(ret == 0, nullptr);
            }
#else
            result = (byte_t*)malloc(real_buffer_size);
#endif
#endif
            return result;
        }

        std::vector<std::string> split(const std::string& str, const std::string& delim)
        {
            std::vector<std::string> result;
            const char* str_c = str.c_str();
            const char* delim_c = delim.c_str();
            char* temp = strtok(const_cast<char*>(str_c), delim_c);
            while (nullptr != temp) {
                std::string partial_str{temp};
                result.emplace_back(partial_str);
                temp = strtok(NULL, delim_c);
            }
            return result;
        }

        int32_t util_make_listen_socket_reuseable(int32_t fd)
        {
            STABLE_INFRA_CHECK_SUC(fd > 0, -1);
            int32_t ret = 0;
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
            int one = 1;
            /* REUSEADDR on Unix means, "don't hang on to this address after the
             * listener is closed."  On Windows, though, it means "don't keep other
             * processes from binding to this address while we're using it. */
            ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*) &one, sizeof(one));
#else
#endif
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            return 0;
        }

        int32_t util_make_listen_socket_reuseable_port(int32_t fd)
        {
            STABLE_INFRA_CHECK_SUC(fd > 0, -1);
            int32_t ret = 0;
#if !defined(_OS_WINDOW_64) && !defined(_OS_WINDOW_32)
            int one = 1;
            /* REUSEPORT on Linux 3.9+ means, "Multiple servers (processes or
             * threads) can bind to the same port if they each set the option. */
            ret = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (void*) &one, sizeof(one));
#else
#endif
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            return 0;
        }
    }
}
