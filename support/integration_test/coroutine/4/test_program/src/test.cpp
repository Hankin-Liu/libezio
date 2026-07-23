/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 4: cancel_read
 *
 * Creates a TCP connection, starts a coroutine read that suspends,
 * then cancels it via cs.cancel_read(). Verifies the read coroutine
 * receives -ECANCELED.
 *
 * Covers:
 *   - cs.cancel_read() on fd with pending coroutine read
 *   - Coroutine resumption with error code
 *   - fd state tracking (PENDING -> CANCELLING -> NONE)
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <cerrno>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int TEST_PORT = 11023;
std::atomic<bool> read_completed{false};
std::atomic<int> read_result{0};
std::atomic<bool> accept_done{false};
std::atomic<int> accepted_fd{-1};

ezio::coroutine::task<void> do_run(
    ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
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

    ezio::event::fd_t listen_fd(srv, ezio::event::FD_TYPE::ACCEPT_FD);

    // Accept a connection
    ezio::event::sock_info addr_buf;
    auto ar_acc = cs.accept(listen_fd, &addr_buf);
    int32_t acc_fd = co_await *ar_acc;
    if (acc_fd < 0) {
        std::cout << "accept failed: " << acc_fd << std::endl;
        ::close(srv);
        evt_svc->close();
        co_return;
    }
    accepted_fd.store(acc_fd);
    accept_done.store(true);
    std::cout << "ACCEPT: fd=" << acc_fd << std::endl;
    ::close(srv);

    ezio::event::fd_t client_fd(acc_fd, ezio::event::FD_TYPE::TCP_FD);

    // Start a read that will suspend (no data yet)
    char buf[256];
    ::iovec iov{ buf, sizeof(buf) };
    auto ar_read = cs.read(client_fd, &iov, 1);
    int32_t result = co_await *ar_read;
    read_result.store(result);
    read_completed.store(true);
    std::cout << "READ_CANCELLED: result=" << result
              << " (expected " << -ECANCELED << ")" << std::endl;

    ::close(acc_fd);
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

    cs.spawn(do_run(cs, evt_service_ptr));
    thread_pool_ptr->start();

    // Connect client after a short delay
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    cli_addr.sin_port = htons(TEST_PORT);
    ::connect(cli, (struct sockaddr*)&cli_addr, sizeof(cli_addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cancel the read — the coroutine should resume with -ECANCELED
    int cf = accepted_fd.load();
    if (cf >= 0) {
        ezio::event::fd_t cancel_fd(cf, ezio::event::FD_TYPE::TCP_FD);
        cs.cancel_read(cancel_fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Wait for coroutine to finish
    while (!read_completed.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ::close(cli);
    cs.clear_spawned();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    evt_service_ptr->close();
    thread_pool_ptr->join();

    bool pass = read_completed.load() && read_result.load() == -ECANCELED;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 4" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
