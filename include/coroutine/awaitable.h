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
 * All mutable state is stored in a heap-allocated block (state) inside
 * each awaitable type. Callbacks capture a raw pointer to this state
 * block, which survives move operations (unique_ptr). The owning
 * awaitable object can be moved freely; the state block stays at the
 * same heap address until the last owner is destroyed.
 ***************************************************************************************/

#ifndef EZIO_AWAITABLE_H
#define EZIO_AWAITABLE_H

#if __cplusplus >= 202002L && EZIO_ENABLE_COROUTINE

#include <coroutine>
#include <functional>
#include <cstdint>
#include <memory>

#include "type_def.h"
#include "util/util.h"

namespace ezio::coroutine {

// ============================================================================
// callback_awaiter_base: common state holder for callback-driven awaitables
// ============================================================================

/**
 * @brief Base class holding the coroutine handle.
 *
 * All mutable state is in a heap-allocated block.  Callbacks capture a raw
 * pointer to this block (self_), which is stable across moves of the owning
 * unique_ptr/object.
 */
class callback_awaiter_base {
public:
    struct state {
        std::coroutine_handle<> handle_{ nullptr };
    };

    callback_awaiter_base() = default;

    callback_awaiter_base(const callback_awaiter_base&) = delete;
    callback_awaiter_base& operator=(const callback_awaiter_base&) = delete;
    callback_awaiter_base(callback_awaiter_base&& other) noexcept
        : self_(other.self_) {
        other.self_ = nullptr;
    }
    callback_awaiter_base& operator=(callback_awaiter_base&& other) noexcept {
        if (this != &other) {
            delete self_;
            self_ = other.self_;
            other.self_ = nullptr;
        }
        return *this;
    }

    ~callback_awaiter_base() = default;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> caller) noexcept {
        self_->handle_ = caller;
    }

protected:
    void do_resume() noexcept {
        if (self_ && self_->handle_) {
            self_->handle_.resume();
        }
    }

    state* self_{ nullptr };
};

// ============================================================================
// async_result: awaitable for a callback that returns int32_t
// ============================================================================

class async_result : public callback_awaiter_base {
public:
    /// Extended state block with result and optional cleanup
    struct async_state : state {
        bool ready_{ false };
        int32_t result_{ 0 };
        std::function<void(int32_t)> cleanup_{ nullptr };
    };

    async_result() {
        self_ = new async_state();
    }
    ~async_result() {
        delete static_cast<async_state*>(self_);
    }
    async_result(async_result&&) = default;
    async_result& operator=(async_result&&) = default;

    /// If complete() was already called before co_await, don't suspend.
    bool await_ready() const noexcept {
        auto* s = static_cast<async_state*>(self_);
        return s->ready_;
    }

    int32_t await_resume() {
        auto* s = static_cast<async_state*>(self_);
        return s->result_;
    }

    /// Called by libezio callback (via lambda capturing self_) to complete and resume
    void complete(int32_t ret) noexcept {
        auto* s = static_cast<async_state*>(self_);
        s->ready_ = true;
        s->result_ = ret;
        if (s->cleanup_) {
            s->cleanup_(ret);
        }
        if (s->handle_) {
            s->handle_.resume();
        }
    }

    /// Convert to a std::function<void(int32_t)> for libezio callback.
    /// Captures only the pointer to the heap state block – stable across moves.
    std::function<void(int32_t)> as_callback() {
        auto* s = static_cast<async_state*>(self_);
        return [s](int32_t ret) mutable {
            s->ready_ = true;
            s->result_ = ret;
            auto cb = std::move(s->cleanup_);
            if (cb) {
                cb(ret);
            }
            s->cleanup_ = std::move(cb);
            if (s->handle_) {
                s->handle_.resume();
            }
        };
    }

