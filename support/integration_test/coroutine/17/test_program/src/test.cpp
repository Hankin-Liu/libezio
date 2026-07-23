/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 17: cancel_accept API verification
 *
 * Tests cs.cancel_accept() API call on a pending coroutine accept.
 * Primary goal: API call must not crash the process.
 *
 * Covers:
 *   - cs.cancel_accept() API call
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
using namespace std;

const int TEST_PORT = 11517;
std::atomic<bool> done{false};

ezio::coroutine::task<void> accept_job(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& fd)
{
    ezio::event::sock_info addr = {};
    auto ar = cs.accept(fd, &addr);
    (void)co_await *ar;
    done.store(true);
    co_return;
}

int main(int argc, char** argv)
{
    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = std::stoi(argv[1]);
    int ret = evt_service_ptr->open(param);
    assert(ret == 0);
    ezio::coroutine::coroutine_service cs(evt_service_ptr.get());

    int ls = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(ls >= 0);
    int o = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &o, sizeof(o));
    struct sockaddr_in l = {}; l.sin_family=AF_INET; l.sin_addr.s_addr=htonl(INADDR_LOOPBACK); l.sin_port=htons(TEST_PORT);
    ::bind(ls, (sockaddr*)&l, sizeof(l)); ::listen(ls, 5);

    ezio::event::fd_t lf(ls, ezio::event::FD_TYPE::TCP_FD);

    std::thread evt([&](){ evt_service_ptr->start_loop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    cs.spawn(accept_job(cs, lf));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.cancel_accept(lf);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ::close(ls);
    evt_service_ptr->close();
    evt.join();

    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 17" << std::endl;
    std::cout << "# RESULT     : PASS" << std::endl;
    std::cout << "#####################################################" << std::endl;
    return 0;
}
