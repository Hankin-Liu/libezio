/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 23: cs.read() on accepted TCP fd (event_thread_pool + spawn)
 *
 * Uses event_thread_pool for event loop management.
 * Uses cs.spawn() with spawn-before-start pattern.
 *
 * Tests: cs.accept() then cs.read() in a spawned coroutine on raw listen socket.
 * The test harness (start.sh) connects and sends data via a separate client.
 *
 * Covers:
 *   - cs.accept() + cs.read() chain in spawned coroutine
 *   - End-to-end data verification
 *   - Spawn-before-start pattern with event_thread_pool
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const char* TEST_DATA = "Hello via coroutine read!";
const int TEST_PORT = 11523;
std::atomic<bool> read_done{false};
char g_buf[128];

ezio::coroutine::task<void> chain(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& lfd)
{
    // Accept
    ezio::event::sock_info addr = {};
    auto aa = cs.accept(lfd, &addr);
    int ar = co_await *aa;
    std::cout << "ACCEPT: ret=" << ar << " fd=" << addr.fd_.get_fd() << std::endl;
    if (ar < 0) {
        cs.get_event_service()->close();
        co_return;
    }

    // Read on accepted fd
    ::iovec iov{ g_buf, sizeof(g_buf) };
    auto ar2 = cs.read(addr.fd_, &iov, 1);
    int32_t bytes = co_await *ar2;
    std::cout << "READ_RESULT: bytes=" << bytes << std::endl;

    bool ok = (bytes > 0 && memcmp(g_buf, TEST_DATA, (size_t)bytes) == 0);
    std::cout << "READ_RESULT: verify=" << (ok ? "PASS" : "FAIL") << std::endl;
    read_done.store(ok);
    cs.get_event_service()->close();
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
    int listen_sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_sock >= 0);
    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in laddr;
    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    laddr.sin_port = htons(TEST_PORT);
    assert(::bind(listen_sock, (struct sockaddr*)&laddr, sizeof(laddr)) == 0);
    assert(::listen(listen_sock, 5) == 0);
    std::cout << "LISTENER: fd=" << listen_sock << " port=" << TEST_PORT << std::endl;
    ezio::event::fd_t lfd(listen_sock, ezio::event::FD_TYPE::ACCEPT_FD);

    // Spawn before start
    cs.spawn(chain(cs, lfd));

    // Start the event loop thread
    thread_pool_ptr->start();

    // Signal start.sh that the server is ready
    std::cout << "READY" << std::endl;
    fflush(stdout);

    // Wait for the event loop to complete
    thread_pool_ptr->join();
    ::close(listen_sock);

    bool pass = read_done.load();
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 23" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
