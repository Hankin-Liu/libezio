/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 7: frame_pool allocation
 *
 * Spawns 200 coroutines, verifying they all complete.  When EZIO_ENABLE_COROUTINE
 * is active and a coroutine_service exists, coroutine frames are allocated from
 * the pre-allocated pool (zero heap allocation per frame).
 *
 * Covers:
 *   - coroutine_frame_pool alloc / free
 *   - task_promise_base::operator new uses pool
 *   - Large-scale coroutine lifecycle
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

constexpr int N = 200;
std::atomic<int> completions{0};

ezio::coroutine::task<void> worker(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc,
    int id)
{
    auto ar = cs.sleep(0, 1000000);  // 1ms
    co_await *ar;
    int cnt = completions.fetch_add(1) + 1;
    std::cout << "T[" << id << "] done" << std::endl;
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

    // Start event loop first (inits epoll) before spawning
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (int i = 0; i < N; ++i) {
        cs.spawn(worker(cs, evt_service_ptr, i));
    }

    thread_pool_ptr->join();
    bool pass = completions.load() == N;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 7" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
