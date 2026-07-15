/****************************************************************************************
 * @file coroutine_service.h
 * @brief Coroutine-friendly wrapper around ezio::event::event_service
 * 
 * Provides co_await-able wrappers for all event_service async operations.
 * Underneath, it still uses libezio's callback-based APIs.
 * 
 * Usage Pattern:
 *   // The awaitable is created BEFORE submit, and the callback captures
 *   // a pointer to it. After co_await, the operation is guaranteed complete.
 *   
 *   int32_t bytes = co_await coro_svc.read(fd, &iov, 1);
 *   // bytes = number read, or negative errno
 * 
 *   int32_t written = co_await coro_svc.write(fd, &iov, 1);
 *
 * @author Generated
 * @license ...
 ***************************************************************************************/
#pragma once

#include "awaitable.h"
#include "../event_service.h"
#include "../type_def.h"

#include <memory>
#include <list>
#include <vector>
#include <atomic>

namespace ezio {
namespace coroutine {

// ============================================================================
// async_accept_result: awaitable for accept callback
//   callback = void(int32_t, const sock_info&)
// ============================================================================

/**
 * @brief Awaitable for async accept operations.
 * 
 * co_await returns int32_t (0 = success, negative = error).
 * Use result().sock_info_ to get the accepted fd.
 */
class async_accept_result : public callback_awaiter_base {
public:
    struct accept_result {
        int32_t ret_{ 0 };
        event::sock_info sock_info_;
    };

    async_accept_result() = default;
    async_accept_result(async_accept_result&&) = default;
    async_accept_result& operator=(async_accept_result&&) = default;

    int32_t await_resume() {
        return result_.ret_;
    }

    void complete(int32_t ret, const event::sock_info& info) noexcept {
        result_.ret_ = ret;
        result_.sock_info_ = info;
        do_resume();
    }

    std::function<void(int32_t, const event::sock_info&)> as_accept_callback() {
        return [this](int32_t ret, const event::sock_info& info) {
            this->complete(ret, info);
        };
    }

    /// Access full result after co_await
    const accept_result& result() const { return result_; }
    int32_t ret() const { return result_.ret_; }
    const event::sock_info& sock_info() const { return result_.sock_info_; }

private:
    accept_result result_;
};

// ============================================================================
// coroutine_service
// ============================================================================

class coroutine_service {
public:
    explicit coroutine_service(event::event_service* svc) : svc_(svc) {}

    coroutine_service(const coroutine_service&) = delete;
    coroutine_service& operator=(const coroutine_service&) = delete;

    // ========================================================================
    // Async Read (iov-based)
    // ========================================================================

    /**
     * @brief Submit an async read and suspend until completion.
     * 
     * Usage:
     *   ::iovec iov{ buf, sizeof(buf) };
     *   int32_t bytes = co_await coro_svc.read(fd, &iov, 1);
     *   // bytes = bytes read, or negative errno
     *   // Use ar.ret(), ar.iov(), ar.iov_cnt() for full result
     */
    async_read_result read(const event::fd_t& fd,
                           ::iovec* buffer, uint32_t buffer_iov_cnt,
                           const event::read_options* opt = nullptr) {
        async_read_result ar;
        int32_t rc = svc_->submit_async_read(fd, buffer, buffer_iov_cnt,
                                              ar.as_read_callback(), opt);
        if (rc != 0) {
            // Submit failed synchronously — complete immediately
            ar.complete(rc, buffer, buffer_iov_cnt, nullptr);
        }
        return ar;  // caller: co_await this
    }

