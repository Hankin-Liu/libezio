#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

atomic<bool> done{false};

ezio::coroutine::task<void> do_sleep(ezio::coroutine::coroutine_service& cs,
                                      ezio::event::event_service* es_ptr,
                                      uint64_t s, uint64_t ns) {
    auto ar = cs.sleep(s, ns);
    uint64_t count = co_await *ar;
    cout << "sleep(" << s << "s, " << ns << "ns): count=" << count << endl;
    done.store(true);
    es_ptr->close();
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);
    cs.spawn(do_sleep(cs, evt_service_ptr, 1, 0));
    thread_pool_ptr->start();
    thread_pool_ptr->join();

    cout << "#####################################################" << endl;
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 1" << endl;
    cout << "# RESULT     : " << (done.load() ? "PASS" : "FAILED") << endl;
    cout << "#####################################################" << endl;

    fflush(stdout);
    _exit(done.load() ? 0 : -1);
    return 0;
}
