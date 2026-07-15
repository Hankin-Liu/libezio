/****************************************************************************************
 * @file awaitable.h
 * @brief C++20 coroutine awaitable types for libezio
 * 
 * Wraps libezio's callback-based APIs into co_await-able interfaces.
 * The underlying libezio remains callback-based; this layer translates 
 * callbacks into coroutine resumption.
 * 
 * Design principle: each awaitable type is self-contained (no manual 
 * promise_type construction). The coroutine frame is the promise;
 * we use co_await to suspend and a callback to resume.
 *
 * @author Generated
 * @license ...
 ***************************************************************************************/
#pragma once

#include <coroutine>
#include <functional>
#include <cstdint>
#include <optional>
#include <exception>
#include <system_error>
#include <atomic>
#include <type_traits>
#include <sys/uio.h>

namespace ezio {
namespace coroutine {

// ============================================================================
// Callback-based awaiter base
// ============================================================================

/**
 * @brief Base class for callbacks that bridge to coroutines.
 * 
 * Uses a simple approach: the awaitable itself holds a flag and a 
 * coroutine_handle. When co_awaited, the caller is stored; the callback
 * (registered with libezio) sets the flag and resumes the caller.
 */
class callback_awaiter_base {
public:
    callback_awaiter_base() = default;

    // Move-only
    callback_awaiter_base(const callback_awaiter_base&) = delete;
    callback_awaiter_base& operator=(const callback_awaiter_base&) = delete;
    callback_awaiter_base(callback_awaiter_base&& other) noexcept
        : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    callback_awaiter_base& operator=(callback_awaiter_base&& other) noexcept {
        if (this != &other) {
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_ = caller;
    }

protected:
    /// Called by the libezio callback to wake the waiter.
    /// Must be callable from any thread (libezio event loop thread).
    void do_resume() noexcept {
        if (handle_) {
            handle_.resume();
        }
    }

    std::coroutine_handle<> handle_{ nullptr };
};

// ============================================================================
// async_result: awaitable for a callback that returns int32_t
// ============================================================================

/**
 * @brief Awaitable for a single callback that returns int32_t.
 * 
 * Usage:
 *   async_result ar;
 *   svc->submit_async_write(fd, buf, cnt, ar.as_callback());
 *   int32_t ret = co_await ar;
 */
class async_result : public callback_awaiter_base {
public:
    async_result() = default;
    async_result(async_result&&) = default;
    async_result& operator=(async_result&&) = default;

    int32_t await_resume() {
        return result_;
    }

    /// Called by libezio callback to complete and resume
    void complete(int32_t ret) noexcept {
        result_ = ret;
        do_resume();
    }

    /// Convert to a std::function<void(int32_t)> for libezio callback
    std::function<void(int32_t)> as_callback() {
        return [this](int32_t ret) {
            this->complete(ret);
        };
    }

private:
    int32_t result_{ 0 };
};

// ============================================================================
// async_read_result: awaitable for read callback
//   callback = void(int32_t, const ::iovec*, uint32_t, void*)
// ============================================================================

class async_read_result : public callback_awaiter_base {
public:
    struct read_result {
        int32_t ret_{ 0 };              ///< bytes read (or negative errno)
        const ::iovec* iov_{ nullptr }; ///< updated iov after partial read
        uint32_t iov_cnt_{ 0 };         ///< remaining iov count
        void* extra_{ nullptr };        ///< extra data (sockaddr etc.)
    };

    async_read_result() = default;
    async_read_result(async_read_result&&) = default;
    async_read_result& operator=(async_read_result&&) = default;

    /// @brief await_resume returns the result code (bytes read or negative errno).
    ///        Use ret()/iov()/iov_cnt()/extra() to access full result.
    int32_t await_resume() {
        return result_.ret_;
    }

    void complete(int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) noexcept {
        result_.ret_ = ret;
        result_.iov_ = iov;
        result_.iov_cnt_ = iov_cnt;
        result_.extra_ = extra;
        do_resume();
    }

