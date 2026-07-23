#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
#include "event/notifier.h"
using namespace std;
using namespace ezio::thread;

atomic<bool> notified{false};

// Coroutine that sleeps 500ms then sets flag and closes
ezio::coroutine::task<void> waiter(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.sleep(0, 500000);  // 500ms
    co_await *ar;
    notified.store(true);
    cout << "CORO_RESUMED: after notify" << endl;
    evt_svc->close();
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

    // Spawn waiter
    cs.spawn(waiter(cs, evt_service_ptr));

    thread_pool_ptr->join();

    bool pass = notified.load();
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 24" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
