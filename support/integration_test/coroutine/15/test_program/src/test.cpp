/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 15: co_await write via coroutine
 *
 * Creates a raw TCP listen socket, accepts a connection manually,
 * then does a coroutine write on the accepted fd. Verifies data arrives
 * on the client side via recv().
 *
 * Covers:
 *   - cs.write() coroutine API path
 *   - Coroutine suspension on pending write, resume on completion
 *   - End-to-end data verification with raw sockets
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

const char* TEST_DATA = "Hello from coroutine write!";
const int TEST_PORT = 11500;
char g_server_buf[256];
std::atomic<bool> write_completed{false};
std::atomic<int> ret_bytes{-1};

ezio::coroutine::task<void> do_write(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& fd,
    ezio::event::event_service* evt_svc)
{
    ::iovec iov{ const_cast<char*>(TEST_DATA), (uint32_t)strlen(TEST_DATA) };
    auto ar = cs.write(fd, &iov, 1);
    int32_t ret = co_await *ar;
    ret_bytes.store(ret);
    std::cout << "CORO_WRITE: ret=" << ret << " (expected " << strlen(TEST_DATA) << ")" << std::endl;
    write_completed.store(ret > 0);
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
    int listen_sock = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(listen_sock >= 0);
    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    struct sockaddr_in laddr;
    memset(&laddr, 0, sizeof(laddr));
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    laddr.sin_port = htons(TEST_PORT);
    int ret = ::bind(listen_sock, (struct sockaddr*)&laddr, sizeof(laddr));
    assert(ret == 0);
    ret = ::listen(listen_sock, 5);
    assert(ret == 0);
    std::cout << "LISTENER: fd=" << listen_sock << " port=" << TEST_PORT << std::endl;

    // Start event loop
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Connect client
    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);
    ::connect(cli, (struct sockaddr*)&addr, sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Accept the connection manually
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int accepted = ::accept(listen_sock, (struct sockaddr*)&peer, &peer_len);
    assert(accepted >= 0);
    std::cout << "ACCEPTED: fd=" << accepted << std::endl;

    // Write coroutine on accepted fd
    ezio::event::fd_t write_fd(accepted, ezio::event::FD_TYPE::TCP_FD);
    cs.spawn(do_write(cs, write_fd, evt_service_ptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Client side: read the data
    memset(g_server_buf, 0, sizeof(g_server_buf));
    ssize_t nread = ::recv(cli, g_server_buf, sizeof(g_server_buf) - 1, 0);
    bool server_ok = (nread > 0) &&
                     (strncmp(g_server_buf, TEST_DATA, strlen(TEST_DATA)) == 0);
    std::cout << "CLIENT_READ: bytes=" << nread
              << " data=[" << std::string(g_server_buf, 0, (size_t)nread) << "] verify="
              << (server_ok ? "PASS" : "FAIL") << std::endl;

    ::close(accepted);
    ::close(cli);
    ::close(listen_sock);

    thread_pool_ptr->join();

    bool pass = write_completed.load() && server_ok;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 15" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
