/****************************************************************************************
 * @file ring_buffer.h
 * @brief data struct for transmit data from one thread to another without lock
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#pragma once
#include <stdint.h>
#include <atomic>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdlib>
#include "../platform_define.h"
#include "../common/const_variable.h"
#include "../util/macros_func.h"
#include "../type_def.h"

#define ALIGN_SIZE 64
#define DEFAULT_ALIGN_SIZE 1024
namespace ezio {
    namespace data_struct {
        struct buffer_info
        {
            ::iovec* iov_{ nullptr };
            uint32_t iov_cnt_{ 0 };
        };

#if __cplusplus < 201703L
        struct aligned_deleter {
            void operator()(void* ptr) const {
                std::free(ptr);
            }
        };
#endif

        class alignas(ALIGN_SIZE) ring_buffer final
        {
            constexpr ring_buffer(ring_buffer&) = delete;
            ring_buffer& operator=(ring_buffer&) = delete;

            struct reader_info
            {
                reader_info() {
                }
                ~reader_info() {
                }
                std::atomic<uint64_t> idx_{ 0 }; ///< reader bytes
                uint64_t writer_idx_screenshot_{ 0 }; ///< screenshot for writer index
                buffer_info buf_info_{};
            };
            struct writer_info
            {
                writer_info() {
                }
                ~writer_info() {
                }
                std::atomic<uint64_t> idx_{ 0 }; ///< writer bytes
                uint64_t reader_idx_screenshot_{ 0 }; ///< screen shot for read index
                buffer_info buf_info_{};
            };
            public:
                using shared_pointer_t = std::shared_ptr<ezio::data_struct::ring_buffer>;
#if __cplusplus < 201703L
                using unique_pointer_t = std::unique_ptr<ezio::data_struct::ring_buffer, aligned_deleter>;
#else
                using unique_pointer_t = std::unique_ptr<ezio::data_struct::ring_buffer>;
#endif
            public:
                template<typename T>
                int32_t put(const T& data)
                {
                    STABLE_INFRA_CHECK_SUC(sizeof(T) <= get_block_size(), -1);
                    auto buf_ptr = writer_get_buffer(1);
                    STABLE_INFRA_CHECK_SUC(buf_ptr != nullptr, -2);
                    memcpy(buf_ptr->iov_[0].iov_base, &data, sizeof(T));
                    writer_commit(1);
                    return 0;
                }
            public:
                static ezio::data_struct::ring_buffer::unique_pointer_t get_unique_ptr();
                static ezio::data_struct::ring_buffer::shared_pointer_t get_shared_ptr();
            public:
                ring_buffer();
                ~ring_buffer();

                /** init function for ring buffer
                 * @param block_cnt buffer count in this ring buffer
                 * @param block_size buffer size for each block in this ring buffer
                 * @param align_size align size of buffer
                 * @param is_zero_mem if need to memset 0 for the buffer
                 * @return 0 - success, otherwise failed
                 */
                int32_t init(uint32_t block_cnt, uint32_t block_size, bool is_zero_mem = false, uint64_t align_size = DEFAULT_ALIGN_SIZE);

                /** get the position for writing
                 * @return writer pointer and buffer length
                 */
                inline buffer_info* writer_get_buffer() {
                    return internal_writer_get_buffer(reader_.idx_.load(std::memory_order_acquire));
                }

                /** get the position for writing
                 * @param need_cnt, buffer count needed
                 * @return data pointer for writing
                 */
                buffer_info* writer_get_buffer(uint64_t need_cnt);

                /** increase the writer index 
                 * @param done_cnt data count which has writen in ring buffer
                 */
                inline void writer_commit(uint64_t done_cnt)
                {
                    writer_.idx_.store(writer_.idx_.load(std::memory_order_relaxed) + done_cnt, std::memory_order_release);
                }

                /** get the position for reading
                 * @return reader pointer and buffer length
                 */
                inline buffer_info* reader_get_buffer()
                {
                    return internal_reader_get_buffer(writer_.idx_.load(std::memory_order_acquire), reader_.idx_.load(std::memory_order_relaxed));
                }

                /** get the position for reading
                 * @param need_cnt, data count needed
                 * @return data pointer for reading
                 */
                buffer_info* reader_get_buffer(uint64_t need_cnt);

                /** increase the reader idx 
                 * @param done_cnt data count which has read from ring buffer
                 */
                inline void reader_commit(uint64_t done_cnt)
                {
                    reader_.idx_.store(reader_.idx_.load(std::memory_order_relaxed) + done_cnt, std::memory_order_release);
                }

                /** get the position for reading
                 * @param buf_info_ptr used to store data pointer
                 * @param read_idx get data from this position
                 * @return RET_SUC success
                 * @return RET_ERR fail
                 */
                int32_t reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx);

                /** get the position for reading
                 * @param buf_info_ptr used to store data pointer
                 * @param read_idx get data from this position
                 * @param need_cnt buffer count which want get
                 * @return RET_SUC success
                 * @return RET_ERR fail
                 */
                int32_t reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx, uint64_t need_cnt);
                int32_t reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx, uint64_t& writer_idx_screenshot, uint64_t need_cnt);
        
                /** get the reader index
                 * @return reader index
                 */
                inline uint64_t reader_get_reader_idx() const {
                    return reader_.idx_.load(std::memory_order_relaxed);
                }

                inline uint64_t writer_get_reader_idx() const {
                    return reader_.idx_.load(std::memory_order_acquire);
                }

                /** get the writer index
                 * @return writer index
                 */
                inline uint64_t writer_get_writer_idx() const {
                    return writer_.idx_.load(std::memory_order_relaxed);
                }

                inline uint64_t reader_get_writer_idx() const {
                    return writer_.idx_.load(std::memory_order_acquire);
                }

                /** reset the ring buffer
                 */
                inline void reset() {
                    writer_.idx_ = writer_.reader_idx_screenshot_ = reader_.idx_ = reader_.writer_idx_screenshot_ = 0; 
                }

                /** ignore the elements which are unread
                 */
                inline void reader_reset_out() { 
                    auto write_idx = writer_.idx_.load(std::memory_order_relaxed); 
                    reader_.idx_.store(write_idx, std::memory_order_release);
                    reader_.writer_idx_screenshot_ = write_idx;
                }

                /** get the unread element count
                 * @return unread element count
                 */
                inline uint64_t reader_get_unread_cnt();

                inline uint32_t get_block_cnt() const {
                    return data_.size();
                }

                inline uint32_t get_block_size() const {
                    return data_[0].iov_len;
                }

                /**
                 * reader's interface
                 */
                inline bool empty()
                {
                    return reader_get_buffer() == nullptr;
                }
            private:
                buffer_info* internal_writer_get_buffer(uint64_t read_idx);
                buffer_info* internal_reader_get_buffer(uint64_t write_idx, uint64_t read_idx);
                int32_t internal_reader_get_buffer(buffer_info* iov, uint64_t read_idx, uint64_t writer_idx);
            private:
                writer_info writer_;
                byte_t PADDING_IN_[ALIGN_SIZE - sizeof(writer_info)];
                reader_info reader_;
                byte_t PADDING_OUT_[ALIGN_SIZE - sizeof(reader_info)];
                std::vector<::iovec> data_{};  ///< the buffer holding the data
                uint64_t mask_{ 0 };
                byte_t PADDING_COMM_[ALIGN_SIZE - sizeof(uint64_t) - sizeof(data_)];
        };
    }
}