    /**
     * @brief Read using pre-registered buffer group (for buffer-ring mode)
     */
    async_read_result read(const event::fd_t& fd, uint32_t buffer_group_id,
                           const event::read_options* opt = nullptr) {
        async_read_result ar;
        int32_t rc = svc_->submit_async_read(fd, buffer_group_id,
                                              ar.as_read_callback(), opt);
        if (rc != 0) {
            ar.complete(rc, nullptr, 0, nullptr);
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
     *   int32_t bytes = co_await coro_svc.write(fd, &iov, 1);
     */
    async_result write(const event::fd_t& fd,
                       ::iovec* buffer, uint32_t buffer_iov_cnt,
                       const event::write_options* opt = nullptr) {
        async_result ar;
        int32_t rc = svc_->submit_async_write(fd, buffer, buffer_iov_cnt,
                                               ar.as_callback(), opt);
        if (rc != 0) {
            ar.complete(rc);
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
     *   int32_t ret = co_await coro_svc.accept(listen_fd, &addr_buf);
     *   // ret = 0 on success, negative on error
     *   // ar.sock_info().fd_ is the accepted client fd
     */
    async_accept_result accept(const event::fd_t& listen_fd,
                               event::sock_info* addr_buffer) {
        async_accept_result ar;
        int32_t rc = svc_->submit_async_accept(listen_fd, addr_buffer,
                                                ar.as_accept_callback());
        if (rc != 0) {
            ar.complete(rc, event::sock_info{});
        }
        return ar;
    }

    // ========================================================================
    // Async Cancel
    // ========================================================================

    async_result cancel_read(const event::fd_t& fd) {
        async_result ar;
        int32_t rc = svc_->cancel_async_read(fd, ar.as_callback());
        if (rc != 0) {
            ar.complete(rc);
        }
        return ar;
    }

    async_result cancel_write(const event::fd_t& fd) {
        async_result ar;
        int32_t rc = svc_->cancel_async_write(fd, ar.as_callback());
        if (rc != 0) {
            ar.complete(rc);
        }
        return ar;
    }

    async_result cancel_accept(const event::fd_t& listen_fd) {
        async_result ar;
        int32_t rc = svc_->cancel_async_accept(listen_fd, ar.as_callback());
        if (rc != 0) {
            ar.complete(rc);
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
     *   int32_t timer_id = co_await coro_svc.sleep(1, 0);  // 1 second
     *   // After resume, close the timer:
     *   svc_->close_timer(timer_id);
     */
    async_result sleep(uint64_t interval_s, uint64_t interval_ns) {
        async_result ar;
        int32_t rc = svc_->create_timer(interval_s, interval_ns,
                                         ar.as_callback());
        if (rc < 0) {
            ar.complete(rc);
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
    async_result run_in_loop(const std::function<void(void)>& job) {
        async_result ar;
        svc_->run_job([&ar, job]() {
            job();
            ar.complete(0);
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
     * std::list iterator into an internal dead-queue. Deferred cleanup is
     * batched: only the first completion after a cleanup triggers a single
     * evt loop job post; subsequent completions before cleanup runs only
     * append to the pending queue. This avoids N posts for N completions.
     *
     * Safety: the on_final_resume_ callback only pushes an iterator into a
     * vector — it never touches the coroutine frame itself. The frame stays
     * alive because final_suspend returns suspend_always. The actual erase
     * (which triggers task destructor and coroutine frame destruction) happens
     * later in sweep_completed(), which is safe because by then the callback
     * has already returned and the frame is fully quiescent.
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
        auto& promise = it->get_promise();
        promise.on_final_resume_ = [this, iter = it]() {
            pending_cleanup_.push_back(iter);
            // Only the first completion after last drain triggers a post.
            // All completions happen on the same evt loop thread, so
            // a plain bool is sufficient — no atomics needed.
            if (!cleanup_posted_) {
                cleanup_posted_ = true;
                svc_->run_job([this]() {
                    this->sweep_completed();
                    // All done — allow next batch
                    cleanup_posted_ = false;
                });
            }
        };

        it->start();
    }

private:
    /**
     * @brief Drain pending queue and erase completed tasks from active list.
     *
     * Called from a evt loop job that was registered by the first
     * completion after the last drain.
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
     *
     * Note: includes completed-but-not-yet-swept tasks.
     * Use sweep() first for accurate count.
     */
    size_t spawned_count() const {
        return spawned_list_.size();
    }

    // ========================================================================
    // Underlying service access
    // ========================================================================

    event::event_service* get_service() const { return svc_; }

private:
    event::event_service* svc_{ nullptr };
    std::list<task<void>> spawned_list_;
    std::vector<std::list<task<void>>::iterator> pending_cleanup_;
    bool cleanup_posted_{ false };
};

// ============================================================================
// Example usage
// ============================================================================
//
// ezio::coroutine::task<void> handle_connection(
//     ezio::coroutine::coroutine_service& coro_svc,
//     ezio::event::fd_t client_fd)
// {
//     char buf[4096];
//     ::iovec iov{ buf, sizeof(buf) };
//
//     int32_t nread = co_await coro_svc.read(client_fd, &iov, 1);
//     if (nread <= 0) { client_fd.close(); co_return; }
//
//     ::iovec w_iov{ buf, (size_t)nread };
//     int32_t nwritten = co_await coro_svc.write(client_fd, &w_iov, 1);
//
//     client_fd.close();
//     co_return;
// }
//
// void start_connection(ezio::coroutine::coroutine_service& svc,
//                        ezio::event::fd_t fd) {
//     auto t = handle_connection(svc, fd);
//     t.start();
// }

} // namespace coroutine
} // namespace ezio
