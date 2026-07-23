/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 28: sleep() with timeout in coroutine
 *
 * Spawns a coroutine that calls cs.sleep() and verifies
 * it resumes after the timeout.
 *
 * Covers:
 *   - cs.sleep() co_await API
 *   - Timer-based coroutine resumption
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

std::atomic<bool> sleep_completed{false};

ezio::coroutine::task<void> do_sleep(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.sleep(0, 100000000); // 100ms
    int32_t ret = co_await *ar;
    std::cout << "SLEEP_RET: ret=" << ret << std::endl;
    sleep_completed.store(true);
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
    cs.spawn(do_sleep(cs, evt_service_ptr));
    thread_pool_ptr->start();
    thread_pool_ptr->join();
    bool pass = sleep_completed.load();
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 28" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    return pass ? 0 : -1;
}
