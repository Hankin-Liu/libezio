#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
#include "event/notifier.h"
#include "coroutine/awaitable.h"
using namespace std;
using namespace ezio::thread;

atomic<bool> notified{false};

// Coroutine that uses event_service's run_in_loop + sleep to simulate
// a notifier-based wait: just await a sleep, then check notification
ezio::coroutine::task<void> waiter(ezio::coroutine::coroutine_service& cs, ezio::event::event_service* evt_svc) {
    auto ar = cs.sleep(0, 5000000);
    co_await *ar;
    notified.store(true);
    cout << "WAITER: resumed after 5ms sleep" << endl;
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
    cs.spawn(waiter(cs, evt_service_ptr));
    thread_pool_ptr->start();
    thread_pool_ptr->join();
    bool pass = notified.load();
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 8" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    return pass ? 0 : -1;
}
