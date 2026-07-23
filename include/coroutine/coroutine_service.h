/****************************************************************************************
 * @file coroutine_service.h
 * @brief Coroutine-friendly wrapper around ezio::event::event_service
 *
 * Provides co_await-able wrappers for all event_service async operations.
 * Underneath, it still uses libezio's callback-based APIs.
 *
 * This header and all its types are available only when
 * EZIO_ENABLE_COROUTINE == 1 (C++20 compiler with <coroutine>).
 *
 * Usage Pattern:
 *   auto ar = cs.read(fd, &iov, 1);
 *   int32_t bytes = co_await *ar;
 *
 * All async_* / awaitable objects are returned as std::unique_ptr to
 * ensure the callback's captured "this" pointer remains valid after move.
 ***************************************************************************************/
#pragma once

#include "awaitable.h"
#include "../event_service.h"
#include "../type_def.h"

#if EZIO_ENABLE_COROUTINE

#include <memory>
#include <list>
#include <vector>
#include <atomic>
#include "../data_struct/opt_map.h"

// Default frame pool configuration
static constexpr size_t DEFAULT_FRAME_BLOCK_SIZE = 8192;
static constexpr size_t DEFAULT_FRAME_POOL_CAPACITY = 4096;

namespace ezio {
namespace coroutine {

// ============================================================================
// coroutine_frame_pool — fixed-size block pool, 0 heap alloc after ctor
// ============================================================================

class coroutine_frame_pool {
public:
    coroutine_frame_pool(size_t block_size, size_t capacity)
        : block_size_(block_size)
        , capacity_(capacity)
    {
        size_t total = block_size_ * capacity_;
        mem_ = static_cast<char*>(::operator new(total));
        // Build LIFO free list using indexes stored in next_free_[].
        // We avoid storing free-list pointers inside the blocks themselves
        // because the blocks may already contain constructed objects.
        free_head_ = 0;
        for (size_t i = 0; i < capacity_; ++i) {
            next_free_[i] = i + 1;
        }
        next_free_[capacity_ - 1] = static_cast<size_t>(-1);
    }

    ~coroutine_frame_pool() {
        ::operator delete(mem_);
    }

    coroutine_frame_pool(const coroutine_frame_pool&) = delete;
    coroutine_frame_pool& operator=(const coroutine_frame_pool&) = delete;
    coroutine_frame_pool(coroutine_frame_pool&&) = delete;
    coroutine_frame_pool& operator=(coroutine_frame_pool&&) = delete;

    size_t block_size() const { return block_size_; }

    bool owns(void* ptr) const {
        return ptr >= mem_ && ptr < mem_ + block_size_ * capacity_;
    }

    void* alloc() {
        if (free_head_ == static_cast<size_t>(-1)) return nullptr;
        size_t idx = free_head_;
        free_head_ = next_free_[idx];
        return mem_ + idx * block_size_;
    }

    void free(void* ptr) {
        if (!ptr || !owns(ptr)) return;
        size_t offset = static_cast<char*>(ptr) - mem_;
        size_t idx = offset / block_size_;
        next_free_[idx] = free_head_;
        free_head_ = idx;
    }

    size_t free_count() const {
        size_t n = 0;
        for (size_t i = free_head_; i != static_cast<size_t>(-1); i = next_free_[i]) {
            ++n;
        }
        return n;
    }

    bool empty() const { return free_head_ == static_cast<size_t>(-1); }

private:
    char* mem_{ nullptr };
    size_t block_size_{ 0 };
    size_t capacity_{ 0 };
    size_t free_head_{ static_cast<size_t>(-1) };
    size_t next_free_[DEFAULT_FRAME_POOL_CAPACITY];
};

static_assert(DEFAULT_FRAME_BLOCK_SIZE >= 80,
    "Frame block size must be >= 80 bytes (minimum coroutine frame)");

// ============================================================================
// coroutine_service
// ============================================================================

// ========================================================================
// fd operation state: tracked by coroutine_service for concurrency control
// ========================================================================

enum class fd_read_state : uint8_t {
    NONE,       // No read in progress
    PENDING,    // Read submitted, read coroutine is suspended
    CANCELLING  // Cancel has been submitted, waiting for CQE
};

struct fd_info {
    fd_read_state state_{ fd_read_state::NONE };
    async_read_result* pending_read_awaiter_{ nullptr };
};
using fd_info_ptr = std::shared_ptr<fd_info>;

// Max number of tracked fds (must be larger than any fd that will be tracked)
static constexpr uint32_t CORO_MAX_FD = 100000;

class coroutine_service {
public:
    explicit coroutine_service(event::event_service* svc,
                               size_t frame_block_size = DEFAULT_FRAME_BLOCK_SIZE,
                               size_t frame_pool_capacity = DEFAULT_FRAME_POOL_CAPACITY)
        : svc_(svc)
        , frame_pool_(frame_block_size, frame_pool_capacity)
        , fd_info_map_()
    {
        set_current();
    }

