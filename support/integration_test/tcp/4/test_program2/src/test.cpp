#include <unistd.h>
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <cstring>
#include "event_service.h"
using namespace std;
using namespace ezio::event;

#pragma pack(1)
struct package
{
    uint32_t pkg_len_{ 0 };
    char buffer_[0];
};
#pragma pack()

const std::string ip{ "0.0.0.0" };
const uint32_t port{ 11000 };
char pkg_buffer[11000]{};
char recv_buffer[4096]{};
::iovec iov{ &recv_buffer[0], sizeof(recv_buffer) };
uint32_t offset{ 0 };
uint32_t part_pkg_len{ 0 };
uint32_t pkg_len_offset{ 0 };

sock_info sock_info_buffer{};

void process_data(char* data, uint32_t len)
{
    std::string tmp(data);
    if (tmp.length() + 1 != len) {
        std::cout << "ERROR PACKAGE: [" << tmp << "], expect len = " << len << ", real len = " << tmp.length() + 1
            << std::endl;
        return;
    }
    std::cout << "ON_DATA: "
        << "data_len = [" << len << "], data = [" << tmp << "]"
        << std::endl;
}

int read_tcp_data(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt, const fd_t& fd)
{
    if (ret == 0) {
        std::cout << "ON_CLOSED: fd = [" << fd.get_fd() << "]"
            << std::endl;
        return -1;
    } else if (ret < 0) {
        std::cout << "ON_ERROR: fd = [" << fd.get_fd() << "]"
            << std::endl;
        return -1;
    }
    char* data_ptr = (char*)iov_ptr->iov_base;
    uint32_t data_len = ret;
    if (pkg_len_offset != 0) {
        auto need_len = sizeof(part_pkg_len) - pkg_len_offset;
        if (data_len < need_len) {
            std::cout << "part_pkg_len: need_len = " << need_len << ", data_len = " << data_len << std::endl;
            memcpy(((char*)&part_pkg_len) + pkg_len_offset, data_ptr, data_len);
            pkg_len_offset += data_len;
            return 0;
        }
        std::cout << "finish partial head: need_len = " << need_len << ", part_pkg_len = " << part_pkg_len << std::endl;
        memcpy(((char*)&part_pkg_len) + pkg_len_offset, data_ptr, need_len);
        pkg_len_offset = 0;
        if (data_len - need_len >= part_pkg_len) {
            std::cout << "package1: header_need_len = " << need_len << ", data_len = " << data_len - need_len
                << std::endl;
            process_data(data_ptr + need_len, part_pkg_len);
            data_ptr += need_len + part_pkg_len;
            data_len -= need_len + part_pkg_len;
            part_pkg_len = 0;
            offset = 0;
        } else {
            auto recv_len = data_len - need_len;
            std::cout << "package1: header_need_len = " << need_len << ", data_len = " << recv_len << std::endl;
            memcpy(pkg_buffer, data_ptr + need_len, recv_len);
            offset = recv_len;
            return 0;
        }
    }
    if (offset != 0) {
        if (offset == UINT32_MAX) {
            offset = 0;
        }
        auto need_len =  part_pkg_len - offset;
        std::cout << "package2: need_len = " << need_len << ", data_len = " << data_len << std::endl;
        if (data_len < need_len) {
            std::cout << "package2(not finish): need_len = " << need_len << ", data_len = " << data_len << std::endl;
            memcpy(pkg_buffer + offset, data_ptr, data_len);
            offset += data_len;
            return 0;
        }
        memcpy(pkg_buffer + offset, data_ptr, need_len);
        process_data(pkg_buffer, part_pkg_len);
        data_ptr += need_len;
        data_len -= need_len;
        part_pkg_len = 0;
        offset = 0;
    }
    while (data_len >= sizeof(package)) {
        package* pkg_ptr = (package*)(data_ptr);
        auto pkg_len = pkg_ptr->pkg_len_;
        auto total_len = pkg_len + sizeof(package);
        if (total_len <= data_len) {
            process_data(pkg_ptr->buffer_, pkg_len);
        } else {
            if (data_len == sizeof(package)) {
                offset = UINT32_MAX;
            } else {
                std::cout << "copy part pkg " << ", pkg_len = " << pkg_len << ", copy_len = "
                    << data_len - sizeof(package) << std::endl;
                memcpy(pkg_buffer, pkg_ptr->buffer_, data_len - sizeof(package));
                offset = data_len - sizeof(package);
            }
            part_pkg_len = pkg_len;
            return 0;
        }
        data_ptr += total_len;
        data_len -= total_len; 
    }
    if (data_len > 0) {
        std::cout << "first partial head: data_len = " << data_len << std::endl;
        memcpy(&part_pkg_len, data_ptr, data_len);
        pkg_len_offset = data_len;
    }
    return 0;
}

void async_recv_tcp_data(const fd_t& fd, ezio::event::event_service* evt_service_ptr, const std::function<void(int32_t, const ::iovec*, uint32_t, void*)>& cb)
{
    auto ret = evt_service_ptr->submit_async_read(fd, &iov, 1, cb);
    assert(ret == 0);
}

void on_read_tcp_data(int32_t ret, const ::iovec* iov_ptr, uint32_t iov_cnt, void*, const fd_t& fd, ezio::event::event_service* evt_service_ptr)
{
    auto ret1 = read_tcp_data(ret, iov_ptr, iov_cnt, fd);
    if (ret1 == 0) {
        async_recv_tcp_data(fd, evt_service_ptr, nullptr);
    }
}

void on_accept(int32_t ret, const sock_info& sock, ezio::event::event_service* evt_service_ptr, ezio::event::fd_t fd)
{
    std::cout << "ret = " << ret << ", fd = " << sock.fd_ << std::endl;
    if (ret < 0) {
        auto ret = evt_service_ptr->submit_async_accept(fd, &sock_info_buffer);
        assert(ret == 0);
    }
    fd_t new_fd = fd_t{ ret, ezio::event::FD_TYPE::TCP_FD };
    auto cb = std::bind(&on_read_tcp_data, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, new_fd, evt_service_ptr);
    async_recv_tcp_data(new_fd, evt_service_ptr, cb);
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
