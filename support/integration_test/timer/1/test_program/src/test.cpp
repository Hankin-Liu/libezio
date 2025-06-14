#include <iostream>
#include <cassert>
#include <memory>
#include "event_service.h"
using namespace std;

uint32_t timer_id{ 0 };
uint32_t timer_id2{ 0 };

void on_timer(uint64_t, ezio::event::event_service* evt_service_ptr)
{
    static int i = 0;
    std::cout << "timer 1 is triggered " << ++i << " times." << std::endl;
    if (i == 20) {
        evt_service_ptr->close_timer(timer_id);
    }
}

void on_timer2(uint64_t, ezio::event::event_service* evt_service_ptr)
{
    static int i = 0;
    std::cout << "timer 2 is triggered " << ++i << " times." << std::endl;
    if (i == 20) {
        evt_service_ptr->close_timer(timer_id2);
    }
}

int main(int argc, char** argv)
{
    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    auto ret = evt_service_ptr->open(param);
    assert(ret == 0);
    auto cb = std::bind(&on_timer, std::placeholders::_1, evt_service_ptr.get());
    timer_id = evt_service_ptr->create_timer(1, 0, cb);
    assert(timer_id >= 0);

    auto cb2 = std::bind(&on_timer2, std::placeholders::_1, evt_service_ptr.get());
    timer_id2 = evt_service_ptr->create_timer(1, 500000000, cb2);
    assert(timer_id2 >= 0);
    evt_service_ptr->start_loop();
    return 0;
}
