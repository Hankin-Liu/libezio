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

const char* TEST_DATA = "Hello from coroutine read!";
const int TEST_PORT = 11020;

std::atomic<bool> read_passed{false};

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
    cout << "LISTEN: fd=" << srv << endl;

    ezio::event::fd_t listen_fd(srv, ezio::event::FD_TYPE::ACCEPT_FD);

    // Accept a connection via coroutine
    ezio::event::sock_info addr_buf;
    auto ar_acc = cs.accept(listen_fd, &addr_buf);
    int32_t cli_fd = co_await *ar_acc;
    if (cli_fd < 0) {
        cerr << "accept failed: " << cli_fd << endl;
        ::close(srv);
        evt_svc->close();
        co_return;
    }
    cout << "ACCEPT: fd=" << cli_fd << endl;
    ::close(srv);  // no longer need listen socket

    ezio::event::fd_t client_fd(cli_fd, ezio::event::FD_TYPE::TCP_FD);

    // Read from client via coroutine
    char buf[256];
    ::iovec iov{ buf, sizeof(buf) };
    auto ar_read = cs.read(client_fd, &iov, 1);
    int32_t bytes = co_await *ar_read;

    ::close(cli_fd);

    if (bytes < 0) {
        std::cerr << "read failed: " << bytes << std::endl;
        evt_svc->close();
        co_return;
    }

    bool ok = (bytes == (int32_t)strlen(TEST_DATA) &&
               memcmp(buf, TEST_DATA, (size_t)bytes) == 0);
    std::cout << "CORO_READ: bytes=" << bytes
              << " data=[" << std::string(buf, (size_t)bytes) << "]"
              << " verify=" << (ok ? "PASS" : "FAIL") << std::endl;
    read_passed.store(ok);
    evt_svc->close();
    co_return;
}

int main(int argc, char** argv) {
    event_thread_pool::pointer_t thread_pool_ptr = make_shared<event_thread_pool>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = stoi(argv[1]);
    thread_pool_ptr->add_thread("main", param);
    auto evt_service_ptr = thread_pool_ptr->get_evt_service("main").get();

    ezio::coroutine::coroutine_service cs(evt_service_ptr);

    // Spawn: the coroutine itself does listen + accept + read
    cs.spawn(do_run(cs, evt_service_ptr));

    thread_pool_ptr->start();
    this_thread::sleep_for(chrono::milliseconds(300));

    // Connect client (from main thread)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(TEST_PORT);
    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(cli >= 0);
    ::connect(cli, (struct sockaddr*)&addr, sizeof(addr));
    this_thread::sleep_for(chrono::milliseconds(200));

    // Send data
    ::send(cli, TEST_DATA, strlen(TEST_DATA), 0);
    this_thread::sleep_for(chrono::milliseconds(1000));

    ::close(cli);

    thread_pool_ptr->join();

    bool pass = read_passed.load();
    cout << "#####################################################" << endl;
    cout << "# Directory  : coroutine" << endl;
    cout << "# CASE       : 2" << endl;
    cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << endl;
    cout << "#####################################################" << endl;
    return pass ? 0 : -1;
}
