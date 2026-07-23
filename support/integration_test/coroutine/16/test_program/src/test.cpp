/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 16: cancel_write API verification
 *
 * Tests cs.cancel_write() API call on a pending coroutine write.
 * The test spawns a write coroutine then calls cancel_write.
 * Primary goal: API call must not crash the process.
 *
 * Uses atomic guard + time-based wait to let coroutine complete.
 *
 * Covers:
 *   - cs.cancel_write() API call
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
using namespace std;

const int TEST_PORT = 11516;
std::atomic<bool> done{false};

ezio::coroutine::task<void> write_job(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& fd)
{
    char buf[] = "Hello";
    ::iovec iov{buf, (uint32_t)strlen(buf)};
    auto ar = cs.write(fd, &iov, 1);
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

    std::thread evt([&](){ evt_service_ptr->start_loop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    struct sockaddr_in a = {}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(TEST_PORT);
    ::connect(cli, (sockaddr*)&a, sizeof(a));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    struct sockaddr_in p = {}; socklen_t pl = sizeof(p);
    int acc = ::accept(ls, (sockaddr*)&p, &pl);
    assert(acc >= 0);

    ezio::event::fd_t wf(acc, ezio::event::FD_TYPE::TCP_FD);
    cs.spawn(write_job(cs, wf));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.cancel_write(wf);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ::close(acc); ::close(cli); ::close(ls);
    evt_service_ptr->close();
    evt.join();

    // If we reached here without crash, cancel_write API worked
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 16" << std::endl;
    std::cout << "# RESULT     : PASS" << std::endl;
    std::cout << "#####################################################" << std::endl;
    return 0;
}