    /// Register a cleanup callback to be called right before resume.
    /// Receives the result code so the callback can act on it.
    void on_complete(std::function<void(int32_t)> cb) {
        auto* s = static_cast<async_state*>(self_);
        s->cleanup_ = std::move(cb);
    }
};

// ============================================================================
// async_read_result: awaitable for read callback
//   callback signature: void(int32_t, const ::iovec*, uint32_t, void*)
// ============================================================================

class async_read_result : public callback_awaiter_base {
public:
    struct read_result {
        int32_t ret_{ 0 };
        const ::iovec* iov_{ nullptr };
        uint32_t iov_cnt_{ 0 };
        void* extra_{ nullptr };
    };

    /// Extended state block with read result
    struct async_read_state : state {
        read_result result_;
    };

    async_read_result() {
        self_ = new async_read_state();
    }
    ~async_read_result() {
        delete static_cast<async_read_state*>(self_);
    }
    async_read_result(async_read_result&&) = default;
    async_read_result& operator=(async_read_result&&) = default;

    int32_t await_resume() {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_.ret_;
    }

    void complete(int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) noexcept {
        auto* s = static_cast<async_read_state*>(self_);
        s->result_.ret_ = ret;
        s->result_.iov_ = iov;
        s->result_.iov_cnt_ = iov_cnt;
        s->result_.extra_ = extra;
        do_resume();
    }

    /// Convert to libezio read callback signature.
    /// Captures only the heap state pointer – stable across moves.
    std::function<void(int32_t, const ::iovec*, uint32_t, void*)> as_read_callback() {
        auto* s = static_cast<async_read_state*>(self_);
        return [s](int32_t ret, const ::iovec* iov, uint32_t iov_cnt, void* extra) mutable {
            s->result_.ret_ = ret;
            s->result_.iov_ = iov;
            s->result_.iov_cnt_ = iov_cnt;
            s->result_.extra_ = extra;
            if (s->handle_) {
                s->handle_.resume();
            }
        };
    }

    /// Access full result after co_await
    const read_result& result() const {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_;
    }
    int32_t ret() const {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_.ret_;
    }
    const ::iovec* iov() const {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_.iov_;
    }
    uint32_t iov_cnt() const {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_.iov_cnt_;
    }
    void* extra() const {
        auto* s = static_cast<async_read_state*>(self_);
        return s->result_.extra_;
    }
};

// ============================================================================
// async_timer_result: awaitable for timer callback
//   callback signature: void(uint64_t)
// ============================================================================

class async_timer_result : public callback_awaiter_base {
public:
    /// Extended state block with timer expiration count
    struct async_timer_state : state {
        uint64_t expiration_count_{ 0 };
    };

    async_timer_result() {
        self_ = new async_timer_state();
    }
    ~async_timer_result() {
        delete static_cast<async_timer_state*>(self_);
    }
    async_timer_result(async_timer_result&&) = default;
    async_timer_result& operator=(async_timer_result&&) = default;

    uint64_t await_resume() {
        auto* s = static_cast<async_timer_state*>(self_);
        return s->expiration_count_;
    }

    void complete(uint64_t count) noexcept {
        auto* s = static_cast<async_timer_state*>(self_);
        s->expiration_count_ = count;
        do_resume();
    }

    /// Convert to timer callback. Captures only the heap state pointer.
    std::function<void(uint64_t)> as_timer_callback() {
        auto* s = static_cast<async_timer_state*>(self_);
        return [s](uint64_t count) mutable {
            s->expiration_count_ = count;
            if (s->handle_) {
                s->handle_.resume();
            }
        };
    }

private:
    uint64_t expiration_count_{ 0 };
};

// ============================================================================
// async_void_result: awaitable for a callback that returns void
//   callback signature: void(void)
//   Used by inotify watch_file, notifier, and any other void(void) callback.
// ============================================================================

class async_void_result : public callback_awaiter_base {
public:
    struct async_void_state : state {
        bool ready_{ false };
        std::shared_ptr<void> guard_{ nullptr };
        std::function<void(void)> saved_cb_{ nullptr };
    };

