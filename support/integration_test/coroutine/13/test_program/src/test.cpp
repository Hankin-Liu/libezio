/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 13: clear_spawned — cancel all spawned tasks
 *
 * Spawns coroutines that sleep for a very long time (never complete
 * during test), then calls cs.clear_spawned().  Verifies the spawned
 * count drops to zero.
 *
 * Covers:
 *   - cs.clear_spawned()
 *   - spawned_count() semantics
 *   - cleanup of active (not-yet-completed) tasks
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

constexpr int N = 20;

ezio::coroutine::task<void> long_sleeper(
    ezio::coroutine::coroutine_service& cs,
    int id,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.sleep(3600, 0);  // 1 hour — never completes in test
    co_await *ar;
    std::cout << "LONG[" << id << "] woke (unexpected)" << std::endl;
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

    // Start event loop first
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Spawn long sleepers — they never complete during test
    for (int i = 0; i < N; ++i) {
        cs.spawn(long_sleeper(cs, i, evt_service_ptr));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t before = cs.spawned_count();
    std::cout << "BEFORE_CLEAR: spawned=" << before << " (expected " << N << ")" << std::endl;

    // Cancel all spawned tasks
    cs.clear_spawned();

    size_t after = cs.spawned_count();
    std::cout << "AFTER_CLEAR: spawned=" << after << " (expected 0)" << std::endl;

    evt_service_ptr->close();
    thread_pool_ptr->join();

    bool pass = (before == (size_t)N && after == 0);
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 13" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
