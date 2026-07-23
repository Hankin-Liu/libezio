/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 26: Batch submit deferred IO (event_thread_pool + spawn)
 *
 * Uses event_thread_pool for event loop management.
 * Uses cs.spawn() with spawn-before-start pattern.
 *
 * Tests deferred IO: cs.read() with read_options set_submit(false),
 * then manual evt_service->submit() to batch-submit.
 *
 * The test harness (start.sh) connects and sends data via a separate client.
 *
 * Covers:
 *   - read_options with set_submit(false) for deferred submission
 *   - Manual batch submit via event_service::submit()
 *   - cs.accept() + deferred cs.read() in same spawned coroutine
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

const char* TEST_DATA = "Deferred submit test!";
const int TEST_PORT = 11526;
std::atomic<bool> batch_done{false};
char g_buf[64];

ezio::coroutine::task<void> chain(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& lfd)
{
    // Accept
    ezio::event::sock_info addr = {};
    auto aa = cs.accept(lfd, &addr);

    cout << "chain: awaiting accept..." << endl;
    int ar = co_await *aa;
    cout << "ACCEPT: ret=" << ar << " fd=" << addr.fd_.get_fd() << endl;
    if (ar < 0) {
        cs.get_event_service()->close();
        co_return;
    }

    // Deferred read: set_submit(false), then manual submit
    ezio::event::read_options opt;
    opt.set_submit(false);

    ::iovec iov{ g_buf, sizeof(g_buf) };
    cout << "chain: calling cs.read with set_submit(false)..." << endl;
    auto ar2 = cs.read(addr.fd_, &iov, 1, &opt);
    cout << "chain: cs.read returned, calling submit()..." << endl;

    int32_t submit_ret = cs.get_event_service()->submit();
    cout << "BATCH_SUBMIT: ret=" << submit_ret << endl;

    fflush(stdout);
    cout << "chain: awaiting read..." << endl;
    int32_t bytes = co_await *ar2;
    cout << "chain: read completed, bytes=" << bytes << endl;

    bool ok = (bytes > 0 && memcmp(g_buf, TEST_DATA, (size_t)bytes) == 0);
    cout << "BATCH_READ: bytes=" << bytes
         << " verify=" << (ok ? "PASS" : "FAIL") << endl;
    batch_done.store(ok);
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
    cout << "LISTENER: fd=" << listen_sock << " port=" << TEST_PORT << endl;
    ezio::event::fd_t lfd(listen_sock, ezio::event::FD_TYPE::ACCEPT_FD);

    cout << "main: spawning coroutine..." << endl;
    cs.spawn(chain(cs, lfd));
    cout << "main: starting event loop..." << endl;
    thread_pool_ptr->start();

    cout << "READY" << endl;
    fflush(stdout);

    thread_pool_ptr->join();
    ::close(listen_sock);

    bool pass = batch_done.load();
    cout << "#####################################################" << endl;
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 26" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    cout << "#####################################################" << endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
