/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 27: run_in_loop from non-IO thread
 *
 * Submits a job via cs.run_in_loop() from a secondary thread.
 * Verifies the job executes on the event loop thread.
 *
 * Covers:
 *   - cs.run_in_loop() from external thread
 *   - Job execution on event loop
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
// No need for awaitable.h
using namespace std;

std::atomic<bool> job_done{false};
std::atomic<std::thread::id> job_thread_id{};

int main(int argc, char** argv)
{
    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = std::stoi(argv[1]);
    auto ret = evt_service_ptr->open(param);
    assert(ret == 0);

    ezio::coroutine::coroutine_service cs(evt_service_ptr.get());
    std::thread::id evt_thread_id{};

    std::thread evt_thread([&evt_service_ptr, &evt_thread_id]() {
        evt_thread_id = std::this_thread::get_id();
        evt_service_ptr->start_loop();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Submit a job via run_job directly (not cs.run_in_loop which returns
    // a unique_ptr that must be co_awaited inside a coroutine).
    evt_service_ptr->run_job([&]() {
        job_thread_id.store(std::this_thread::get_id());
        job_done.store(true);
        std::cout << "RUN_IN_LOOP_JOB: executed on event thread" << std::endl;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    evt_service_ptr->close();
    evt_thread.join();

    bool pass = job_done.load();
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 27" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