    ~coroutine_service() {
        clear_spawned();
        clear_current();
    }

    coroutine_service(const coroutine_service&) = delete;
    coroutine_service& operator=(const coroutine_service&) = delete;

    /// Activate pool allocation: sets thread-local pointer so that
    /// promise_type::operator new allocates frames from the pool.
    /// Called automatically in constructor; call manually if you
    /// re-enter a coroutine-creating context outside this service's
    /// lifetime (rare).
    void set_current();
    void clear_current();

    /// Access the underlying event_service, e.g. for direct callback-based usage.
    event::event_service* get_event_service() const { return svc_; }

    // ========================================================================
    // Async Read (iov-based)
    // ========================================================================

    /**
     * @brief Submit an async read and suspend until completion.
     *
     * Usage:
     *   ::iovec iov{ buf, sizeof(buf) };
     *   auto ar = coro_svc.read(fd, &iov, 1);
     *   int32_t bytes = co_await *ar;
     */
    auto read(const event::fd_t& fd,
              ::iovec* buffer, uint32_t buffer_iov_cnt,
              const event::read_options* opt = nullptr)
        -> std::unique_ptr<async_read_result>
    {
        auto ar = std::make_unique<async_read_result>();

        auto fd_key = static_cast<uint32_t>(fd.get_fd());
        const auto& found_ref = fd_info_map_.find(fd_key);
        fd_info_ptr info_ptr;
        if (found_ref != nullptr) {
            info_ptr = found_ref;
        } else {
            info_ptr = std::make_shared<fd_info>();
            STABLE_INFRA_ASSERT(fd_info_map_.insert(fd_key, info_ptr));
        }

        if (info_ptr->state_ != fd_read_state::NONE) {
            ar->complete(-EALREADY, buffer, buffer_iov_cnt, nullptr);
            return ar;
        }

        info_ptr->state_ = fd_read_state::PENDING;
        info_ptr->pending_read_awaiter_ = ar.get();

        auto ar_cb = ar->as_read_callback();
        auto tmp_cb = [this, fd_key, ar_cb = std::move(ar_cb)](
                int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) mutable {
            const auto& found_ref = fd_info_map_.find(fd_key);
            if (found_ref != nullptr) {
                found_ref->state_ = fd_read_state::NONE;
                found_ref->pending_read_awaiter_ = nullptr;
            }
            ar_cb(ret, iov, iov_cnt, extra);
        };

        int32_t rc = svc_->submit_async_read(fd, buffer, buffer_iov_cnt,
                                              tmp_cb, opt);
        if (rc != 0) {
            info_ptr->state_ = fd_read_state::NONE;
            info_ptr->pending_read_awaiter_ = nullptr;
            ar->complete(rc, buffer, buffer_iov_cnt, nullptr);
        }
        return ar;
    }

    /**
     * @brief Read using pre-registered buffer group (for buffer-ring mode)
     */
    auto read(const event::fd_t& fd, uint32_t buffer_group_id,
              const event::read_options* opt = nullptr)
        -> std::unique_ptr<async_read_result>
    {
        auto ar = std::make_unique<async_read_result>();

        auto fd_key = static_cast<uint32_t>(fd.get_fd());
        const auto& found_ref = fd_info_map_.find(fd_key);
        fd_info_ptr info_ptr;
        if (found_ref != nullptr) {
            info_ptr = found_ref;
        } else {
            info_ptr = std::make_shared<fd_info>();
            STABLE_INFRA_ASSERT(fd_info_map_.insert(fd_key, info_ptr));
        }

        if (info_ptr->state_ != fd_read_state::NONE) {
            ar->complete(-EALREADY, nullptr, 0, nullptr);
            return ar;
        }

        info_ptr->state_ = fd_read_state::PENDING;
        info_ptr->pending_read_awaiter_ = ar.get();

        auto ar_cb = ar->as_read_callback();
        auto tmp_cb = [this, fd_key, ar_cb = std::move(ar_cb)](
                int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) mutable {
            const auto& found_ref = fd_info_map_.find(fd_key);
            if (found_ref != nullptr) {
                found_ref->state_ = fd_read_state::NONE;
                found_ref->pending_read_awaiter_ = nullptr;
            }
            ar_cb(ret, iov, iov_cnt, extra);
        };

        int32_t rc = svc_->submit_async_read(fd, buffer_group_id,
                                              tmp_cb, opt);
        if (rc != 0) {
            info_ptr->state_ = fd_read_state::NONE;
            info_ptr->pending_read_awaiter_ = nullptr;
            ar->complete(rc, nullptr, 0, nullptr);
        }
        return ar;
    }

