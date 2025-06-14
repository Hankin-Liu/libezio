/**
 * @file poll_base.h
 * @brief base class or variables of io multiplexing mechanism
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 */
#pragma once
#include <stdint.h>
#include <functional>
#include "../platform_define.h"
#include "../type_def.h"

/**
 * @brief stable_infra function namespace
 */
namespace ezio {
    /**
     * @brief event namespace
     * All event driven codes are in this namespace
     */
    namespace event {
        
        /**
         * @brief interface class for io multiplexing mechanism
         * Use pure virtual functions to define interfaces
         */
        class poll_base
        {
            public:
                virtual ~poll_base() = default;
                /**
                 * @brief init interface
                 * Initialization of io multiplexing mechanism
                 * @return result of Initialization
                 * @retval true successful
                 * @retval false failed
                 */
                virtual bool init(uint32_t entry_cnt) = 0;
                
                /**
                 * @brief register ring buffer
                 * @param[in] block_size bytes of one block
                 * @param[in] block_count block count of this ring buffer
                 * @return result of register
                 * @retval >= 0 buffer group id
                 * @retval -1 failed
                 */
                virtual int32_t register_ring_buffer(uint32_t block_size, uint32_t block_count) = 0;

                /**
                 * @brief remove ring buffer
                 * @param[in] buf_group_id, buffer group id which is returned from register_ring_buffer interface
                 * @return result of unregister
                 * @retval 0 success
                 * @retval -1 failed
                 */
                virtual int32_t unregister_ring_buffer(uint16_t buf_group_id) = 0;

                virtual int32_t submit_async_read(const fd_t& fd,
                                                  uint32_t buffer_index,
                                                  const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                                  const read_options* opt = nullptr) = 0;
                virtual int32_t submit_async_read(const fd_t& fd,
                                                  ::iovec* buffer,
                                                  uint32_t buffer_iov_cnt,
                                                  const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb = nullptr,
                                                  const read_options* opt = nullptr) = 0;
                virtual int32_t cancel_async_read(const fd_t& fd, const std::function<void(int32_t)>& cb = nullptr) = 0;

                virtual int32_t submit_async_accept(const fd_t& listen_fd, sock_info* addr_buffer, const std::function<void(int32_t, const sock_info&)>& cb = nullptr) = 0;
                virtual int32_t cancel_async_accept(const fd_t& listen_fd, const std::function<void(int32_t)>& cb) = 0;

                virtual int32_t submit_async_write(const fd_t& fd,
                                                   ::iovec* buffer,
                                                   uint32_t buffer_iov_cnt,
                                                   const std::function<void(int32_t)>& cb = nullptr,
                                                   const write_options* opt = nullptr) = 0;
                virtual int32_t cancel_async_write(const fd_t& fd, const std::function<void(int32_t)>& cb) = 0;

                virtual int32_t create_timer(uint64_t interval_s, uint64_t interval_ns, const std::function<void(uint64_t)>& cb) = 0;

                virtual int32_t close_timer(int32_t timer_id) = 0;

                virtual int32_t submit() = 0;

                /**
                 * @brief Dispatch event interface
                 * Dispatch events and invoke callback functions
                 * @return result of dispatching
                 * @retval 0 successful
                 * @retval -1 failed
                 */
                virtual int32_t dispatch(int32_t timeout) = 0;
                /**
                 * @brief Close interface
                 * Close this io multiplexing mechanism
                 */
                virtual void close() = 0;
        };
    }
}
