# libezio

A lightweight C++ event-driven I/O library for Linux, with dual epoll/io_uring backend, C++20 coroutine support, and a strong focus on **control, simplicity, and low-latency network applications**.

## Why libezio?

Existing C++ networking libraries fall into two camps: heavyweight frameworks with enormous surface areas (Boost.Asio, seastar), or minimal wrappers that leave you dealing with raw system calls. libezio sits in between — it provides a proper Reactor abstraction but stays lean enough that a single developer can read and understand the entire codebase (~8k lines).

The design prioritizes three things:

- **Direct control over I/O behavior** — you decide when to submit operations, when to batch, when to flush. The library doesn't hide the knobs.
- **No hidden allocations on the hot path** — callbacks are stored per-file-descriptor, not per-operation. After first registration, subsequent submissions reuse the same callback slot.
- **Dual backend with a unified API** — epoll and io_uring share the same interface. No `#ifdef` hell in user code.

## Features

- **Dual I/O backend:** epoll (Reactor) and io_uring (hand-written syscall wrapper, no liburing dependency)
- **Unified API:** same `submit_async_read/write/accept` for both backends
- **Multiple event loops:** `event_thread_pool` for multi-threaded, multi-loop architectures
- **Buffer ring support:** io_uring's provided-buffer mode for zero-copy reads (both backends support application-level ring buffers)
- **C++20 coroutine bridge:** wrap any synchronous async call with `co_await`, without touching the underlying event loop
- **Submit-time control:** each operation can be submitted immediately (low-latency) or deferred for batching (`submit()` on your schedule)
- **No external dependencies:** pure syscall interface for io_uring, no liburing, no Boost
- **C++11 base, C++20 extensions:** the core event loop is C++11 for broad compiler compatibility; coroutine support is a separate layer requiring C++20

## Architecture

```
┌─────────────────────────────────────────────┐
│              event_service                   │  ← User-facing API
├─────────────────────────────────────────────┤
│               event_loop                     │  ← Event dispatch engine
├──────────────────┬──────────────────────────┤
│   poll_base      │      poll_base           │
│   (epoll)        │      (io_uring)          │  ← Pluggable backend
└──────────────────┴──────────────────────────┘
```

```
┌─────────────────────────────────────────────┐
│          coroutine_service                   │  ← C++20 co_await wrapper (optional)
├─────────────────────────────────────────────┤
│            event_service                     │  ← C++11 callback API
└─────────────────────────────────────────────┘
```

- **event_service** — user-facing class. Provides `submit_async_read`, `submit_async_write`, `submit_async_accept`, `register_ring_buffer`, `create_timer`, `run_job`, and more. All operations take an `Options*` parameter for fine-grained control.
- **event_loop** — event dispatch engine. Runs a single epoll_wait / io_uring_enter loop, dispatches to per-fd callbacks.
- **poll_base / epoll / io_uring** — backend abstraction. The epoll variant wraps each fd into a `event_action` object that tracks pending tasks and registered callbacks. The io_uring variant uses `complete_action` and per-fd `iouring_event_info` for state.
- **coroutine_service** — optional C++20 layer that translates `std::function` callbacks into `co_await`-able objects. The bridge incurs negligible overhead (~30-50 ns per operation) and does not affect C++11 users.

## Quick Start (non-coroutine)

```cpp
#include "event_service.h"
#include "type_def.h"

// 1. Create event service
ezio::event::event_service evt_svc;
ezio::event::poll_param param;
param.poll_type_ = 0;        // 0 = epoll, 1 = io_uring
param.entry_cnt_ = 128;
param.polling_ms_ = -1;      // block indefinitely

evt_svc.open(param);

// 2. Submit async read on a connected fd
int fd = /* ... */;
ezio::event::fd_t evt_fd(fd, ezio::event::FD_TYPE::TCP_FD);
char buf[4096];
::iovec iov{ buf, sizeof(buf) };

evt_svc.submit_async_read(evt_fd, &iov, 1,
    [&](int32_t ret, const ::iovec* iov, uint32_t cnt, void*) {
        // ret = bytes read, or negative errno
        printf("read %d bytes\n", ret);
    });

// 3. Enter event loop
evt_svc.start_loop();
```

## Quick Start (coroutine)

```cpp
#include "coroutine/coroutine_service.h"
#include "coroutine/awaitable.h"
#include "event_service.h"

// Create services
ezio::event::event_service evt_svc;
ezio::event::poll_param param = /* ... */;
evt_svc.open(param);

ezio::coroutine::coroutine_service coro_svc(&evt_svc);

// Coroutine handler
ezio::coroutine::task<void> handle_client(
    ezio::coroutine::coroutine_service& svc, int raw_fd)
{
    ezio::event::fd_t fd(raw_fd, ezio::event::FD_TYPE::TCP_FD);
    char buf[4096];
    ::iovec iov{ buf, sizeof(buf) };

    int32_t nread = co_await svc.read(fd, &iov, 1);
    if (nread > 0) {
        ::iovec wiov{ buf, (size_t)nread };
        co_await svc.write(fd, &wiov, 1);
    }
    ::close(raw_fd);
    co_return;
}

// Start coroutine
auto task = handle_client(coro_svc, client_fd);
task.start();
```

## Submit Control

By default, every `submit_async_read/write` call immediately submits to the kernel (one SQE or one epoll_ctl per call). This gives the lowest possible latency for each I/O.

If you prefer batching, pass a read/write_options with `set_submit(false)`:

```cpp
ezio::event::read_options opt;
opt.set_submit(false);

evt_svc.submit_async_read(fd, &iov, 1, callback, &opt);
// More operations...
evt_svc.submit();  // flush all pending operations
```

This gives you fine-grained control over the latency-vs-throughput tradeoff for each operation.

## Callback Lifecycle

Callbacks are stored **per file descriptor, not per submission**:

- In the epoll backend, callbacks live in `event_action`, which is stored in `fd_to_event_info_`. Subsequent reads on the same fd reuse the existing callback.
- In the io_uring backend, callbacks live in `complete_action`, also stored in `fd_to_event_info_`. The first `submit_async_read` with a non-null callback registers it; subsequent calls can pass `nullptr` to reuse it.

This means after the first I/O registration on a given fd, **no heap allocation is needed for callbacks on subsequent operations**.

## Multi-Threaded Event Loops

```cpp
ezio::thread::event_thread_pool pool;

ezio::event::poll_param param = /* ... */;
pool.add_thread("fast_path", param);
pool.add_thread("normal_path", param);
pool.start();

auto fast_svc = pool.get_evt_service("fast_path");
// Assign high-priority connections to this event loop
fast_svc->submit_async_read(fast_fd, &iov, 1, callback);
```

Each thread runs its own independent event loop with its own epoll/io_uring instance. You control which fd goes to which thread.

## Building

```bash
cd build
sh build.sh
```

Produces `libezio.so` (shared) and `libezio.a` (static) in `lib/`.

## Requirements

### io_uring
- Linux 5.19+ (recommended) for io_uring backend with multishot & buffer ring support
### coroutine
- GCC 11+ / Clang 14+ for C++20 coroutine support

## When to use libezio

**Good fit for:**
- Network middleware where you want full control over the I/O path
- Financial/trading systems that need predictable latency and no surprises
- Projects that value compile-time stability (C++11 base) but want optional C++20 features
- Teams that prefer a readable codebase over a black-box framework

**Less suitable for:**
- Cross-platform applications (Linux only)
- Projects needing built-in SSL, HTTP, or protocol parsing
- Integration as a public-facing SDK without additional work

## License

GNU Affero General Public License v3.0. See LICENSE.
