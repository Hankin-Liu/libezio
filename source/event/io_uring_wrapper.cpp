/**
 * @file io_uring_wrapper.cpp
 * @brief encapsulation of io_uring
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#include "../../include/platform_define.h"
#ifdef IOURING_IS_SUPPORTED
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <functional>
#include <vector>
#include <memory>
#include "../../include/event/io_uring_wrapper.h"
#include "../../include/event/complete_action.h"
#include "../../include/util/macros_func.h"
#include "../../include/util/util.h"
#include "../../include/util/barrier.h"

namespace ezio {
    namespace event {
        static void initialize_sqe(struct io_uring_sqe *sqe)
        {
            sqe->flags = 0;
            sqe->ioprio = 0;
            sqe->rw_flags = 0;
            sqe->buf_index = 0;
            sqe->personality = 0;
            sqe->file_index = 0;
            sqe->addr3 = 0;
            //sqe->__pad2[0] = 0;
            sqe->buf_group = 0;
        }

        io_uring_wrapper::~io_uring_wrapper()
        {
            clean_rings();
            if (io_uring_fd_ > 0) {
                ::close(io_uring_fd_);
            }
        }

        int32_t io_uring_wrapper::init(uint32_t entry_cnt, const io_uring_params& param)
        {
            STABLE_INFRA_CHECK_SUC(entry_cnt > 0, -1);
            param_ = param;
            io_uring_fd_ = io_uring_setup_local(entry_cnt, &param_);
            STABLE_INFRA_CHECK_SUC(io_uring_fd_ > 0, -1);
            auto ret = mmap_rings();
            STABLE_INFRA_CHECK_SUC(ret == 0, -1);
            if (param_.flags & IORING_SETUP_CQE32) {
                shift_ = 1;
            }
            return 0;
        }
        
        void io_uring_wrapper::clean_rings()
        {
            if (ring_sq_.ring_ptr != MAP_FAILED) {
                munmap(ring_sq_.ring_ptr, ring_sq_.ring_sz);
                ring_sq_.ring_ptr = MAP_FAILED;
            }
            if (ring_cq_.ring_ptr != MAP_FAILED) {
                if (! (param_.features & IORING_FEAT_SINGLE_MMAP)) {
                    munmap(ring_cq_.ring_ptr, ring_cq_.ring_sz);
                }
                ring_cq_.ring_ptr = MAP_FAILED;
            }
            if (ring_sq_.sqes != MAP_FAILED) {
                auto size = sizeof(struct io_uring_sqe);
                if (param_.flags & IORING_SETUP_SQE128) {
                    size += 64;
                }
                munmap(ring_sq_.sqes, size * param_.sq_entries);
                ring_sq_.sqes = (struct io_uring_sqe*)MAP_FAILED;
            }
        }

        int32_t io_uring_wrapper::mmap_rings()
        {
            size_t size = sizeof(struct io_uring_cqe);
            if (param_.flags & IORING_SETUP_CQE32)
                size += sizeof(struct io_uring_cqe);

            ring_sq_.ring_sz = param_.sq_off.array + param_.sq_entries * sizeof(unsigned);
            ring_cq_.ring_sz = param_.cq_off.cqes + param_.cq_entries * size;

            if (param_.features & IORING_FEAT_SINGLE_MMAP) {
                if (ring_cq_.ring_sz > ring_sq_.ring_sz) {
                    ring_sq_.ring_sz = ring_cq_.ring_sz;
                }
                ring_cq_.ring_sz = ring_sq_.ring_sz;
            }
            ring_sq_.ring_ptr = ::mmap(0, ring_sq_.ring_sz, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, io_uring_fd_,
                    IORING_OFF_SQ_RING);
            STABLE_INFRA_CHECK_SUC(ring_sq_.ring_ptr != MAP_FAILED, -1);

            if (param_.features & IORING_FEAT_SINGLE_MMAP) {
                ring_cq_.ring_ptr = ring_sq_.ring_ptr;
            } else {
                ring_cq_.ring_ptr = ::mmap(0, ring_cq_.ring_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, io_uring_fd_,
                        IORING_OFF_CQ_RING);
                if (ring_cq_.ring_ptr == MAP_FAILED) {
                    clean_rings();
                    return -1;
                }
            }
         
            size = sizeof(struct io_uring_sqe);
            if (param_.flags & IORING_SETUP_SQE128) {
                size += 64;
            }
            ring_sq_.sqes = (io_uring_sqe *)::mmap(0, size * param_.sq_entries, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, io_uring_fd_, IORING_OFF_SQES);
            if (ring_sq_.sqes == MAP_FAILED) {
                clean_rings();
                return -1;
            }

            assign_ring_address();
            return 0;
        }

        void io_uring_wrapper::assign_ring_address()
        {
            ring_sq_.khead = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.head);
            ring_sq_.ktail = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.tail);
            ring_sq_.kring_mask = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.ring_mask);
            ring_sq_.kring_entries = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.ring_entries);
            ring_sq_.kflags = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.flags);
            ring_sq_.kdropped = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.dropped);
            if (!(param_.flags & IORING_SETUP_NO_SQARRAY)) {
                ring_sq_.array = (uint32_t *)((char *)ring_sq_.ring_ptr + param_.sq_off.array);
            }

            ring_cq_.khead = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.head);
            ring_cq_.ktail = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.tail);
            ring_cq_.kring_mask = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.ring_mask);
            ring_cq_.kring_entries = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.ring_entries);
            ring_cq_.koverflow = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.overflow);
            ring_cq_.cqes = (io_uring_cqe *)((char *)ring_cq_.ring_ptr + param_.cq_off.cqes);
            if (param_.cq_off.flags) {
                ring_cq_.kflags = (uint32_t *)((char *)ring_cq_.ring_ptr + param_.cq_off.flags);
            }

            ring_sq_.ring_mask = *ring_sq_.kring_mask;
            ring_sq_.ring_entries = *ring_sq_.kring_entries;
            ring_cq_.ring_mask = *ring_cq_.kring_mask;
            ring_cq_.ring_entries = *ring_cq_.kring_entries;
        }

        void io_uring_wrapper::prepare_readv(struct io_uring_sqe *sqe, int32_t fd,
                const struct ::iovec *iovecs, unsigned nr_vecs,
                uint64_t offset)
        {
            PREPARE_SQE(sqe, IORING_OP_READV, fd, iovecs, nr_vecs, offset, 0);
        }

        io_uring_sqe* io_uring_wrapper::get_sqe()
        {
            const uint32_t next = ring_sq_.sqe_tail + 1;
            unsigned int head{};
            uint32_t shift = (param_.flags & IORING_SETUP_SQE128) ? 1 : 0;
            if (!(param_.flags & IORING_SETUP_SQPOLL)) {
                head = *ring_sq_.khead;
            } else {
                head = io_uring_smp_load_acquire(ring_sq_.khead);
            }
            if (next - head <= *ring_sq_.kring_entries) {
                auto sqe = &ring_sq_.sqes[(ring_sq_.sqe_tail & *ring_sq_.kring_mask) << shift];
                ring_sq_.sqe_tail = next;
                initialize_sqe(sqe);
                return sqe; 
            }
            return nullptr;
        }

        int32_t io_uring_wrapper::submit()
        {
            uint32_t to_submit = ring_sq_.sqe_tail - *ring_sq_.ktail;  // 计算待提交的数量
            STABLE_INFRA_CHECK_SUC(to_submit > 0, 0);
            io_uring_smp_store_release(ring_sq_.ktail, ring_sq_.sqe_tail);
            uint32_t flags = (need_wakeup_kernel()) ? IORING_ENTER_SQ_WAKEUP : 0;
            auto ret = io_uring_enter_local(io_uring_fd_, to_submit, 0, flags);
            return (ret < 0) ? -errno : ret; // 返回提交的请求数，或错误码
        }

        bool io_uring_wrapper::need_wakeup_kernel()
        {
            STABLE_INFRA_IF_TRUE_RETURN_CODE(*ring_sq_.kflags & IORING_SQ_NEED_WAKEUP, true);
            return false;
        }

        void io_uring_wrapper::rollback_sqe()
        {
            STABLE_INFRA_IF_TRUE_RETURN(STABLE_INFRA_UNLIKELY(ring_sq_.sqe_tail <= *ring_sq_.ktail));
            --ring_sq_.sqe_tail; // 回滚未提交的 tail
        }

        int32_t io_uring_wrapper::wait_cqe_timeout(struct __kernel_timespec *ts,
                                                   uint32_t min_event_count)
        {
            struct io_uring_getevents_arg ge_arg = {
                .sigmask = 0,           // 不传信号掩码
                .sigmask_sz = _NSIG/8,        // 信号掩码大小
                .ts = (uint64_t)ts // 传入 `__kernel_timespec*`
            };
            auto ret = io_uring_enter_local(io_uring_fd_, 0, min_event_count, IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG, &ge_arg, sizeof(ge_arg));
            STABLE_INFRA_CHECK_SUC(ret >= 0, -errno);
            // 确保 `ktail` 读取的值是最新的，防止 CPU 乱序优化
            auto ktail_snapshot = io_uring_smp_load_acquire(ring_cq_.ktail);
            uint32_t ready = ktail_snapshot - *ring_cq_.khead; // 计算有多少个 CQE 已就绪
            STABLE_INFRA_IF_TRUE_RETURN_CODE(ready == 0, -ETIME);
            return ready; // 返回就绪的 CQE 数量
        }

        void io_uring_wrapper::cq_consume(uint32_t count)
        {
            // 确保获取到最新的 tail，防止读取旧值
            const auto tail = io_uring_smp_load_acquire(ring_cq_.ktail);
            const uint32_t head = *ring_cq_.khead;
            // 确保不会消费超过可用的 CQE 数量
            if (STABLE_INFRA_UNLIKELY(head + count > tail)) {
                count = tail - head;  // 只消费剩余的 CQE
            }
            // 确保在更新 khead_ 之前，所有的 CQE 数据都已经被正确读取
            io_uring_smp_store_release(ring_cq_.khead, head + count);
        }

        void io_uring_wrapper::prepare_timeout(struct io_uring_sqe *sqe,
                                               const struct __kernel_timespec* ts,
                                               uint32_t count, uint32_t flags)
        {
            PREPARE_SQE(sqe, IORING_OP_TIMEOUT, -1, ts, count, 0, 0);
            sqe->timeout_flags = flags;
        }

        void io_uring_wrapper::prepare_cancel(struct io_uring_sqe* sqe, void* user_data,
                                              uint32_t flags)
        {
            PREPARE_SQE(sqe, IORING_OP_ASYNC_CANCEL, -1, user_data, 0, 0, 0);
            sqe->cancel_flags = flags; // 将 flags 赋值给 cancel_flags 字段
        }

        void io_uring_wrapper::prepare_recv_multishot(struct io_uring_sqe* sqe,
                int32_t fd, struct msghdr* msghdr_ptr, uint32_t flags,
                uint16_t buf_group_id)
        {
            flags |= IOSQE_BUFFER_SELECT;
            PREPARE_SQE(sqe, IORING_OP_RECV, fd, msghdr_ptr, 0, 0, flags);
            sqe->ioprio |= IORING_RECV_MULTISHOT;
            set_buf_group(sqe, buf_group_id);
        }

        void io_uring_wrapper::prepare_recvmsg_multishot(struct io_uring_sqe* sqe,
                int32_t fd, struct msghdr* msghdr_ptr, uint32_t flags,
                uint16_t buf_group_id)
        {
            flags |= IOSQE_BUFFER_SELECT;
            PREPARE_SQE(sqe, IORING_OP_RECVMSG, fd, msghdr_ptr, 0, 0, flags);
            sqe->ioprio |= IORING_RECV_MULTISHOT;
            set_buf_group(sqe, buf_group_id);
        }

        void io_uring_wrapper::prepare_recvmsg(struct io_uring_sqe* sqe, int32_t fd,
                                               struct msghdr* msghdr_ptr, uint32_t flags)
        {
            PREPARE_SQE(sqe, IORING_OP_RECVMSG, fd, msghdr_ptr, 0, 0, flags);
        }

        void io_uring_wrapper::prepare_poll(struct io_uring_sqe* sqe, int32_t fd, uint16_t events)
        {
            PREPARE_SQE(sqe, IORING_OP_POLL_ADD, fd, 0, 0, 0, 0);
            sqe->poll32_events = events; // 设置要检查的事件类型, TODO 大小端字节序
        }

        void io_uring_wrapper::prepare_accept_multishot(struct io_uring_sqe* sqe, int32_t listen_fd,
                                                        struct sockaddr* addr, socklen_t* addrlen,
                                                        uint32_t flags)
        {
            PREPARE_SQE(sqe, IORING_OP_ACCEPT, listen_fd, addr, 0, (uint64_t)addrlen, flags);
            sqe->ioprio |= IORING_ACCEPT_MULTISHOT; // 使用 multishot 以便多次接受连接
        }

        void io_uring_wrapper::prepare_sendmsg(struct io_uring_sqe* sqe, int32_t fd,
                                               struct msghdr* msg, uint32_t flags)
        {
            PREPARE_SQE(sqe, IORING_OP_SENDMSG, fd, msg, 1, 0, 0);
            sqe->msg_flags = flags;
        }

        void io_uring_wrapper::prepare_writev(struct io_uring_sqe* sqe, int32_t fd,
                                              const struct iovec* iov, uint32_t iovcnt,
                                              uint64_t offset)
        {
            PREPARE_SQE(sqe, IORING_OP_WRITEV, fd, iov, iovcnt, offset, 0);
        }

        void io_uring_wrapper::prepare_read_multishot(struct io_uring_sqe* sqe, int32_t fd, uint32_t bytes,
                                                      uint64_t offset, uint32_t buffer_group)
        {
            auto flags = sqe->flags |= IOSQE_BUFFER_SELECT;
            PREPARE_SQE(sqe, IORING_OP_READ, fd, 0, bytes, offset, flags);
            sqe->buf_group = buffer_group;      // 绑定 `buffer group ID`
        }

        void io_uring_wrapper::add_buf_ring(struct io_uring_buf_ring* br,
                                            void* addr, uint32_t block_size, uint32_t bid,
                                            uint32_t mask, uint32_t idx)
        {
            struct io_uring_buf* buf = &br->bufs[(br->tail + idx) & mask]; // 确保 `idx` 在 `mask` 限制范围内
            buf->addr = (uint64_t)addr;  // 设置缓冲区地址
            buf->len = block_size;                // 设置缓冲区大小
            buf->bid = bid;                        // 设置 `buffer group ID`
        }

        void io_uring_wrapper::buf_ring_advance(struct io_uring_buf_ring* br, unsigned block_count, unsigned mask)
        {
            unsigned short new_tail = br->tail + block_count;
            io_uring_smp_store_release(&br->tail, new_tail);
        }

        int32_t io_uring_wrapper::free_buf_ring(struct io_uring_buf_ring* br, uint32_t nentries, uint16_t bgid)
        {
            struct io_uring_buf_reg reg = { .bgid = bgid };
            // 解除缓冲区组 bgid
            auto ret = io_uring_register_local(io_uring_fd_, IORING_UNREGISTER_PBUF_RING, &reg, 1);
            STABLE_INFRA_CHECK_SUC(ret == 0, -errno);
            // 释放缓冲区环的内存
            uint64_t map_size = nentries * sizeof(struct io_uring_buf);
            auto ret_unmap = munmap(br, map_size);
            STABLE_INFRA_CHECK_SUC(ret_unmap  == 0, -errno);
            return 0;
        }

        struct io_uring_buf_ring* io_uring_wrapper::setup_buf_ring(uint32_t nentries, uint16_t buf_group_id, uint16_t flags, int32_t& ret)
        {
            // 通过 io_uring_register 注册 buf_ring
            struct io_uring_buf_reg reg = {
                .ring_entries = nentries,
                .bgid = buf_group_id,
                .flags = IOU_PBUF_RING_MMAP
            };
            ret = io_uring_register_local(io_uring_fd_, IORING_REGISTER_PBUF_RING, &reg, 1);
            if (ret != 0) {
                ret = -errno;
                return nullptr;
            }

            uint64_t map_size = nentries * sizeof(struct io_uring_buf);
            uint64_t offset = IORING_OFF_PBUF_RING | (uint64_t)buf_group_id << IORING_OFF_PBUF_SHIFT;
            auto br = (struct io_uring_buf_ring*)::mmap(NULL, map_size,
                                                        PROT_READ | PROT_WRITE,
                                                        MAP_SHARED | MAP_POPULATE,
                                                        io_uring_fd_, offset);
            if (br == MAP_FAILED) {
                ret = -errno; // 记录错误码
                return nullptr;
            }
            return br;
        }
    }
}
#endif
