/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 9: task<T> value propagation
 *
 * Spawns a coroutine that sleeps 1ms then stores a value (99) into an atomic.
 * Verifies the value was properly propagated after the coroutine completes.
 *
 * Covers:
 *   - task<void> with parameter passing
 *   - value propagation via atomic
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

atomic<int> final_value{0};

ezio::coroutine::task<void> compute_and_store(
    ezio::coroutine::coroutine_service& cs,
    int v,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.sleep(0, 1000000);
    co_await *ar;
    final_value.store(v);
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

    // Start event loop first to ensure epoll is initialized before spawning
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.spawn(compute_and_store(cs, 99, evt_service_ptr));

    thread_pool_ptr->join();
    bool pass = (final_value.load() == 99);
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 9" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
