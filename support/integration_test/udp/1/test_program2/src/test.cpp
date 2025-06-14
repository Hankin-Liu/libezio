#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include "event_service.h"
#include "socket/udp_socket.h"
using namespace std;

char buffer[12000]{};

void on_udp_error(int64_t ret)
{
    std::cout << "ON_ERROR: ret = " << ret
        << std::endl;
}

void on_udp_data(const ::iovec iov, const sockaddr_storage* sockaddr)
{
    std::cout << "ON_DATA: data = ["
        << std::string((char*)iov.iov_base) << "]"
        << std::endl;
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
    auto udp_ptr = std::make_shared<ezio::socket::udp_socket>();
    ezio::socket::udp_config conf;
    conf.addr_ = "127.0.0.1:12000";
    conf.block_size_ = 64 * 1024;
    conf.block_count_ = 1024;
    auto data_cb = std::bind(&on_udp_data, std::placeholders::_1, std::placeholders::_2);
    auto error_cb = std::bind(&on_udp_error, std::placeholders::_1);
    conf.reg_handler(data_cb, error_cb);
    ret = udp_ptr->open(evt_service_ptr.get(), conf);
    if (ret != 0) {
        return -1;
    }

    evt_service_ptr->start_loop();
    return 0;
}