    async_void_result() {
        self_ = new async_void_state();
    }
    ~async_void_result() {
        auto* s = static_cast<async_void_state*>(self_);
        (void)s;
    }
    async_void_result(async_void_result&&) = default;
    async_void_result& operator=(async_void_result&&) = default;

    bool await_ready() const noexcept {
        auto* s = static_cast<async_void_state*>(self_);
        return s->ready_;
    }

    void await_resume() noexcept {}

    void complete() noexcept {
        auto* s = static_cast<async_void_state*>(self_);
        s->ready_ = true;
        do_resume();
    }

    void set_guard(std::shared_ptr<void> g) noexcept {
        auto* s = static_cast<async_void_state*>(self_);
        s->guard_ = std::move(g);
    }

    void set_saved_cb(std::function<void(void)> cb) noexcept {
        auto* s = static_cast<async_void_state*>(self_);
        s->saved_cb_ = std::move(cb);
    }

    std::function<void(void)> take_saved_cb() noexcept {
        auto* s = static_cast<async_void_state*>(self_);
        return std::move(s->saved_cb_);
    }

    std::function<void(void)> as_void_callback() {
        auto* s = static_cast<async_void_state*>(self_);
        return [s]() mutable {
            s->ready_ = true;
            if (s->handle_) {
                s->handle_.resume();
            }
        };
    }
};

// ============================================================================
// async_accept_result: awaitable for accept callback
//   callback signature: void(int32_t, const event::sock_info&)
// ============================================================================

class async_accept_result : public callback_awaiter_base {
public:
    struct accept_result {
        int32_t fd_{ -1 };
        event::sock_info info_;
    };

    /// Extended state block with accept result
    struct async_accept_state : state {
        accept_result result_;
    };

    async_accept_result() {
        self_ = new async_accept_state();
    }
    ~async_accept_result() {
        delete static_cast<async_accept_state*>(self_);
    }
    async_accept_result(async_accept_result&&) = default;
    async_accept_result& operator=(async_accept_result&&) = default;

    int32_t await_resume() {
        auto* s = static_cast<async_accept_state*>(self_);
        return s->result_.fd_;
    }

    void complete(int32_t fd, const event::sock_info& info) noexcept {
        auto* s = static_cast<async_accept_state*>(self_);
        s->result_.fd_ = fd;
        s->result_.info_ = info;
        do_resume();
    }

    /// Convert to accept callback. Captures only the heap state pointer.
    std::function<void(int32_t, const event::sock_info&)> as_accept_callback() {
        auto* s = static_cast<async_accept_state*>(self_);
        return [s](int32_t fd, const event::sock_info& info) mutable {
            s->result_.fd_ = fd;
            s->result_.info_ = info;
            if (s->handle_) {
                s->handle_.resume();
            }
        };
    }

    const accept_result& result() const {
        auto* s = static_cast<async_accept_state*>(self_);
        return s->result_;
    }

private:
    accept_result result_;
};

// ============================================================================
// task<T> – awaitable coroutine return type
// ============================================================================

template<typename T>
class task;

namespace detail {

// Trampoline functions that delegate to coroutine_service::alloc_frame/free_frame.
// Defined in coroutine_service.cpp to break the circular dependency:
//   awaitable.h --forward-declares--> coroutine_service
//   coroutine_service.h --includes--> awaitable.h
void* frame_alloc(std::size_t sz);
void  frame_free(void* ptr, std::size_t sz);

struct task_promise_base {
    std::coroutine_handle<> continuation_{ nullptr };
    std::exception_ptr exception_{ nullptr };

    // Allocate coroutine frame from the pool if current_coro_svc is set
    static void* operator new(std::size_t sz) {
        return frame_alloc(sz);
    }

    static void operator delete(void* ptr, std::size_t sz) {
        frame_free(ptr, sz);
    }

