/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 21: frame pool exhaustion fallback
 *
 * Spawns enough coroutines to exhaust the frame pool (pool has 20 slots
 * + 4 per-thread in 1 thread = default ~24). Spawns 50 to exceed pool.
 * Verifies that spawning does not crash and all coroutines complete.
 *
 * Covers:
 *   - Frame pool exhaustion
 *   - Fallback to heap allocation
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

const int NUM_COROUTINES = 50;

std::atomic<int> done_count{0};

ezio::coroutine::task<void> short_job(
    ezio::coroutine::coroutine_service& cs,
    int id,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.sleep(0, 1000000);  // 1ms
    co_await *ar;
    std::cout << "POOL_EXHAUST: coroutine " << id << " done" << std::endl;
    if (done_count.fetch_add(1) + 1 == NUM_COROUTINES) {
        evt_svc->close();
    }
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    std::cout << "POOL_EXHAUST: spawning " << NUM_COROUTINES << " coroutines" << std::endl;
    ezio::coroutine::coroutine_service cs(evt_service_ptr);
    for (int i = 0; i < NUM_COROUTINES; ++i) {
        cs.spawn(short_job(cs, i, evt_service_ptr));
    }
    thread_pool_ptr->start();
    thread_pool_ptr->join();
    bool pass = (done_count.load() == NUM_COROUTINES);
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 21" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    return pass ? 0 : -1;
}
