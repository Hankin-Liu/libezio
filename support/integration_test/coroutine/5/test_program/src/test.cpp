/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 5: run_in_loop
 *
 * Uses cs.run_in_loop() to post a job onto the event loop thread and
 * co_await its completion.  Verifies that the job executes and the
 * coroutine resumes.
 *
 * Covers:
 *   - cs.run_in_loop() / async_result
 *   - cross-context job dispatch
 *   - async_result with ready flag
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "thread/event_thread_pool.h"
using namespace std;
using namespace ezio::thread;

std::atomic<bool> job_done{false};
std::atomic<int> job_value{0};

ezio::coroutine::task<void> do_run_in_loop(ezio::coroutine::coroutine_service& cs,
    ezio::event::event_service* evt_svc)
{
    auto ar = cs.run_in_loop([]() {
        job_value.store(42);
        std::cout << "JOB: executed on event loop thread" << std::endl;
    });
    int32_t ret = co_await *ar;
    std::cout << "JOB_RESULT: ret=" << ret << std::endl;
    job_done.store(true);
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

    // Start event loop first to ensure epoll is inited before spawning
    thread_pool_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    cs.spawn(do_run_in_loop(cs, evt_service_ptr));

    thread_pool_ptr->join();
    bool pass = job_done.load() && job_value.load() == 42;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 5" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    fflush(stdout);
    _exit(pass ? 0 : -1);
    return pass ? 0 : -1;
}
