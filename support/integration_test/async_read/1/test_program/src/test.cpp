#include <fcntl.h>
#include <iostream>
#include <cassert>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include "event_service.h"
using namespace std;

const uint32_t BUFFER_SIZE = 16384;
char buffer[BUFFER_SIZE + 1]{};
::iovec iov = { .iov_base = buffer, .iov_len = BUFFER_SIZE };
ezio::event::fd_t fd{};
std::shared_ptr<ezio::event::event_service> evt_service_ptr{ nullptr };

void read_callback(int32_t ret, const ::iovec* data_iov, uint32_t iov_cnt)
{
    if (ret < 0) {
        std::cout << "read return " << ret << std::endl;
        auto ret = evt_service_ptr->submit_async_read(fd, &iov, 1);
        assert(ret == 0);
        return;
    } else if (ret == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto ret1 = evt_service_ptr->submit_async_read(fd, &iov, 1);
        assert(ret1 == 0);
        return;
    }

    uint32_t data_len = ret;
    std::string data((const char*)data_iov->iov_base, data_len);
    std::cout << data;
    auto ret1 = evt_service_ptr->submit_async_read(fd, &iov, 1);
    assert(ret1 == 0);
}

int main(int argc, char** argv)
{
    srand((unsigned int)time(NULL));
    evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) {
        param.poll_type_ = std::stoi(argv[1]);
    }
    auto ret = evt_service_ptr->open(param);
    assert(ret == 0);
    auto tmp_fd = open("./output.txt", O_RDONLY);
    if (tmp_fd < 0) {
        return -1;
    }
    fd = ezio::event::fd_t{ tmp_fd, ezio::event::FD_TYPE::FILE_FD };
    auto cb = std::bind(&read_callback, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    ret = evt_service_ptr->submit_async_read(fd, &iov, 1, cb);
    assert(ret == 0);

    evt_service_ptr->start_loop();
    return 0;
}
