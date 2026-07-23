/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 18: task<int> value propagation via direct co_await
 *
 * Creates and immediately co_awaits a task<int> that returns 42.
 * Verifies the value 42 propagates correctly without dangling handle.
 *
 * Key difference from original: uses direct co_await compute_value(cs)
 * instead of t.start() + sleep + co_await t, avoiding the problem of
 * task<int> destruction while coroutine frame is still alive.
 *
 * Covers:
 *   - task<int> return value propagation
 *   - direct co_await on task prvalue (temporary)
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
#include "coroutine/awaitable.h"
using namespace std;
using namespace ezio::thread;

std::atomic<int> g_value{0};

ezio::coroutine::task<int> compute_value(ezio::coroutine::coroutine_service& cs)
{
    (void)cs;
    co_return 42;
}

ezio::coroutine::task<void> use_value(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    int val = co_await compute_value(cs);
    std::cout << "TASK_INT: val=" << val << std::endl;
    g_value.store(val);
    evt_svc->close();
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Start event loop first before spawning (epoll init)
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.spawn(use_value(cs, evt_service_ptr));

    thread_pool_ptr->join();
    bool pass = (g_value.load() == 42);
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 18" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
