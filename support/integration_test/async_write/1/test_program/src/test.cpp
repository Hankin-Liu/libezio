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
::iovec iov = { .iov_base = buffer, .iov_len = 0 };
std::shared_ptr<ezio::event::event_service> evt_service_ptr{ nullptr };
ezio::event::fd_t fd{};
uint32_t current_len{ 0 };

char* rand_str(char *str, const int len)
{
    if (len == 0) {
        str = nullptr;
        return nullptr;
    }
    static int t = 0;
    if (t++ == 0) {
        srand(time(NULL));
    }
    int i = 0;
    for (; i < len; ++i)
    {
        switch ((rand() % 3))
        {
        case 1:
            str[i] = 'A' + rand() % 26;
            break;
        case 2:
            str[i] = 'a' + rand() % 26;
            break;
        default:
            str[i] = '0' + rand() % 10;
            break;
        }
    }
    str[len - 1] = '\n';
    return str;
}

void write_one_random_str(ezio::event::fd_t fd, const std::function<void(int32_t)>& cb)
{
    uint32_t len = rand() % BUFFER_SIZE;
    while (len == 0) {
        len = rand() % BUFFER_SIZE;
    }
    rand_str(buffer, len);
    iov.iov_len = len;
    auto ret = evt_service_ptr->submit_async_write(fd, &iov, 1, cb);
    assert(ret == 0);
    std::string tmp(buffer, len);
    printf("%s", tmp.c_str());
    current_len = len;
}

void write_callback(int32_t ret)
{
    static uint32_t cnt = 0;
    if (ret < 0) {
        std::cerr << "ret = " << ret << std::endl;
        std::exit(0);
    }
    if (current_len == (uint32_t)ret) {
        if (++cnt < 10000) {
            write_one_random_str(fd, nullptr);
        } else {
            std::exit(0);
        }
    } else {
        current_len -= ret;
    }
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

    auto tmp_fd = open("./output.txt", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (tmp_fd < 0) {
        return -1;
    }
    fd = ezio::event::fd_t{ tmp_fd, ezio::event::FD_TYPE::FILE_FD };
    auto cb = std::bind(&write_callback, std::placeholders::_1);
    write_one_random_str(fd, cb);

    evt_service_ptr->start_loop();
    return 0;
}
