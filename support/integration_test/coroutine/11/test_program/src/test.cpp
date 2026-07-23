/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 11: task<void> specialization — independent coroutines
 *
 * Spawns 3 independent task<void> coroutines (stage1/2/3) each sleeping
 * for a different duration.  Verifies all 3 complete via atomic flags.
 * The last coroutine to finish calls close() on the event service.
 *
 * Covers:
 *   - task<void> non-chained usage (spawned after event loop starts)
 *   - multiple independent coroutine lifecycles
 *   - sleep-based synchronization
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

atomic<bool> s1{false}, s2{false}, s3{false};
atomic<int> completions{0};

ezio::coroutine::task<void> stage1(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc) {
    auto ar = cs.sleep(0, 1000000);
    co_await *ar;
    s1.store(true);
    if (completions.fetch_add(1) + 1 == 3) evt_svc->close();
    co_return;
}

ezio::coroutine::task<void> stage2(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc) {
    auto ar = cs.sleep(0, 2000000);
    co_await *ar;
    s2.store(true);
    if (completions.fetch_add(1) + 1 == 3) evt_svc->close();
    co_return;
}

ezio::coroutine::task<void> stage3(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc) {
    auto ar = cs.sleep(0, 3000000);
    co_await *ar;
    s3.store(true);
    if (completions.fetch_add(1) + 1 == 3) evt_svc->close();
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Start event loop first before spawning
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.spawn(stage1(cs, evt_service_ptr));
    cs.spawn(stage2(cs, evt_service_ptr));
    cs.spawn(stage3(cs, evt_service_ptr));

    thread_pool_ptr->join();
    bool pass = s1.load() && s2.load() && s3.load();
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 11" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
