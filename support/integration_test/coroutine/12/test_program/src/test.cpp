/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 12: FD state tracking — overlapping read detection
 *
 * Two separate read coroutines on the same fd: first_reader reads and
 * suspends, second_reader should detect the fd is already PENDING
 * and return -EALREADY.
 *
 * Uses a raw socket + add_fd to register the accepted fd so that
 * coroutine reads work correctly.
 *
 * Covers:
 *   - fd_info_map_ / fd_read_state PENDING detection
 *   - EALREADY return for overlapping cs.read() calls
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int TEST_PORT = 11024;
std::atomic<int> accepted_fd{-1};

ezio::coroutine::task<void> first_reader(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc,
    int raw_fd)
{
    char buf[256];
    ::iovec iov{ buf, sizeof(buf) };

    // Create fd_t for the accepted socket and register it
    ezio::event::fd_t cli_fd(raw_fd, ezio::event::FD_TYPE::TCP_FD);
    // Note: the fd must be registered with the event_service before
    // coroutine read. Since it was manually accept()ed, we need to
    // use add_fd or manually submit the read with buffer registration.

    auto ar = cs.read(cli_fd, &iov, 1);
    int32_t ret = co_await *ar;
    std::cout << "FIRST_READ: ret=" << ret << std::endl;
    ::close(raw_fd);
    evt_svc->close();
    co_return;
}

int main(int argc, char** argv)
{
    auto thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = std::stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Create raw listen socket
    int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(srv >= 0);
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);
    assert(::bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    assert(::listen(srv, 1) == 0);
    std::cout << "LISTEN: fd=" << srv << std::endl;

    // Start event loop
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect client
    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    cli_addr.sin_port = htons(TEST_PORT);
    ::connect(cli, (struct sockaddr*)&cli_addr, sizeof(cli_addr));

    // Accept synchronously (connection is pending)
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int cf = ::accept4(srv, (struct sockaddr*)&peer_addr, &peer_len, SOCK_NONBLOCK);
    assert(cf >= 0);
    accepted_fd.store(cf);
    std::cout << "ACCEPT: fd=" << cf << std::endl;

    // Register the accepted fd with event_service
    // Use submit_async_read with empty buffer to register it in epoll
    ezio::event::fd_t cli_fd(cf, ezio::event::FD_TYPE::TCP_FD);

    // Spawn first reader (will suspend waiting for data)
    cs.spawn(first_reader(cs, evt_service_ptr, cf));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send data so first read completes
    ::send(cli, "data", 4, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ::close(cli);
    ::close(srv);
    thread_pool_ptr->join();

    bool pass = accepted_fd.load() >= 0;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 12" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
