/****************************************************************************************
 * @file fd_io_operation.h
 * @brief read or write fd
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include "../platform_define.h"
#ifdef EPOLL_IS_SUPPORTED
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <cstdint>
#include <cerrno>
#include "../type_def.h"
#include "../util/util.h"

namespace ezio {
    namespace event {
#define FD_TYPE_TCP      1
#define FD_TYPE_UDP      2
#define FD_TYPE_FILE   3
#define FD_TYPE_EVENT_FD   5
#define FD_TYPE_SIGNAL_FD   6
#define FD_TYPE_INOTIFY  8
#define FD_TYPE_UDS_STREAM 9
#define FD_TYPE_UDS_DGRAM 10

        template<uint32_t TYPE>
        class fd_io_operation
        {
        };

        template<>
        class fd_io_operation<FD_TYPE_UDP>
        {
        private:
            static constexpr uint32_t RECV_CNT_ONCE = 100;
        public:
            static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void* customized_data)
            {
                is_empty = false;
                struct timespec timeout{};
                int32_t result = 0;
                mmsghdr_buf_info* buf_info = (mmsghdr_buf_info*)customized_data;
               auto cur_ptr = buf_info->buf_.data();
               auto cur_cnt = iov_cnt;
               while (cur_cnt > 0) {
                   auto ret = recvmmsg(fd.get_fd(), cur_ptr, cur_cnt, 0, &timeout);
                   if (ret >= 0) {
                       result += ret;
                       cur_ptr += ret;
                       cur_cnt -= ret;
                       continue;
                   }
                   if (errno == EAGAIN || errno == EWOULDBLOCK) {
                       is_empty = true;
                       STABLE_INFRA_IF_TRUE_BREAK(result > 0);
                       return INT32_MAX;
                   }
                   STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                   return -ret;
               }
               // }
               return result;
            }

            static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void* customized_data)
            {
                is_full = false;
                struct mmsghdr send_buff[RECV_CNT_ONCE]{};
                int32_t result = 0;
                for (uint32_t cur_cnt = 0; cur_cnt <= iov_cnt; cur_cnt += RECV_CNT_ONCE) {
                    auto real_cnt = iov_cnt - cur_cnt;
                    if (real_cnt > RECV_CNT_ONCE) {
                        real_cnt = RECV_CNT_ONCE;
                    }
                    for (uint32_t i = 0; i < real_cnt; ++i) {
                        send_buff[i].msg_hdr.msg_iov = &iov[i + cur_cnt];
                        send_buff[i].msg_hdr.msg_iovlen = 1;
                        if (customized_data != nullptr) {
                            send_buff[i].msg_hdr.msg_name = (struct sockaddr*)customized_data;
                            send_buff[i].msg_hdr.msg_namelen = sizeof(struct sockaddr);
                        }
                    }
                    do {
                        auto ret = sendmmsg(fd.get_fd(), send_buff, real_cnt, 0);
                        if (ret >= 0) {
                            result += ret;
                            break;
                        }
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            is_full = true;
                            break;
                        }
                        STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                        return -ret;
                    } while(true);
                }
                return result;
            }

            static int32_t read_fd_for_multishot(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void* customized_data)
            {
                mmsghdr_buf_info* buf_info = (mmsghdr_buf_info*)customized_data;
                struct timespec timeout{};
                while (true) {
                    auto ret = recvmmsg(fd.get_fd(), buf_info->buf_.data(), buf_info->get_block_cnt(), 0, &timeout);
                    STABLE_INFRA_IF_TRUE_RETURN_CODE(ret >= 0, ret);
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        is_empty = true;
                        return INT32_MAX;
                    }
                    STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                    return -ret;
                }
            }
        };

        template<>
            class fd_io_operation<FD_TYPE_TCP>
            {
            public:
                static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void*)
                {
                    uint32_t result = 0;
                    struct msghdr msg{};
                    msg.msg_iov = iov;
                    msg.msg_iovlen = iov_cnt;
                    is_empty = false;
                    while (true) {
                        auto ret_recv = recvmsg(fd, &msg, 0);
                        if (ret_recv > 0) {
                            result += ret_recv;
                            auto ret = ezio::util::move_iov(iov, iov_cnt, ret_recv);
                            STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                            continue;
                        }
                        STABLE_INFRA_IF_TRUE_RETURN_CODE(ret_recv == 0, 0); // closed
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no data in socket buffer
                            is_empty = true;
                            break;
                        }
                        STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                        return ret_recv;   // error
                    }
                    return result;
                }

                static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void*)
                {
                    uint32_t result = 0;
                    struct msghdr msg{};
                    msg.msg_iov = iov;
                    msg.msg_iovlen = iov_cnt;
                    is_full = false;
                    while (true) {
                        auto ret_w = sendmsg(fd, &msg, 0);
                        if (ret_w >= 0) {
                            result += ret_w;
                            auto ret = ezio::util::move_iov(iov, iov_cnt, ret_w);
                            STABLE_INFRA_IF_TRUE_BREAK(ret != 0);
                            continue;
                        }
                        STABLE_INFRA_IF_TRUE_CONTINUE(ret_w == 0);
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            is_full = true; // socket buffer has no space
                            break;
                        }
                        STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                        return ret_w;
                    }
                    return result;
                }

                static int32_t read_fd_for_multishot(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void* msg)
                {
                    struct msghdr* msg_ptr = (struct msghdr*)msg;
                    msg_ptr->msg_iov = iov;
                    msg_ptr->msg_iovlen = iov_cnt;
                    is_empty = false;
                    while (true) {
                        auto ret_recv = recvmsg(fd, msg_ptr, 0);
                        STABLE_INFRA_IF_TRUE_RETURN_CODE(STABLE_INFRA_LIKELY(ret_recv > 0), ret_recv);
                        STABLE_INFRA_IF_TRUE_RETURN_CODE(ret_recv == 0, 0); // closed
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // no data in socket buffer
                            is_empty = true;
                            return INT32_MAX;
                        }
                        STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                        return ret_recv; // error
                    }
                }
            };

        template<>
            class fd_io_operation<FD_TYPE_INOTIFY>
            {
            public:
                static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void*)
                {
                    uint32_t result = 0;
                    is_empty = false;
                    while (true) {
                        auto ret_recv = readv(fd, iov, iov_cnt);
                        if (ret_recv > 0) {
                            result += ret_recv;
                            auto ret = ezio::util::move_iov(iov, iov_cnt, ret_recv);
                            if (ret != 0) {
                                break;
                            }
                            continue;
                        } else if (ret_recv == 0) {
                            break;
                        } else {
                            if (errno == EAGAIN
                                || errno == EWOULDBLOCK) {
                                // no data
                                is_empty = true;
                                break;
                            } else if (errno == EINTR) {
                                continue;
                            }
                            return ret_recv;
                        }
                    }
                    return result;
                }

                static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void*)
                {
                    return 0;
                }
            };

        template<>
            class fd_io_operation<FD_TYPE_FILE>
            {
            public:
                static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void*)
                {
                    uint32_t result = 0;
                    is_empty = false;
                    while (true) {
                        auto ret_recv = readv(fd, iov, iov_cnt);
                        if (ret_recv > 0) {
                            result += ret_recv;
                            auto ret = ezio::util::move_iov(iov, iov_cnt, ret_recv);
                            if (ret != 0) {
                                break;
                            }
                            continue;
                        } else if (ret_recv == 0) {
                            break;
                        } else {
                            STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                            return ret_recv;
                        }
                    }
                    return result;
                }

                static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void*)
                {
                    is_full = false;
                    uint32_t result = 0;
                    while (true) {
                        auto ret_w = writev(fd, iov, iov_cnt);
                        if (ret_w >= 0) {
                            result += ret_w;
                            auto ret = ezio::util::move_iov(iov, iov_cnt, ret_w);
                            if (ret != 0) {
                                break;
                            }
                            continue;
                        } else if (ret_w == 0) {
                            continue;
                        } else {
                            STABLE_INFRA_IF_TRUE_CONTINUE(errno == EINTR);
                            return ret_w;
                        }
                    }
                    return result;
                }
            };

        template<>
            class fd_io_operation<FD_TYPE_EVENT_FD>
            {
            public:
                static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void*)
                {
                    if (iov[0].iov_len < sizeof(uint64_t)) {
                        return -1;
                    }
                    is_empty = false;
                    int32_t ret_recv = 0;
                    while (ret_recv != sizeof(uint64_t)) {
                        ret_recv = readv(fd, iov, iov_cnt);
                        if (ret_recv < 0) {
                            if (errno == EAGAIN
                                || errno == EWOULDBLOCK) {
                                // no data
                                break;
                            } else if (errno == EINTR) {
                                continue;
                            } else {
                                // error
                                is_empty = true;
                                return ret_recv;
                            }
                        }
                    }
                    is_empty = true;
                    return ret_recv;
                }

                // only for writing event_fd
                static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void*)
                {
                    if (iov[0].iov_len < sizeof(uint64_t)) {
                        return -1;
                    }
                    is_full = false;
                    uint32_t ret_w = 0;
                    while (ret_w != sizeof(uint64_t)) {
                        ret_w = writev(fd, iov, iov_cnt);
                        if (ret_w < 0) {
                            if (errno == EAGAIN
                                || errno == EWOULDBLOCK) {
                                continue;
                            } else if (errno == EINTR) {
                                continue;
                            } else {
                                return ret_w;
                            }
                        }
                    }
                    return ret_w;
                }
            };

        template<>
            class fd_io_operation<FD_TYPE_SIGNAL_FD>
            {
            public:
                static int32_t read_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_empty, void*)
                {
                    if (iov[0].iov_len < sizeof(struct signalfd_siginfo)) {
                        return -1;
                    }
                    is_empty = false;
                    int32_t ret_recv = 0;
                    while (ret_recv != sizeof(struct signalfd_siginfo)) {
                        ret_recv = readv(fd, iov, iov_cnt);
                        if (ret_recv < 0) {
                            if (errno == EAGAIN
                                || errno == EWOULDBLOCK) {
                                // no data
                                break;
                            } else if (errno == EINTR) {
                                continue;
                            } else {
                                // error
                                is_empty = true;
                                return ret_recv;
                            }
                        }
                    }
                    is_empty = true;
                    return ret_recv;
                }

                // signalfd will not be writen by user
                static int32_t write_fd(fd_t fd, ::iovec* iov, uint32_t iov_cnt, bool& is_full, void*)
                {
                    return 0;
                }
            };

        template<>
            class fd_io_operation<FD_TYPE_UDS_STREAM> : public fd_io_operation<FD_TYPE_TCP>
            {
            };
        
        template<>
            class fd_io_operation<FD_TYPE_UDS_DGRAM> : public fd_io_operation<FD_TYPE_UDP>
            {
            };
    }
}
#endif
