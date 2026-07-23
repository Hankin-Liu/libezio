/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 14: cancel_accept / cancel_write
 *
 * Tests cs.cancel_accept() and cs.cancel_write() — cancellation of
 * pending accept and write operations.
 *
 * Accept is submitted first (spawned as a separate coroutine), then
 * cancel_accept is called.  The accept coroutine remains suspended
 * (cancel_async_accept disables read but does not resume the
 * pending coroutine), so a timer closes the event loop.
 *
 * Also tests cancel_write on an fd with no pending write.
 *
 * Covers:
 *   - cs.cancel_accept()
 *   - cs.cancel_write()
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

const int TEST_PORT = 11025;
std::atomic<int> accept_ret{-999};
std::atomic<int> cancel_accept_ret{-999};
std::atomic<int> cancel_write_ret{-999};

ezio::coroutine::task<void> do_accept_and_cancel(
    ezio::coroutine::coroutine_service& cs,
    const ezio::event::fd_t& listen_fd,
    ezio::event::event_service* evt_svc)
{
    // Step 1: submit accept
    ezio::event::sock_info addr_buf;
    auto accept_ar = cs.accept(listen_fd, &addr_buf);
    std::cout << "ACCEPT_SUBMITTED" << std::endl;

    // Step 2: cancel the pending accept
    auto cancel_ar = cs.cancel_accept(listen_fd);
    int32_t cancel_rc = co_await *cancel_ar;
    cancel_accept_ret.store(cancel_rc);
    std::cout << "CANCEL_ACCEPT: ret=" << cancel_rc << std::endl;

    // Step 3: the original accept will never complete — discard it
    // (cancel_async_accept only disables read, doesn't resume the acceptor)

    // Step 4: cancel_write on same fd (no pending write)
    auto cancel_w_ar = cs.cancel_write(listen_fd);
    int32_t cancel_w_rc = co_await *cancel_w_ar;
    cancel_write_ret.store(cancel_w_rc);
    std::cout << "CANCEL_WRITE: ret=" << cancel_w_rc << std::endl;

    // Step 5: wait a bit then close
    auto sleep_ar = cs.sleep(0, 500000);  // 500ms
    co_await *sleep_ar;

    // Now also consume the accept result (may never complete)
    // Use a short timeout approach: accept is pending, we close service
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

    // Create listen socket
    int srv_raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    assert(srv_raw >= 0);
    int opt = 1;
    ::setsockopt(srv_raw, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);
    assert(::bind(srv_raw, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    assert(::listen(srv_raw, 1) == 0);
    std::cout << "LISTEN: fd=" << srv_raw << std::endl;
    ezio::event::fd_t listen_fd(srv_raw, ezio::event::FD_TYPE::ACCEPT_FD);

    // Start event loop first
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Single coroutine does accept + cancel + sleep + close
    cs.spawn(do_accept_and_cancel(cs, listen_fd, evt_service_ptr));

    thread_pool_ptr->join();
    ::close(srv_raw);

    bool pass = (cancel_accept_ret.load() == 0) && (cancel_write_ret.load() == 0);
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 14" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
