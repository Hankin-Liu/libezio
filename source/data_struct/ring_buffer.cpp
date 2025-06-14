/****************************************************************************************
 * @file ring_buffer.cpp
 * @brief data struct for transmit data from one thread to another without lock
 * @author Liu Hua Jun
 * @email wojiaoliuhuajun@126.com
 * @license Use of this source code is governed by The GNU Affero General Public License Version 3
 *          which can be found in the LICENSE file
 ***************************************************************************************/
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "../../include/platform_define.h"
#include "../../include/data_struct/ring_buffer.h"
#include "util/util.h"

namespace ezio {
    namespace data_struct {
        ezio::data_struct::ring_buffer::unique_pointer_t ring_buffer::get_unique_ptr()
        {
#if __cplusplus >= 201703L
            return ezio::data_struct::ring_buffer::unique_pointer_t(new(std::align_val_t(ALIGN_SIZE)) ring_buffer{});
#else
            void* ptr = ezio::util::alloc_aligned_mem(sizeof(ring_buffer), ALIGN_SIZE);
            if (ptr == nullptr) {
                throw std::bad_alloc();
            }
            auto buffer = new(ptr) ezio::data_struct::ring_buffer{};
            ezio::data_struct::ring_buffer::unique_pointer_t unique_buf(buffer, ezio::data_struct::aligned_deleter());
            return unique_buf;
#endif
        }
        ezio::data_struct::ring_buffer::shared_pointer_t ring_buffer::get_shared_ptr()
        {
#if __cplusplus >= 201703L
            return std::shared_ptr<ezio::data_struct::ring_buffer>(new(std::align_val_t(ALIGN_SIZE)) ring_buffer{});
#else
            void* ptr = ezio::util::alloc_aligned_mem(sizeof(ring_buffer), ALIGN_SIZE);
            if (ptr == nullptr) {
                throw std::bad_alloc();
            }
            auto buffer = new(ptr) ezio::data_struct::ring_buffer{};
            ezio::data_struct::ring_buffer::shared_pointer_t shared_buf(buffer, ezio::data_struct::aligned_deleter());
            return shared_buf;
#endif
        }

        ring_buffer::ring_buffer()
        {
        }

        ring_buffer::~ring_buffer()
        {
            if (! data_.empty()) {
                for (auto& iov : data_) {
                    free(iov.iov_base);
                }
            }
        }

        int32_t ring_buffer::init(uint32_t block_cnt, uint32_t buffer_size, bool is_zero_mem, uint64_t align_size)
        {
            uint32_t real_align_size = align_size;
            if (! ezio::util::is_power_of_2(align_size)) {
                real_align_size = ezio::util::roundup_power_of_two(align_size);
            }
            uint32_t real_block_cnt = block_cnt;
            //uint64_t real_buffer_size = buffer_size;
            if (! ezio::util::is_power_of_2(block_cnt)) {
                real_block_cnt = ezio::util::roundup_power_of_two(block_cnt);
            }
            STABLE_INFRA_IF_TRUE_RETURN_CODE(real_block_cnt == 0 || real_align_size == 0, -1);
            data_.reserve(real_block_cnt);
            bool is_alloc_failed = false;
            for (uint32_t i = 0; i < block_cnt; ++i) {
                auto mem = (byte_t*)ezio::util::alloc_aligned_mem(buffer_size, real_align_size);
                if (mem == nullptr) {
                    is_alloc_failed = true;
                    break;
                }
                if (is_zero_mem) {
                    memset(mem, 0, buffer_size);
                }
                ::iovec iov = {
                    .iov_base = mem,
                    .iov_len = buffer_size
                };
                data_.push_back(iov);
            }
            if (is_alloc_failed) {
                for (auto& iov : data_) {
                    free(iov.iov_base);
                }
                data_.clear();
                return -1;
            }

            mask_ = real_block_cnt - 1;
            return 0;
        }

        buffer_info* ring_buffer::internal_writer_get_buffer(uint64_t read_idx)
        {
            uint64_t write_idx = writer_.idx_.load(std::memory_order_relaxed);
            STABLE_INFRA_CHECK_SUC(write_idx - read_idx < get_block_cnt(), nullptr);
            const uint64_t idx_w = write_idx & mask_;
            const uint64_t idx_r = read_idx & mask_;
            writer_.buf_info_.iov_ = &data_[idx_w];
            uint32_t iov_cnt = 0;
            if (idx_w >= idx_r) {
                iov_cnt = get_block_cnt() - idx_w;
            } else {
                iov_cnt = idx_r - idx_w;
            }
            STABLE_INFRA_CHECK_SUC(iov_cnt > 0, nullptr);
            writer_.buf_info_.iov_cnt_ = iov_cnt;
            return &writer_.buf_info_;
        }