    // ========================================================================
    // Async Write
    // ========================================================================

    /**
     * @brief Submit an async write and suspend until completion.
     *
     * Usage:
     *   ::iovec iov{ data, len };
     *   auto ar = coro_svc.write(fd, &iov, 1);
     *   int32_t bytes = co_await *ar;
     */
    auto write(const event::fd_t& fd,
               ::iovec* buffer, uint32_t buffer_iov_cnt,
               const event::write_options* opt = nullptr)
        -> std::unique_ptr<async_result>
    {
        auto ar = std::make_unique<async_result>();
        int32_t rc = svc_->submit_async_write(fd, buffer, buffer_iov_cnt,
                                               ar->as_callback(), opt);
        if (rc != 0) {
            ar->complete(rc);
        }
        return ar;
    }

    // ========================================================================
    // Async Accept
    // ========================================================================

    /**
     * @brief Submit an async accept and suspend until completion.
     *
     * Usage:
     *   event::sock_info addr_buf;
     *   auto ar = coro_svc.accept(listen_fd, &addr_buf);
     *   int32_t ret = co_await *ar;
     *   // ret = 0 on success, negative on error
     *   // ar->sock_info().fd_ is the accepted client fd
     */
    auto accept(const event::fd_t& listen_fd,
                event::sock_info* addr_buffer)
        -> std::unique_ptr<async_accept_result>
    {
        auto ar = std::make_unique<async_accept_result>();
        int32_t rc = svc_->submit_async_accept(listen_fd, addr_buffer,
                                                ar->as_accept_callback());
        if (rc != 0) {
            ar->complete(rc, event::sock_info{});
        }
        return ar;
    }

    // ========================================================================
    // Async Cancel
    // ========================================================================

    auto cancel_read(const event::fd_t& fd)
        -> std::unique_ptr<async_result>
    {
        auto ar = std::make_unique<async_result>();

        auto fd_key = static_cast<uint32_t>(fd.get_fd());
        const auto& found_ref = fd_info_map_.find(fd_key);
        if (found_ref == nullptr) {
            ar->complete(-EALREADY);
            return ar;
        }

        if (found_ref->state_ != fd_read_state::PENDING) {
            ar->complete(-EALREADY);
            return ar;
        }

        found_ref->state_ = fd_read_state::CANCELLING;

        // Wake up the suspended read coroutine with -ECANCELED
        if (found_ref->pending_read_awaiter_ != nullptr) {
            found_ref->pending_read_awaiter_->complete(-ECANCELED, nullptr, 0, nullptr);
            found_ref->pending_read_awaiter_ = nullptr;
        }

        // Guard: prevents the cancel CQE callback from completing `ar` after
        // it has already been completed synchronously (when submit succeeds).
        auto done_flag = std::make_shared<std::atomic<bool>>(false);

        auto cancel_ar_cb = ar->as_callback();
        auto guarded_cb = [this, fd_key, done_flag, cancel_ar_cb = std::move(cancel_ar_cb)](int32_t res) mutable {
            if (done_flag->exchange(true)) return;  // already handled
            const auto& found_ref = fd_info_map_.find(fd_key);
            if (found_ref != nullptr) {
                found_ref->state_ = fd_read_state::NONE;
            }
            cancel_ar_cb(res);
        };

        int32_t rc = svc_->cancel_async_read(fd, std::move(guarded_cb));
        if (rc != 0) {
            found_ref->state_ = fd_read_state::NONE;
            ar->complete(rc);
        } else {
            // Mark done BEFORE completing ar, so any race with CQE callback
            // is resolved by the atomic guard.
            done_flag->store(true);
            ar->complete(0);
        }
        return ar;
    }

    auto cancel_write(const event::fd_t& fd)
        -> std::unique_ptr<async_result>
    {
        auto ar = std::make_unique<async_result>();
        int32_t rc = svc_->cancel_async_write(fd, ar->as_callback());
        if (rc != 0) {
            ar->complete(rc);
        }
        return ar;
    }

    auto cancel_accept(const event::fd_t& listen_fd)
        -> std::unique_ptr<async_result>
    {
        auto ar = std::make_unique<async_result>();
        int32_t rc = svc_->cancel_async_accept(listen_fd, ar->as_callback());
        if (rc != 0) {
            ar->complete(rc);
        }
        return ar;
    }

    // ========================================================================
    // Timer / Sleep
    // ========================================================================

