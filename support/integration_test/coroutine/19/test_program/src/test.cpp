/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 19: generator<T> with multiple co_yield + exhaustion
 *
 * Creates a generator coroutine that yields a sequence of values,
 * iterates via next()/value() until exhausted. Verifies all expected
 * values and sum.
 *
 * Covers:
 *   - generator<int> with multiple co_yield
 *   - next() loop until exhaustion
 *   - value() access after each resume
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "coroutine/awaitable.h"
using namespace std;

std::atomic<int> total_sum{0};

ezio::coroutine::generator<int> number_sequence(
    ezio::coroutine::coroutine_service& cs,
    int start, int count)
{
    (void)cs;
    for (int i = 0; i < count; ++i) {
        co_yield start + i;
    }
    co_return;
}

int main(int argc, char** argv)
{
    auto evt_service_ptr = std::make_shared<ezio::event::event_service>();
    ezio::event::poll_param param;
    if (argc > 1) param.poll_type_ = std::stoi(argv[1]);
    auto ret = evt_service_ptr->open(param);
    assert(ret == 0);

    ezio::coroutine::coroutine_service cs(evt_service_ptr.get());

    std::thread evt_thread([&evt_service_ptr]() {
        evt_service_ptr->start_loop();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Use generator directly (not inside coroutine, since generator is sync)
    auto gen = number_sequence(cs, 1, 5);
    int sum = 0;
    int count = 0;
    while (gen.next()) {
        int val = gen.value();
        std::cout << "GENERATOR: yield " << val << std::endl;
        sum += val;
        ++count;
    }
    std::cout << "GENERATOR: sum=" << sum << " count=" << count << std::endl;
    bool ok = (sum == 15 && count == 5);
    total_sum.store(ok ? 1 : 0);

    evt_service_ptr->close();
    evt_thread.join();

    bool pass = total_sum.load() == 1;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 19" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
