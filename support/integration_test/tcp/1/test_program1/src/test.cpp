#include <iostream>
#include <cassert>
#include <memory>
#include "event_service.h"
#include "socket/tcp_socket.h"
using namespace std;

void on_tcp_connected(const ezio::socket::connection_info& info)
{
    std::cout << "ON_CONNECTED: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << std::endl;
}

void on_tcp_closed(const ezio::socket::connection_info& info)
{
    std::cout << "ON_CLOSED: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << std::endl;
}

void on_tcp_error(const ezio::socket::connection_info& info, int64_t ret)
{
    std::cout << "ON_ERROR: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << ", ret = " << ret
        << std::endl;
}

void on_tcp_data(const ezio::socket::connection_info& info, ::iovec iov)
{
    std::cout << "ON_DATA: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << ", data = [" << std::string((char*)iov.iov_base, iov.iov_len) << "]"
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
    auto tcp_ptr = std::make_shared<ezio::socket::tcp_socket>();
    ezio::socket::tcp_config conf;
    conf.addr_ = "127.0.0.1:11000";
    auto connect_cb = std::bind(&on_tcp_connected, std::placeholders::_1);
    auto close_cb = std::bind(&on_tcp_closed, std::placeholders::_1);
    auto error_cb = std::bind(&on_tcp_error, std::placeholders::_1, std::placeholders::_2);
    auto data_cb = std::bind(&on_tcp_data, std::placeholders::_1, std::placeholders::_2);
    conf.reg_handler(connect_cb, close_cb, data_cb, error_cb);
    ret = tcp_ptr->open(evt_service_ptr.get(), conf);
    if (ret != 0) {
        return -1;
    }

    evt_service_ptr->start_loop();
    return 0;
}
