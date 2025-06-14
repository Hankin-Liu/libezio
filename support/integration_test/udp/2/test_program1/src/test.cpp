#include <iostream>
#include <cassert>
#include <memory>
#include <thread>
#include <chrono>
#include "event_service.h"
#include "socket/udp_socket.h"
#include "common.h"
using namespace std;

const uint32_t MAX_DATA_LEN = 10000;
uint32_t data_cnt = 1;
int32_t timer_id = -1;
char buffer[12000];
::iovec iov = {buffer, 0};
std::shared_ptr<ezio::socket::udp_socket> udp_ptr = nullptr;

void on_udp_error(int64_t ret)
{
    std::cout << "ON_ERROR: ret = " << ret
        << std::endl;
}

void on_udp_data(const ::iovec iov, const sockaddr_storage* sockaddr)
{
    std::cout << "ON_DATA: data = ["
        << std::string((char*)iov.iov_base, iov.iov_len) << "]"
        << std::endl;
}

void on_send_done(int32_t ret)
{
    if (ret <= 0) {
        std::cerr << "send error, ret = " << ret << std::endl;
        return;
    }
    std::cout << "data_len = [" << data_cnt << "], data = [" << (char*)buffer << "]" << std::endl;
    if (data_cnt >= MAX_DATA_LEN) {
        return;
    }
    ++data_cnt;
    rand_str(buffer, data_cnt);
    iov.iov_len = data_cnt;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto ret1 = udp_ptr->async_send(&iov, 1);
    assert(ret1 == 0);
}

void on_timer(uint64_t, ezio::event::event_service* evt_service_ptr)
{
    evt_service_ptr->close_timer(timer_id);
    rand_str(buffer, data_cnt);
    iov.iov_len = data_cnt;
    auto cb = std::bind(&on_send_done, std::placeholders::_1);
    auto ret = udp_ptr->async_send(&iov, 1, cb);
    assert(ret == 0);
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
    udp_ptr = std::make_shared<ezio::socket::udp_socket>();
    ezio::socket::udp_config conf;
    conf.peer_addr_ = "127.0.0.1:12000";
    conf.block_size_ = 64 * 1024;
    conf.block_count_ = 1024;
    auto data_cb = std::bind(&on_udp_data, std::placeholders::_1, std::placeholders::_2);
    auto error_cb = std::bind(&on_udp_error, std::placeholders::_1);
    conf.reg_handler(data_cb, error_cb);
    ret = udp_ptr->open(evt_service_ptr.get(), conf);
    if (ret != 0) {
        return -1;
    }
    auto cb = std::bind(&on_timer, std::placeholders::_1, evt_service_ptr.get());
    timer_id = evt_service_ptr->create_timer(0, 10000000, cb);
    assert(timer_id >= 0);

    evt_service_ptr->start_loop();
    return 0;
}
