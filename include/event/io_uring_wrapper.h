/****************************************************************************************
 * @file io_uring_wrapper.h
 * @brief encapsulation of iouring
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include "../platform_define.h"
#ifdef IOURING_IS_SUPPORTED
#include <sys/mman.h>
#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdint>

#define PREPARE_SQE(sqe, operation_code, real_fd, address, length, offset, flag) \
        sqe->opcode = operation_code; \
        sqe->fd = real_fd; \
        sqe->addr = (uint64_t)address; \
        sqe->len = length; \
        sqe->off = offset; \
        sqe->flags = flag;

/**
 * @brief stable_infra namespace
 */
namespace ezio {
    /**
     * @brief event namespace
     * All event driven codes are in this namespace
     */
    namespace event {
        static inline int io_uring_setup_local(unsigned entries, struct io_uring_params *params) {
            return syscall(SYS_io_uring_setup, entries, params);
        }
        static inline int io_uring_enter_local(int fd, unsigned to_submit,unsigned min_complete, unsigned flags,
                                         void *arg = nullptr, size_t argsz = 0) {
            return syscall(SYS_io_uring_enter, fd, to_submit, min_complete, flags, arg, argsz);
        }
        static inline int io_uring_register_local(int fd, unsigned opcode, const void *arg, unsigned nr_args) {
            return syscall(SYS_io_uring_register, fd, opcode, arg, nr_args);
        }

        struct submission_queue
        {
            uint32_t* khead{};
            uint32_t* ktail{};
            uint32_t* kring_mask{};
            uint32_t* kring_entries{};
            uint32_t* array{};
            uint32_t* kflags{};
            uint32_t* kdropped{};
            struct io_uring_sqe* sqes{};
            uint32_t sqe_head{};
            uint32_t sqe_tail{};
            size_t ring_sz{};
            void* ring_ptr{};
            uint32_t ring_mask{};
            uint32_t ring_entries{};
            uint32_t pad[2]{};
        };
        struct completion_queue
        {
            uint32_t* khead{};
            uint32_t* ktail{};
            uint32_t* kring_mask{};
            uint32_t* kring_entries{};
            struct io_uring_cqe* cqes{};
            uint32_t* kflags{};
            uint32_t* koverflow{};
            size_t ring_sz{};
            void* ring_ptr{};
            uint32_t ring_mask{};
            uint32_t ring_entries{};
            uint32_t pad[2]{};
        };

        class io_uring_wrapper
        {
            constexpr io_uring_wrapper(io_uring_wrapper&) = delete;
            io_uring_wrapper& operator=(io_uring_wrapper&) = delete;
        public:
            io_uring_wrapper() = default;
            ~io_uring_wrapper();
        public:
            int32_t init(uint32_t entry_cnt, const io_uring_params& param);
            void prepare_readv(struct io_uring_sqe *sqe, int32_t fd,
                               const struct ::iovec *iovecs, unsigned nr_vecs,
                               uint64_t offset);
            struct ::io_uring_sqe* get_sqe();
            void rollback_sqe();
            inline void set_flag(struct ::io_uring_sqe* sqe, uint8_t flag) {
                sqe->flags = flag;
            }
            inline void set_user_data(struct ::io_uring_sqe* sqe, uint64_t user_data) {
                sqe->user_data = user_data;
            }
            inline void set_buf_group(struct ::io_uring_sqe* sqe, uint32_t buf_group_id) {
                sqe->buf_group = buf_group_id;
            }
            int32_t submit();
            int32_t wait_cqe_timeout(struct __kernel_timespec* ts,
                                     uint32_t min_event_count);
            void cq_consume(uint32_t count);
            void prepare_timeout(struct io_uring_sqe *sqe,
                                 const struct __kernel_timespec* ts,
                                 uint32_t count, uint32_t flags);
            void prepare_cancel(struct io_uring_sqe* sqe, void* user_data,
                    uint32_t flags);
            void prepare_cancel_by_fd(struct io_uring_sqe* sqe, void* user_data,
                    int32_t fd);
            void prepare_recv_multishot(struct io_uring_sqe* sqe,
                    int32_t fd, struct msghdr* msghdr_ptr,
                    uint32_t flags, uint16_t buf_group_id);
            void prepare_recvmsg_multishot(struct io_uring_sqe* sqe,
                    int32_t fd, struct msghdr* msghdr_ptr,
                    uint32_t flags, uint16_t buf_group_id);
            void prepare_recvmsg(struct io_uring_sqe* sqe, int32_t fd,
                    struct msghdr* msghdr_ptr, uint32_t flags);
            void prepare_poll(struct io_uring_sqe* sqe, int32_t fd, uint16_t events);
            void prepare_accept_multishot(struct io_uring_sqe* sqe, int32_t listen_fd,
                                          struct sockaddr* addr, socklen_t* addrlen,
                                          uint32_t flags);
            void prepare_sendmsg(struct io_uring_sqe* sqe, int32_t fd,
                                 struct msghdr* msg, uint32_t flags);
            void prepare_writev(struct io_uring_sqe* sqe, int32_t fd,
                                const struct iovec* iov, uint32_t iovcnt,
                                uint64_t offset);
            void prepare_read_multishot(struct io_uring_sqe* sqe, int32_t fd, uint32_t bytes,
                                        uint64_t offset, uint32_t buffer_group);
            void add_buf_ring(struct io_uring_buf_ring* br,
                              void* addr, uint32_t block_size, uint32_t bid,
                              uint32_t mask, uint32_t idx);
            void buf_ring_advance(struct io_uring_buf_ring* br, unsigned block_count, unsigned mask);
            struct io_uring_buf_ring* setup_buf_ring(uint32_t nentries, uint16_t buf_group_id, uint16_t flags, int32_t& ret);
            int32_t free_buf_ring(struct io_uring_buf_ring* br, uint32_t nentries, uint16_t bgid);
            inline uint32_t buf_ring_mask(uint32_t block_count)
            {
                return block_count - 1;
            }
            inline struct io_uring_cqe* get_cqe(uint32_t idx)
            {
                return &ring_cq_.cqes[((*ring_cq_.khead + idx) & *ring_cq_.kring_mask) << shift_];
            }
        private:
            int32_t mmap_rings();
            bool need_wakeup_kernel();
            void clean_rings();
            void assign_ring_address();
        private:
            struct io_uring_params param_{};
            uint32_t shift_{ 0 };
            int32_t io_uring_fd_{ -1 };
            submission_queue ring_sq_{};
            completion_queue ring_cq_{};
            unsigned flags_{};
            unsigned features_{};
            uint8_t int_flags_{};
        };
    }
}
#endif
