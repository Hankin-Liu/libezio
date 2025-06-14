/****************************************************************************************
 * @file type_def.h
 * @brief type definition for external use
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <stdint.h>
#include <functional>
#include "platform_define.h"
#ifdef _OS_LINUX
#include <netinet/in.h>
#endif

#define INVALID_FD -1
#if defined(_OS_WINDOW_64) || defined(_OS_WINDOW_32)
struct iovec
{
    void *iov_base;
    size_t iov_len;
};
#else
#include <sys/uio.h>
#endif

namespace ezio
{
namespace event
{
    enum class FD_TYPE: uint32_t {
        TCP_FD = 1,
        UDP_FD = 2,
        FILE_FD = 3,
        ACCEPT_FD = 4,
        EVENT_FD = 5,
        TIMER_FD = 6,
        SIGNAL_FD = 7,
        INOTIFY_FD = 8,
        UDS_STREAM_FD = 9,
        UDS_DGRAM_FD = 10,
        UNKNOWN_FD = 100
    };

    class fd_t
    {
        public:
            fd_t() = default;
#if defined(_OS_WINDOW_64) || defined(_OS_WINDOW_32)
            fd_t(const SOCKET& fd, const FD_TYPE& fd_type) : fd_(fd), fd_type_(fd_type)
#else
            fd_t(const int32_t fd, const FD_TYPE& fd_type) : fd_(fd), fd_type_(fd_type)
#endif
            {
            }
            ~fd_t() = default;
        public:
            inline bool is_valid() const {
                return fd_ >= 0;
            }
            operator int32_t() const {
                return fd_;
            }
            int32_t close();
            inline FD_TYPE get_fd_type() const {
                return fd_type_;
            }
#if defined(_OS_WINDOW_64) || defined(_OS_WINDOW_32)
            inline SOCKET get_fd() const {
#else
            inline int32_t get_fd() const {
#endif
                return fd_;
            }
        private:
#if defined(_OS_WINDOW_64) || defined(_OS_WINDOW_32)
            SOCKET fd_{ INVALID_FD };
#else
            int32_t fd_{ INVALID_FD };
#endif
            FD_TYPE fd_type_{ FD_TYPE::UNKNOWN_FD };
    };

    class read_options
    {
    public:
        read_options() = default;
        ~read_options() = default;
    public:
        inline read_options& set_offset(uint64_t offset) {
            offset_ = offset;
            return *this;
        }
        inline uint64_t get_offset() const {
            return offset_;
        }
        inline read_options& set_submit(bool is_submit) {
            is_submit_ = is_submit;
            return *this;
        }
        inline bool get_submit() const {
            return is_submit_;
        }
        inline read_options& set_need_socket_detail(bool need_socket_detail) {
            need_socket_detail_ = need_socket_detail;
            return *this;
        }
        inline bool get_need_socket_detail() const {
            return need_socket_detail_;
        }
    private:
        uint64_t offset_{ UINT64_MAX };
        bool is_submit_{ true };
        bool need_socket_detail_{ false };
    };
    class write_options
    {
    public:
        write_options() = default;
        ~write_options() = default;
    public:
        inline write_options& set_offset(uint64_t offset) {
            offset_ = offset;
            return *this;
        }
        inline uint64_t get_offset() const {
            return offset_;
        }
        inline write_options& set_submit(bool is_submit) {
            is_submit_ = is_submit;
            return *this;
        }
        inline bool get_submit() const {
            return is_submit_;
        }
        inline write_options& set_sockaddr(struct sockaddr_storage* sockaddr) {
            sockaddr_ptr_ = sockaddr;
            return *this;
        }
        inline struct sockaddr_storage* get_sockaddr() const {
            return sockaddr_ptr_;
        }
    private:
        uint64_t offset_{ UINT64_MAX };
        bool is_submit_{ true };
        struct sockaddr_storage* sockaddr_ptr_{ nullptr };
    };

#ifdef _OS_LINUX
    struct sock_info
    {
        fd_t fd_{};
        struct sockaddr_storage sock_addr_{};
    };
#endif

    struct poll_param
    {
        int32_t polling_ms_{ -1 };
        uint32_t entry_cnt_{ 128 };
        uint32_t poll_type_{ UINT32_MAX };  ///< default poller
    };

    class socket_more_info
    {
        public:
            inline struct sockaddr_storage* get_sockaddr_storage() const {
                return sock_addr_;
            }
            inline void set_sockaddr_storage(struct sockaddr_storage* sockaddr) {
                sock_addr_ = sockaddr;
            }
        private:
            struct sockaddr_storage* sock_addr_{ nullptr };
    };
}
}

typedef uint8_t byte_t;