    /// Convert to libezio read callback signature
    std::function<void(int32_t, const ::iovec*, uint32_t, void*)> as_read_callback() {
        return [this](int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) {
            this->complete(ret, iov, iov_cnt, extra);
        };
    }

    /// Access full result after co_await
    const read_result& result() const { return result_; }
    /// Convenience: bytes read (same as co_await return value)
    int32_t ret() const { return result_.ret_; }
    const ::iovec* iov() const { return result_.iov_; }
    uint32_t iov_cnt() const { return result_.iov_cnt_; }
    void* extra() const { return result_.extra_; }

private:
    read_result result_;
};

// ============================================================================
// async_timer_result: awaitable for timer callback
//   callback = void(uint64_t)
// ============================================================================

class async_timer_result : public callback_awaiter_base {
public:
    async_timer_result() = default;
    async_timer_result(async_timer_result&&) = default;
    async_timer_result& operator=(async_timer_result&&) = default;

    uint64_t await_resume() {
        return expiration_count_;
    }

    void complete(uint64_t count) noexcept {
        expiration_count_ = count;
        do_resume();
    }

    std::function<void(uint64_t)> as_timer_callback() {
        return [this](uint64_t count) {
            this->complete(count);
        };
    }

private:
    uint64_t expiration_count_{ 0 };
};

// ============================================================================
// task<T> – awaitable coroutine return type
// ============================================================================

/**
 * @brief A simple task type for composing coroutines.
 * 
 * Allows:
 *   task<int> foo() { co_return 42; }
 *   task<int> bar() {
 *     int v = co_await foo();
 *     co_return v + 1;
 *   }
 */
template<typename T>
class task;

namespace detail {

struct task_promise_base {
    std::coroutine_handle<> continuation_{ nullptr };
    std::exception_ptr exception_{ nullptr };

    /// Optional: called in final_suspend before resuming continuation.
    /// Used by coroutine_service::spawn to auto-enqueue this task's
    /// iterator into a dead-queue for lazy cleanup.
    /// Safe to call here: the coroutine frame is still alive (we return
    /// suspend_always and the compiler does not destroy it until the
    /// task destructor calls handle_.destroy()).
    std::function<void()> on_final_resume_{ nullptr };

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() noexcept { return false; }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept {
            // Resume continuation first (if any).
            // Then fire the cleanup callback as the very last thing.
            // Order matters: the callback may have side effects that
            // assume the continuation has already been notified.
            auto& promise = h.promise();
            auto continuation = promise.continuation_;
            auto cleanup = std::move(promise.on_final_resume_);

            if (continuation) {
                continuation.resume();
            }
            // Now callback truly is the last thing that touches this frame.
            // After this returns, the coroutine is fully quiescent.
            if (cleanup) {
                cleanup();
            }
        }

        void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }
};

} // namespace detail

template<typename T>
class task {
public:
    struct promise_type : detail::task_promise_base {
        T value_;

        task get_return_object() noexcept {
            return task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        template<typename U>
        void return_value(U&& val) noexcept(std::is_nothrow_constructible_v<T, U>) {
            value_ = std::forward<U>(val);
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit task(handle_type h) : handle_(h) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~task() {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }

    T await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(handle_.promise().value_);
    }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
    }

    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    bool done() const {
        return !handle_ || handle_.done();
    }

    /// Access the promise (for coroutine_service internal use)
    promise_type& get_promise() { return handle_.promise(); }

private:
    handle_type handle_{ nullptr };
};

template<>
class task<void> {
public:
    struct promise_type : detail::task_promise_base {
        task get_return_object() noexcept {
            return task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        void return_void() noexcept {}
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit task(handle_type h) : handle_(h) {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~task() {
        if (handle_) handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }

    void await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
    }

    void start() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    bool done() const {
        return !handle_ || handle_.done();
    }

    /// Access the promise (for coroutine_service internal use)
    promise_type& get_promise() { return handle_.promise(); }

private:
    handle_type handle_{ nullptr };
};

} // namespace coroutine
} // namespace ezio
