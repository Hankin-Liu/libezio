/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 3: co_await accept via coroutine (event_thread_pool pattern)
 *
 * Uses raw listen socket + cs.accept() to accept a TCP connection.
 * Server socket is created on the event loop thread via run_job().
 *
 * Covers:
 *   - cs.accept() on a listening fd
 *   - async_accept_result
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int TEST_PORT = 11022;
std::atomic<bool> accept_done{false};
std::atomic<int> accepted_fd{-1};

ezio::coroutine::task<void> do_accept(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc,
    const ezio::event::fd_t& listen_fd)
{
    ezio::event::sock_info addr_buf;
    auto ar = cs.accept(listen_fd, &addr_buf);
    int32_t fd = co_await *ar;
    accepted_fd.store(fd);
    accept_done.store(true);
    std::cout << "CORO_ACCEPT: fd=" << fd << std::endl;
    if (fd >= 0) ::close(fd);
    evt_svc->close();
    co_return;
}

int main(int argc, char** argv)
{
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Create raw listen socket via run_job on event loop thread
    std::atomic<int> srv_raw_ret{-1};
    ezio::event::fd_t listen_fd;
    evt_service_ptr->run_job([&]() {
        int srv_raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (srv_raw < 0) { srv_raw_ret.store(-1); return; }
        int opt = 1;
        ::setsockopt(srv_raw, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(TEST_PORT);
        if (::bind(srv_raw, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            ::close(srv_raw); srv_raw_ret.store(-2); return;
        }
        if (::listen(srv_raw, 1) != 0) {
            ::close(srv_raw); srv_raw_ret.store(-3); return;
        }
        listen_fd = ezio::event::fd_t(srv_raw, ezio::event::FD_TYPE::ACCEPT_FD);
        srv_raw_ret.store(0);
    });

    thread_pool_ptr->start();
    while (srv_raw_ret.load() != 0) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    this_thread::sleep_for(chrono::milliseconds(100));

    // Launch accept coroutine
    cs.spawn(do_accept(cs, evt_service_ptr, listen_fd));
    this_thread::sleep_for(chrono::milliseconds(100));

    // Connect a client
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);
    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    ::connect(cli, (struct sockaddr*)&addr, sizeof(addr));
    this_thread::sleep_for(chrono::milliseconds(500));

    ::close(cli);

    thread_pool_ptr->join();

    // Clean up listen fd (if accept happened, it's already closed by do_accept)
    if (listen_fd.get_fd() >= 0) ::close(listen_fd.get_fd());

    bool pass = accept_done.load() && accepted_fd.load() >= 0;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 3" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
