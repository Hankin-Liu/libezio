/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 22: clear_spawned partial cleanup
 *
 * Spawns several coroutines (some short-lived, some long accept-waiting),
 * then calls cs.clear_spawned(). Verifies that spawn queue is cleaned.
 *
 * Note: clear_spawned() cleans the spawn queue for coroutines that have
 * not yet started, not already-running coroutines waiting on IO.
 *
 * Covers:
 *   - cs.clear_spawned() clears pending spawns
 *   - No crash when clearing with mixed state
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int NUM_SHORT = 5;
const int NUM_LONG = 10;
std::atomic<int> completed_short{0};
std::atomic<int> completed_long{0};
std::atomic<bool> clear_done{false};
std::atomic<int> post_clear_completed{0};

ezio::coroutine::task<void> short_job(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    completed_short.fetch_add(1);
    co_return;
}

ezio::coroutine::task<void> long_job(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    // Long sleep to simulate never-completing IO work
    auto ar = cs.sleep(3600, 0);  // 1 hour
    co_await *ar;
    completed_long.fetch_add(1);
    co_return;
}

int main(int argc, char** argv) {
    auto thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Start event loop first
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Spawn short jobs (complete immediately)
    for (int i = 0; i < NUM_SHORT; ++i) {
        cs.spawn(short_job(cs, evt_service_ptr));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Spawn long jobs (will stay pending)
    for (int i = 0; i < NUM_LONG; ++i) {
        cs.spawn(long_job(cs, evt_service_ptr));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t before = cs.spawned_count();
    std::cout << "BEFORE_CLEAR: spawned=" << before
              << " short=" << completed_short.load() << " long=" << completed_long.load()
              << std::endl;

    // Clear all spawned tasks
    cs.clear_spawned();

    size_t after = cs.spawned_count();
    std::cout << "AFTER_CLEAR: spawned=" << after
              << " long=" << completed_long.load()
              << std::endl;

    bool pass = (before == (size_t)(NUM_SHORT + NUM_LONG)) && (after == 0);
    clear_done.store(pass);

    evt_service_ptr->close();
    thread_pool_ptr->join();

    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 22" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
