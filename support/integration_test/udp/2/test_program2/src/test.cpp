#include <unistd.h>
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <cstring>
#include <vector>
#include "event_service.h"
using namespace std;
using namespace ezio::event;

const std::string ip{ "0.0.0.0" };
const uint32_t port{ 12000 };
const uint32_t RECV_BUFFER_CNT = 1024;
char recv_buffer[RECV_BUFFER_CNT][15000]{};
std::vector<::iovec> iovs{};

void async_recv_tcp_data(const fd_t& fd, ezio::event::event_service* evt_service_ptr, const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb)
{
    auto ret = evt_service_ptr->submit_async_read(fd, iovs.data(), iovs.size(), cb);
    assert(ret == 0);
}

void on_read_udp_data(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt, void* mmsghdrs, const fd_t& fd, ezio::event::event_service* evt_service_ptr)
{
    if (ret < 0) {
        std::cout << "ERROR, ret = " << ret << std::endl;
        return;
    }
    char* data = (char*)(iov_ptr->iov_base);
    uint32_t len = (uint32_t)ret;
    if (data[len - 1] != 0) {
        std::cout << "ERROR, data in index " << len - 1
            << " is not '0', it is " << data[len - 1] << std::endl;
        return;
    }
    std::cout << "ON_DATA: data_len = " << len << ", data = ["
        << data << "]"
        << std::endl;
    async_recv_tcp_data(fd, evt_service_ptr, nullptr);
}

int32_t make_listen_socket_reuseable(int32_t sock)
{
    int32_t ret = 0;
    int one = 1;
    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*) &one, sizeof(one));
    if (0 != ret) {
        return -1;
    }
    return 0;
}

int32_t make_listen_socket_reuseable_port(int32_t sock)
{
    int32_t ret = 0;
    int one = 1;
    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void*) &one, sizeof(one));
    if (0 != ret) {
        return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    auto tmp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (tmp_fd < 0) {
        return -1;
    }
    auto fd = ezio::event::fd_t{ tmp_fd, ezio::event::FD_TYPE::UDP_FD };
    auto ret = make_listen_socket_reuseable(fd);
    if (ret < 0) {
        return -1;
    }
    ret = make_listen_socket_reuseable_port(fd);
    if (ret < 0) {
        return -1;
    }
    struct sockaddr_in sock_addr{};
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    sock_addr.sin_port = htons(port);
    if(::bind(tmp_fd, (struct sockaddr*)&sock_addr , sizeof(struct sockaddr)) < 0) {
        return -1;
    }

    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    ret = evt_service_ptr->open(param);
    assert(ret == 0);

    iovs.resize(RECV_BUFFER_CNT);
    uint32_t i = 0;
    for (auto& iov : iovs) {
        iov.iov_base = recv_buffer[i];
        iov.iov_len = sizeof(recv_buffer[i]);
        ++i;
    }

    auto cb = std::bind(&on_read_udp_data, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, fd, evt_service_ptr.get());
    async_recv_tcp_data(fd, evt_service_ptr.get(), cb);

    evt_service_ptr->start_loop();
    return 0;
}
