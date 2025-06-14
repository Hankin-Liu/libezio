#include <iostream>
#include <cassert>
#include <memory>
#include <map>
#include <chrono>
#include "event_service.h"
#include "event/notifier.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const uint32_t NOTIFY_COUNT = 2000;
const uint32_t PRINT_COUNT = 200;

std::shared_ptr<ezio::event::notifier> ntf_ptr1;
std::shared_ptr<ezio::event::notifier> ntf_ptr2;
std::shared_ptr<ezio::event::notifier> ntf_ptr3;
std::shared_ptr<ezio::event::notifier> ntf_ptr4;
std::shared_ptr<ezio::event::notifier> ntf_ptr5;

bool is_first_on_timer = true;

void on_timer(uint64_t)
{
    if (is_first_on_timer) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        is_first_on_timer = false;
        return;
    }
    static uint32_t produce_cnt = 0;
    if (produce_cnt >= NOTIFY_COUNT) {
        return;
    }
    auto cnt = ++produce_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Producer : %u times\n", cnt);
    }
    ntf_ptr1->notify();
    ntf_ptr2->notify();
    ntf_ptr3->notify();
    ntf_ptr4->notify();
    ntf_ptr5->notify();
}

void consumer_1()
{
    static uint32_t consumer1_cnt;
    auto cnt = ++consumer1_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Consumer 1 : %u times\n", cnt);
    }
}

void consumer_2()
{
    static uint32_t consumer2_cnt;
    auto cnt = ++consumer2_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Consumer 2 : %u times\n", cnt);
    }
}

void consumer_3()
{
    static uint32_t consumer3_cnt;
    auto cnt = ++consumer3_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Consumer 3 : %u times\n", cnt);
    }
}

void consumer_4()
{
    static uint32_t consumer4_cnt;
    auto cnt = ++consumer4_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Consumer 4 : %u times\n", cnt);
    }
}

void consumer_5()
{
    static uint32_t consumer5_cnt;
    auto cnt = ++consumer5_cnt;
    if (cnt % PRINT_COUNT == 0) {
        printf("Consumer 5 : %u times\n", cnt);
    }
}

int main(int argc, char** argv)
{
    event_thread_pool::pointer_t thread_pool_ptr = std::make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    thread_pool_ptr->add_thread("producer", param);
    thread_pool_ptr->add_thread("consumer_1", param);
    thread_pool_ptr->add_thread("consumer_2", param);
    thread_pool_ptr->add_thread("consumer_3", param);
    thread_pool_ptr->add_thread("consumer_4", param);
    thread_pool_ptr->add_thread("consumer_5", param);

    // producer create a timer.
    auto producer_service = thread_pool_ptr->get_evt_service("producer").get();
    auto cb = std::bind(&on_timer, std::placeholders::_1);
    auto timer_id = producer_service->create_timer(0, 10000000, cb);
    assert(timer_id >= 0);

    ntf_ptr1 = std::make_shared<ezio::event::notifier>();
    ntf_ptr2 = std::make_shared<ezio::event::notifier>();
    ntf_ptr3 = std::make_shared<ezio::event::notifier>();
    ntf_ptr4 = std::make_shared<ezio::event::notifier>();
    ntf_ptr5 = std::make_shared<ezio::event::notifier>();

    // consumers
    auto consumer_1_service = thread_pool_ptr->get_evt_service("consumer_1").get();
    auto cb_consumer_1 = std::bind(&consumer_1);
    auto ret = ntf_ptr1->open(consumer_1_service, cb_consumer_1);
    assert(ret == 0);

    auto consumer_2_service = thread_pool_ptr->get_evt_service("consumer_2").get();
    auto cb_consumer_2 = std::bind(&consumer_2);
    ret = ntf_ptr2->open(consumer_2_service, cb_consumer_2);
    assert(ret == 0);

    auto consumer_3_service = thread_pool_ptr->get_evt_service("consumer_3").get();
    auto cb_consumer_3 = std::bind(&consumer_3);
    ret = ntf_ptr3->open(consumer_3_service, cb_consumer_3);
    assert(ret == 0);

    auto consumer_4_service = thread_pool_ptr->get_evt_service("consumer_4").get();
    auto cb_consumer_4 = std::bind(&consumer_4);
    ret = ntf_ptr4->open(consumer_4_service, cb_consumer_4);
    assert(ret == 0);

    auto consumer_5_service = thread_pool_ptr->get_evt_service("consumer_5").get();
    auto cb_consumer_5 = std::bind(&consumer_5);
    ret = ntf_ptr5->open(consumer_5_service, cb_consumer_5);
    assert(ret == 0);

    thread_pool_ptr->start();
    thread_pool_ptr->join();
    return 0;
}
