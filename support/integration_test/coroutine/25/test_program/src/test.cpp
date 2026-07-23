/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 25: Multi-coro concurrent accept+read echo server
 *
 * Uses event_thread_pool + cs.spawn() with spawn-before-start pattern.
 * Spawns a coroutine that sequentially accepts NUM_CONNS connections,
 * then reads data from each. The test harness (start.sh) connects and sends
 * data via separate client processes.
 *
 * Covers:
 *   - cs.accept() + cs.read() chain in a spawned coroutine
 *   - Sequential accept on one listen fd (multiple connections)
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

const int TEST_PORT = 11525;
const int NUM_CONNS = 3;
std::atomic<int> success_count{0};
char g_bufs[NUM_CONNS][64];

ezio::coroutine::task<void> echo_loop(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& listen_fd,
    int num_conns,
    ezio::event::event_service* evt_svc)
{
    for (int i = 0; i < num_conns; ++i) {
        ezio::event::sock_info addr_buf;
        auto ar_accept = cs.accept(listen_fd, &addr_buf);
        int32_t accept_ret = co_await *ar_accept;
        if (accept_ret < 0) {
            std::cerr << "ACCEPT[" << i << "]: failed " << accept_ret << std::endl;
            continue;
        }
        int accepted_raw_fd = addr_buf.fd_.get_fd();
        std::cout << "ACCEPT[" << i << "]: accepted fd=" << accepted_raw_fd << std::endl;

        ::iovec iov{ g_bufs[i], sizeof(g_bufs[i]) };
        auto ar_read = cs.read(addr_buf.fd_, &iov, 1);
        int32_t bytes = co_await *ar_read;
        std::cout << "READ[" << i << "]: ret=" << bytes << std::endl;
        if (bytes > 0) {
            success_count.fetch_add(1);
            std::cout << "READ[" << i << "]: " << bytes << " bytes: \""
                      << std::string(g_bufs[i], (size_t)bytes) << "\"" << std::endl;
        }
    }
    std::cout << "DONE: " << success_count.load() << "/" << num_conns << " succeeded." << std::endl;
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
    assert(::bind(listen_sock, (struct sockaddr*)&laddr, sizeof(laddr)) == 0);
    assert(::listen(listen_sock, 5) == 0);
    std::cout << "LISTEN: fd=" << listen_sock << std::endl;

    ezio::event::fd_t raw_listen_fd(listen_sock, ezio::event::FD_TYPE::ACCEPT_FD);

    // Spawn the coroutine before starting the event loop thread.
    // The coroutine resumes and submits the first accept before the
    // event loop starts, so there is no data race.
    cs.spawn(echo_loop(cs, raw_listen_fd, NUM_CONNS, evt_service_ptr));

    // Start the event loop thread
    thread_pool_ptr->start();

    // Signal start.sh that the server is ready
    std::cout << "READY" << std::endl;
    fflush(stdout);

    // Wait for the event loop to complete
    thread_pool_ptr->join();
    ::close(listen_sock);

    bool pass = (success_count.load() == NUM_CONNS);
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 25" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "# " << success_count.load() << "/" << NUM_CONNS << " succeeded." << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