        buffer_info* ring_buffer::writer_get_buffer(uint64_t need_cnt)
        {
            auto buf_info = internal_writer_get_buffer(writer_.reader_idx_screenshot_);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(buf_info != nullptr && buf_info->iov_cnt_ >= need_cnt, buf_info);
            // get the newest reader's idx_, take a screenshot
            writer_.reader_idx_screenshot_ = reader_.idx_.load(std::memory_order_acquire);
            return internal_writer_get_buffer(writer_.reader_idx_screenshot_);
        }

        buffer_info* ring_buffer::internal_reader_get_buffer(uint64_t write_idx, uint64_t read_idx)
        {
            STABLE_INFRA_CHECK_SUC(write_idx > read_idx, nullptr);
            uint64_t idx_w = write_idx & mask_;
            uint64_t idx_r = read_idx & mask_;
            reader_.buf_info_.iov_ = &data_[idx_r];
            uint32_t iov_cnt = 0;
            if (idx_w > idx_r) {
                iov_cnt = idx_w - idx_r;
            } else {
                iov_cnt = get_block_cnt() - idx_r;
            }
            STABLE_INFRA_CHECK_SUC(iov_cnt > 0, nullptr);
            return &reader_.buf_info_;
        }

        buffer_info* ring_buffer::reader_get_buffer(uint64_t need_cnt)
        {
            uint64_t read_idx  = reader_.idx_.load(std::memory_order_relaxed);
            auto buf_info = internal_reader_get_buffer(reader_.writer_idx_screenshot_, read_idx);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(buf_info != nullptr && buf_info->iov_cnt_ >= need_cnt, buf_info);
            // get the newest writer's idx_, take a screenshot
            reader_.writer_idx_screenshot_ = writer_.idx_.load(std::memory_order_acquire);
            return internal_reader_get_buffer(reader_.writer_idx_screenshot_, read_idx);
        }

        int32_t ring_buffer::internal_reader_get_buffer(buffer_info* buf_info, uint64_t read_idx, uint64_t writer_idx) {
            uint64_t reader_idx = reader_.idx_.load(std::memory_order_relaxed);
            STABLE_INFRA_CHECK_SUC(read_idx >= reader_idx && read_idx < writer_idx, -1);
            uint64_t idx_w = writer_idx & mask_;
            uint64_t idx_r = read_idx & mask_;
            uint32_t iov_cnt = 0;
            if (idx_w > idx_r) {
                iov_cnt = idx_w - idx_r;
            } else {
                iov_cnt = get_block_cnt() - idx_r;
            }
            STABLE_INFRA_CHECK_SUC(iov_cnt > 0, -1);
            reader_.buf_info_.iov_ = &data_[idx_r];
            reader_.buf_info_.iov_cnt_ = iov_cnt;
            buf_info = &reader_.buf_info_;
            return 0;
        }

        int32_t ring_buffer::reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx) {
            return internal_reader_get_buffer(buf_info_ptr, read_idx, writer_.idx_.load(std::memory_order_acquire));
        }

        int32_t ring_buffer::reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx, uint64_t need_cnt)
        {
            auto ret = internal_reader_get_buffer(buf_info_ptr, read_idx, reader_.writer_idx_screenshot_);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(ret == 0
                    && buf_info_ptr->iov_cnt_ >= need_cnt, 0);
            // get the newest writer's idx_, take a screenshot
            reader_.writer_idx_screenshot_ = writer_.idx_.load(std::memory_order_acquire);
            return internal_reader_get_buffer(buf_info_ptr, read_idx, reader_.writer_idx_screenshot_);
        }

        int32_t ring_buffer::reader_get_buffer(buffer_info* buf_info_ptr, uint64_t read_idx, uint64_t& writer_idx_screenshot, uint64_t need_cnt)
        {
            auto ret = internal_reader_get_buffer(buf_info_ptr, read_idx, writer_idx_screenshot);
            STABLE_INFRA_IF_TRUE_RETURN_CODE(ret == 0
                    && buf_info_ptr->iov_cnt_ >= need_cnt, 0);
            // get the newest writer's idx_, take a screenshot
            writer_idx_screenshot = writer_.idx_.load(std::memory_order_acquire);
            return internal_reader_get_buffer(buf_info_ptr, read_idx, writer_idx_screenshot);
        }

        uint64_t ring_buffer::reader_get_unread_cnt()
        {
            auto write_idx = writer_.idx_.load(std::memory_order_acquire);
            auto read_idx  = reader_.idx_.load(std::memory_order_relaxed);
            reader_.writer_idx_screenshot_ = write_idx;
            return write_idx - read_idx;
        }
    }
}
