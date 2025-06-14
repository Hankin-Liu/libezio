#include <iostream>
#include <cassert>
#include <memory>
#include "event_service.h"
#include "socket/tcp_socket.h"
#include "common.h"
using namespace std;

#pragma pack(1)
struct package
{
    uint32_t pkg_len_{ 0 };
    char buffer_[0];
};
#pragma pack()

const uint32_t MAX_DATA_LEN = 10000;
uint32_t data_cnt = 1;
char buffer[12000];
package* pkg = (package*)buffer; 
::iovec iov = {pkg, 0};
::iovec iov1 = {pkg, 0};
bool use_iov_1 = false;
std::shared_ptr<ezio::socket::tcp_socket> tcp_ptr = nullptr;

void on_send_done(int32_t ret, const ezio::socket::connection_info& info)
{
    if (ret != data_cnt + sizeof(package)) {
        if (ret < 0) {
            if (ret == -EAGAIN) {
                auto res = tcp_ptr->async_send(info, &iov, 1);
                assert(res == 0);
                use_iov_1 = false;
                return;
            }
            std::cout << "send error, ret = " << ret << std::endl;
            return;
        }
        std::cout << "partial send, ret = " << ret << ", send_cnt = " << data_cnt << std::endl;
        if (use_iov_1) {
            iov1.iov_base = (char*)iov1.iov_base + ret;
            iov1.iov_len -= ret;
        } else {
            iov1.iov_base = (char*)iov.iov_base + ret;
            iov1.iov_len = iov.iov_len - ret;
            use_iov_1 = true;
        }
        auto res = tcp_ptr->async_send(info, &iov1, 1);
        assert(res == 0);
        return;
    }
    ++data_cnt;
    if (data_cnt > MAX_DATA_LEN) {
        return;
    }
    rand_str(pkg->buffer_, data_cnt);
    pkg->pkg_len_ = data_cnt;
    iov.iov_len = sizeof(package) + data_cnt;
    auto res = tcp_ptr->async_send(info, &iov, 1);
    assert(res == 0);
    use_iov_1 = false;
    //std::cout << "data_len = [" << ((package*)(iov.iov_base))->pkg_len_ << "], data = [" << std::string(pkg->buffer_, data_cnt) << "]" << std::endl;
    std::cout << "data_len = [" << ((package*)(iov.iov_base))->pkg_len_ << "], data = [" << (char*)pkg->buffer_ << "]" << std::endl;
}

void on_tcp_connected(const ezio::socket::connection_info& info)
{
    std::cout << "ON_CONNECTED: ip = [" << info.get_ip_addr() << "]"
        << ", port = [" << info.get_port() << "]"
        << std::endl;
    auto cb = std::bind(&on_send_done, std::placeholders::_1, info);
    rand_str(pkg->buffer_, data_cnt);
    pkg->pkg_len_ = data_cnt;
    iov.iov_len = sizeof(package) + data_cnt;
    auto ret = tcp_ptr->async_send(info, &iov, 1, cb);
    assert(ret == 0);
    use_iov_1 = false;
    std::cout << "data_len = [" << ((package*)(iov.iov_base))->pkg_len_ << "], data = [" << (char*)pkg->buffer_ << "]" << std::endl;
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
    tcp_ptr = std::make_shared<ezio::socket::tcp_socket>();
    ezio::socket::tcp_config conf;
    conf.addr_ = "127.0.0.1:11000";
    conf.mode_ = ezio::socket::TCP_CONNECT_MODE::CONNECT;
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
