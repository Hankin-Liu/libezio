/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 20: accept+read chain in one coroutine (event_thread_pool + spawn)
 *
 * Uses event_thread_pool for event loop management.
 * Uses cs.spawn() to run the coroutine. Spawn happens before thread start,
 * consistent with the spawn-before-start pattern used by other spawn-based tests.
 *
 * A single coroutine first co_awaits accept(), then co_awaits read()
 * on the accepted fd. The test harness (start.sh) connects and sends data
 * via a separate client process, then signals readiness via a connect.
 *
 * Covers:
 *   - cs.accept() then cs.read() in same coroutine
 *   - Sequential co_await chain in a spawned coroutine
 *   - Spawn-before-start pattern with event_thread_pool
 *   - Uses a simple signal socket to block main until accept is submitted
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
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int TEST_PORT = 11520;
char g_buf[64];
std::atomic<bool> ok{false};

ezio::coroutine::task<void> chain(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& lfd,
    ezio::event::event_service* evt_svc)
{
    ezio::event::sock_info addr = {};
    auto aa = cs.accept(lfd, &addr);
    int ar = co_await *aa;
    std::cout << "ACCEPT_RET: " << ar << std::endl;
    if (ar < 0) {
        evt_svc->close();
        co_return;
    }

    ::iovec iov{g_buf, sizeof(g_buf)};
    auto ar2 = cs.read(addr.fd_, &iov, 1);
    int br = co_await *ar2;
    std::cout << "READ_RET: " << br << std::endl;
    ok.store(br > 0);
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
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_fd >= 0);
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in laddr;
    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    laddr.sin_port = htons(TEST_PORT);
    assert(::bind(listen_fd, (struct sockaddr*)&laddr, sizeof(laddr)) == 0);
    assert(::listen(listen_fd, 5) == 0);
    ezio::event::fd_t lfd(listen_fd, ezio::event::FD_TYPE::ACCEPT_FD);
    std::cout << "LISTEN: fd=" << listen_fd << std::endl;

    // Spawn the coroutine BEFORE starting the event loop thread
    cs.spawn(chain(cs, lfd, evt_service_ptr));

    // Start the event loop thread
    thread_pool_ptr->start();

    // Signal start.sh that the server is ready (port is bound and accept is submitted)
    // The test harness should now send a client connection.
    std::cout << "READY" << std::endl;
    fflush(stdout);

    // Wait for the event loop to stop (coroutine calls evt_svc->close() on completion)
    thread_pool_ptr->join();

    bool pass = ok.load();
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 20" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