    /// Optional: called in final_suspend before resuming continuation.
    /// Used by coroutine_service::spawn to auto-enqueue this task's
    /// iterator into a dead-queue for lazy cleanup.
    std::function<void()> on_final_resume_{ nullptr };

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
        constexpr bool await_ready() noexcept { return false; }

        template<typename PromiseT>
        void await_suspend(std::coroutine_handle<PromiseT> h) noexcept {
            auto base = std::coroutine_handle<task_promise_base>::from_address(h.address());
            auto& promise = base.promise();
            if (promise.on_final_resume_) {
                promise.on_final_resume_();
            }
            auto cont = promise.continuation_;
            if (cont) {
                cont.resume();
            }
        }

        constexpr void await_resume() noexcept {}
    };

    auto final_suspend() noexcept {
        return final_awaiter{};
    }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    bool done() const noexcept { return false; }
};

} // namespace detail

template<typename T>
class [[nodiscard]] task {
public:
    struct promise_type : detail::task_promise_base {
        T value_;

        task<T> get_return_object() noexcept {
            return task<T>(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        void return_value(T v) noexcept {
            value_ = std::move(v);
        }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit task(handle_t h) : handle_(h) {}
    task(task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~task() { if (handle_) handle_.destroy(); }

    T await_resume() {
        if (handle_.promise().exception_) std::rethrow_exception(handle_.promise().exception_);
        return std::move(handle_.promise().value_);
    }

    bool await_ready() noexcept { return !handle_ || handle_.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        if (!handle_.done()) {
            return handle_;
        }
        return std::noop_coroutine();
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    promise_type& get_promise() noexcept {
        return handle_.promise();
    }

    void start() noexcept {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    /// Reset handle to nullptr without destroying.
    /// Used by coroutine_service::spawn to prevent double-destroy
    /// (the coroutine frame is already freed by promise_type::operator delete
    /// when the coroutine completes).
    void clear_handle() noexcept {
        handle_ = nullptr;
    }

    handle_t handle_;
};

/// Specialization for void return
template<>
class [[nodiscard]] task<void> {
public:
    struct promise_type : detail::task_promise_base {
        task<void> get_return_object() noexcept {
            return task<void>(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        void return_void() noexcept {}
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit task(handle_t h) : handle_(h) {}
    task(task&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~task() { if (handle_) handle_.destroy(); }

    void await_resume() {
        if (handle_.promise().exception_) std::rethrow_exception(handle_.promise().exception_);
    }

    bool await_ready() noexcept { return !handle_ || handle_.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        if (!handle_.done()) {
            return handle_;
        }
        return std::noop_coroutine();
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    promise_type& get_promise() noexcept {
        return handle_.promise();
    }

    void start() noexcept {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    /// Reset handle to nullptr without destroying.
    /// Used by coroutine_service::spawn to prevent double-destroy.
    void clear_handle() noexcept {
        handle_ = nullptr;
    }

    /// Expose raw handle for coroutine_service::spawn internal use.
    handle_t get_handle() const noexcept { return handle_; }

    handle_t handle_;
};

// ============================================================================
// generator<T>: minimal async generator (suspend on yield, resume on next)
// ============================================================================

template<typename T>
class generator {
public:
    struct promise_type {
        T current_value_;

        generator<T> get_return_object() noexcept {
            return generator<T>(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v) noexcept {
            current_value_ = std::move(v);
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit generator(handle_t h) : handle_(h) {}
    generator(generator&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    generator& operator=(generator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~generator() { if (handle_) handle_.destroy(); }

    bool next() {
        if (!handle_) return false;
        handle_.resume();
        return !handle_.done();
    }

    const T& value() const { return handle_.promise().current_value_; }

private:
    handle_t handle_;
};

} // namespace ezio::coroutine

#endif // __cplusplus >= 202002L && EZIO_ENABLE_COROUTINE

#endif // EZIO_AWAITABLE_H
