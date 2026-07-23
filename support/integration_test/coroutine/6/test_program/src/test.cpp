/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 6: multiple sleep coroutines with cs.spawn
 *
 * Spawns N=10 coroutines that each sleep for 10ms and increment a
 * completion counter.  The last coroutine to finish calls close()
 * on the event service so the event loop exits.
 *
 * Uses delayed-spawn via run_job: instead of letting spawn() call
 * start() (handle_.resume()) on the caller thread (which may trigger
 * create_timer before epoll is inited), we first start the event loop,
 * then use run_job to spawn inside the event loop thread.
 *
 * Covers:
 *   - cs.spawn() for concurrent coroutines
 *   - concurrent multi-coroutine lifecycle
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

constexpr int N = 10;
std::atomic<int> completions{0};

ezio::coroutine::task<void> worker(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc,
    int id)
{
    auto ar = cs.sleep(0, 10000000);  // 10ms
    co_await *ar;
    int cnt = completions.fetch_add(1) + 1;
    std::cout << "WORKER[" << id << "] done (total=" << cnt << ")" << std::endl;
    if (cnt == N) evt_svc->close();
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Start the event loop first (this inits epoll) before spawning any coroutine
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now spawn workers on the event loop thread via run_job
    // This ensures create_timer runs after epoll is initialized
    for (int i = 0; i < N; ++i) {
        evt_service_ptr->run_job([&cs, evt_svc = evt_service_ptr, i]() {
            cs.spawn(worker(cs, evt_svc, i));
        });
    }

    thread_pool_ptr->join();

    bool pass = completions.load() == N;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 6" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
