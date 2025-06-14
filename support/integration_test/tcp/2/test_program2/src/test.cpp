#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include "event_service.h"
#include "socket/tcp_socket.h"
using namespace std;

#pragma pack(1)
struct package
{
    uint32_t pkg_len_{ 0 };
    char buffer_[0];
};
#pragma pack()

char buffer[11000]{};
uint32_t offset{ 0 };
uint32_t part_pkg_len{ 0 };
uint32_t pkg_len_offset{ 0 };

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

void process_data(char* data, uint32_t len, const ezio::socket::connection_info& info)
{
    std::string tmp(data);
    if (tmp.length() + 1 != len) {
        std::cout << "ERROR PACKAGE: [" << tmp << "], expect len = " << len << ", real len = " << tmp.length() + 1
            << std::endl;
        return;
    }
    std::cout << "ON_DATA: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << ", data_len = [" << len << "], data = [" << tmp << "]"
        << std::endl;
}

void on_tcp_data(const ezio::socket::connection_info& info, ::iovec iov)
{
    char* data_ptr = (char*)iov.iov_base;
    uint32_t data_len = iov.iov_len;
    if (pkg_len_offset != 0) {
        auto need_len = sizeof(part_pkg_len) - pkg_len_offset;
        if (data_len < need_len) {
            std::cout << "part_pkg_len: need_len = " << need_len << ", data_len = " << data_len << std::endl;
            memcpy(((char*)&part_pkg_len) + pkg_len_offset, data_ptr, data_len);
            pkg_len_offset += data_len;
            return;
        }
        std::cout << "finish partial head: need_len = " << need_len << ", part_pkg_len = " << part_pkg_len << std::endl;
        memcpy(((char*)&part_pkg_len) + pkg_len_offset, data_ptr, need_len);
        pkg_len_offset = 0;
        if (data_len - need_len >= part_pkg_len) {
            std::cout << "package1: header_need_len = " << need_len << ", data_len = " << data_len - need_len
                << std::endl;
            process_data(data_ptr + need_len, part_pkg_len, info);
            data_ptr += need_len + part_pkg_len;
            data_len -= need_len + part_pkg_len;
            part_pkg_len = 0;
            offset = 0;
        } else {
            auto recv_len = data_len - need_len;
            std::cout << "package1: header_need_len = " << need_len << ", data_len = " << recv_len << std::endl;
            memcpy(buffer, data_ptr + need_len, recv_len);
            offset = recv_len;
            return;
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
            memcpy(buffer + offset, data_ptr, data_len);
            offset += data_len;
            return;
        }
        memcpy(buffer + offset, data_ptr, need_len);
        process_data(buffer, part_pkg_len, info);
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
            process_data(pkg_ptr->buffer_, pkg_len, info);
        } else {
            if (data_len == sizeof(package)) {
                offset = UINT32_MAX;
            } else {
                std::cout << "copy part pkg " << ", pkg_len = " << pkg_len << ", copy_len = "
                    << data_len - sizeof(package) << std::endl;
                memcpy(buffer, pkg_ptr->buffer_, data_len - sizeof(package));
                offset = data_len - sizeof(package);
            }
            part_pkg_len = pkg_len;
            return;
        }
        data_ptr += total_len;
        data_len -= total_len; 
    }
    if (data_len > 0) {
        std::cout << "first partial head: data_len = " << data_len << std::endl;
        memcpy(&part_pkg_len, data_ptr, data_len);
        pkg_len_offset = data_len;
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
