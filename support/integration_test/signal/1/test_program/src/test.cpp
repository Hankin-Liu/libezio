#include <iostream>
#include <cassert>
#include <memory>
#include <map>
#include "event_service.h"
#include "event/signal_event.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

void on_signal(signo_t sig)
{
    std::cout << "receive signal " << sig << std::endl;
}

int main(int argc, char** argv)
{
    event_thread_pool::pointer_t thread_pool_ptr = std::make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();
    auto signal_ptr = std::make_shared<ezio::event::signal_event>();
    auto cb = std::bind(&on_signal, std::placeholders::_1);
    auto ret = signal_ptr->open(evt_service_ptr, cb);
    assert(ret == 0);

    thread_pool_ptr->start();
    thread_pool_ptr->join();
    return 0;
}