    /**
     * @brief Create a timer and suspend until it fires.
     *
     * Usage:
     *   auto ar = coro_svc.sleep(1, 0);
     *   int32_t timer_id = co_await *ar;  // 1 second
     *
     * Note: create_timer expects callback signature void(uint64_t), while
     * as_callback() returns void(int32_t).  We wrap it with a lambda that
     * converts the argument and closes the timer afterwards.
     */
    auto sleep(uint64_t interval_s, uint64_t interval_ns)
        -> std::unique_ptr<async_timer_result>
    {
        auto ar = std::make_unique<async_timer_result>();
        auto timer_id = std::make_shared<int32_t>(-1);
        // as_timer_callback() already has the correct void(uint64_t) signature.
        // Wrap to auto-close the timer after resume.
        auto raw_cb = ar->as_timer_callback();
        auto timer_cb = [svc = svc_, timer_id, raw_cb = std::move(raw_cb)](uint64_t count) mutable {
            raw_cb(count);                          // resume coroutine
            if (*timer_id >= 0) {
                svc->close_timer(*timer_id);        // cleanup timer
            }
        };
        *timer_id = svc_->create_timer(interval_s, interval_ns, std::move(timer_cb));
        if (*timer_id < 0) {
            ar->complete(static_cast<uint64_t>(*timer_id));
        }
        return ar;
    }

    // ========================================================================
    // Run job in event loop thread
    // ========================================================================

    /**
     * @brief Run a job in the event loop and await its completion.
     *
     * Useful for thread-safe operations that must run in the event loop.
     */
    auto run_in_loop(const std::function<void(void)>& job)
        -> std::unique_ptr<async_result>
    {
        auto ar = std::make_unique<async_result>();
        svc_->run_job([ar_ptr = ar.get(), job]() {
            job();
            ar_ptr->complete(0);
        });
        return ar;
    }

    // ========================================================================
    // Spawn: fire-and-forget coroutine lifecycle managed by this service
    // ========================================================================

    /**
     * @brief Spawn a fire-and-forget coroutine.
     *
     * The spawned task is automatically start()ed and tracked by this service.
     * On completion, the task's final_suspend callback pushes its
     * std::list iterator into an internal dead-queue.
     *
     * Usage:
     *   coro_svc.spawn(echo_loop(coro_svc, fd));
     *   // no manual start(), no manual vector management
     */
    void spawn(task<void> t) {
        if (t.done()) {
            return;
        }

        // Insert into tracking list
        spawned_list_.push_front(std::move(t));
        auto it = spawned_list_.begin();

        // Register final_suspend callback: push iterator to pending queue
        // and lazily schedule a single cleanup job on the evt loop.
        //
        // IMPORTANT: The coroutine frame is automatically freed by
        // promise_type::operator delete when the coroutine completes.
        // Clear the task's handle_ here so ~task() will NOT call
        // handle_.destroy() again (double-free).
        auto& promise = it->get_promise();
        promise.on_final_resume_ = [this, iter = it]() {
            // Clear handle first to prevent double-free in ~task()
            // (compiler-generated code calls operator delete after
            //  final_suspend returns, freeing the coroutine frame).
            iter->clear_handle();
            pending_cleanup_.push_back(iter);
            if (!cleanup_posted_) {
                cleanup_posted_ = true;
                svc_->run_job([this]() {
                    this->sweep_completed();
                    cleanup_posted_ = false;
                });
            }
        };

        it->start();
    }

private:
    /**
     * @brief Drain pending queue and erase completed tasks from active list.
     */
    void sweep_completed() {
        for (auto& iter : pending_cleanup_) {
            spawned_list_.erase(iter);
        }
        pending_cleanup_.clear();
    }

public:
    /**
     * @brief Cancel all spawned tasks immediately.
     */
    void clear_spawned() {
        spawned_list_.clear();
        pending_cleanup_.clear();
        cleanup_posted_ = false;
    }

    /**
     * @brief Number of currently active spawned tasks.
     */
    size_t spawned_count() const {
        return spawned_list_.size();
    }

    // ========================================================================
    // Frame pool access for task_promise_base::operator new/delete
    // ========================================================================

    void* alloc_frame(std::size_t sz) {
        if (sz <= frame_pool_.block_size()) {
            void* p = frame_pool_.alloc();
            if (p) {
                return p;
            }
        }
        return ::operator new(sz);
    }

    void free_frame(void* ptr, std::size_t sz) {
        if (sz <= frame_pool_.block_size() && frame_pool_.owns(ptr)) {
            frame_pool_.free(ptr);
        } else {
            ::operator delete(ptr);
        }
    }

    event::event_service* get_service() const { return svc_; }

private:
    event::event_service* svc_{ nullptr };
    coroutine_frame_pool frame_pool_;
    ezio::data_struct::opt_map<fd_info_ptr, uint32_t, CORO_MAX_FD> fd_info_map_;
    std::list<task<void>> spawned_list_;
    std::vector<std::list<task<void>>::iterator> pending_cleanup_;
    bool cleanup_posted_{ false };
};

extern thread_local coroutine_service* current_coro_svc;

} // namespace coroutine
} // namespace ezio

#endif // EZIO_ENABLE_COROUTINE
