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

atomic<bool> coro_done{false};

ezio::coroutine::task<void> waiter(ezio::coroutine::coroutine_service& cs, ezio::event::event_service* evt_svc) {
    auto ar = cs.sleep(0, 50000000);  // 50ms
    int32_t ret = co_await *ar;
    coro_done.store(true);
    cout << "CORO: resumed after sleep, ret=" << ret << endl;
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
    bool pass = coro_done.load();
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 29" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    return pass ? 0 : -1;
}
