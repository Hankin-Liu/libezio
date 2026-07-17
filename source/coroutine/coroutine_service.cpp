/****************************************************************************************
 * @file coroutine_service.cpp
 * @brief TLS variable definitions + frame alloc/free trampolines
 *
 * Defines the thread_local current_coro_svc pointer used by
 * task_promise_base::operator new/delete to allocate coroutine frames
 * from the pre-allocated pool.
 *
 * Also provides the detail::frame_alloc / frame_free trampolines so
 * that awaitable.h can call into coroutine_service without requiring
 * the full class definition (breaks the circular dependency between
 * awaitable.h and coroutine_service.h).
 ***************************************************************************************/

#include "coroutine/coroutine_service.h"

#if EZIO_ENABLE_COROUTINE

namespace ezio {
namespace coroutine {

// Thread-local pointer to the current coroutine_service.
// Set in coroutine_service constructor, cleared in destructor.
// All coroutines spawned during this service's lifetime will
// allocate their frames from the pre-allocated pool instead of
// the global heap — 0 alloc overhead per coroutine creation.
thread_local coroutine_service* current_coro_svc = nullptr;

// ========================================================================
// set_current / clear_current
// ========================================================================

void coroutine_service::set_current() {
    current_coro_svc = this;
}

void coroutine_service::clear_current() {
    current_coro_svc = nullptr;
}

// ========================================================================
// Frame alloc/free trampolines (break circular dependency in awaitable.h)
// ========================================================================

void* detail::frame_alloc(std::size_t sz) {
    if (current_coro_svc) {
        return current_coro_svc->alloc_frame(sz);
    }
    return ::operator new(sz);
}

void detail::frame_free(void* ptr, std::size_t sz) {
    if (current_coro_svc) {
        current_coro_svc->free_frame(ptr, sz);
    } else {
        ::operator delete(ptr);
    }
}

} // namespace coroutine
} // namespace ezio

#endif // EZIO_ENABLE_COROUTINE
