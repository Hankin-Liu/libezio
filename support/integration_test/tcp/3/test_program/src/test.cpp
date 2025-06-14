#include <unistd.h>
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <memory>
#include "event_service.h"
using namespace std;
using namespace ezio::event;

const std::string ip{ "0.0.0.0" };
const uint32_t port{ 13579 };

sock_info sock_info_buffer{};

void on_accept(int32_t ret, const sock_info& sock, ezio::event::event_service* evt_service_ptr, ezio::event::fd_t fd)
{
    std::cout << "ret = " << ret << ", fd = " << sock.fd_ << std::endl;
    if (ret < 0) {
        auto ret = evt_service_ptr->submit_async_accept(fd, &sock_info_buffer);
        assert(ret == 0);
    }
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
    auto tmp_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (tmp_fd < 0) {
        return -1;
    }
    auto fd = ezio::event::fd_t{ tmp_fd, ezio::event::FD_TYPE::ACCEPT_FD };
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

    const uint32_t MAX_WAIT_COUNT = 100000;
    if(listen(tmp_fd, MAX_WAIT_COUNT) < 0) {
        close(tmp_fd);
        return -1;
    }

    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    ret = evt_service_ptr->open(param);
    assert(ret == 0);

    auto cb = std::bind(&on_accept, std::placeholders::_1, std::placeholders::_2, evt_service_ptr.get(), fd);
    ret = evt_service_ptr->submit_async_accept(fd, &sock_info_buffer, cb);
    assert(ret == 0);

    evt_service_ptr->start_loop();
    return 0;
}
