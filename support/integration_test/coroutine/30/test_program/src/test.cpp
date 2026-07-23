/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 30: generator<T> filtered even sequence
 *
 * Creates a generator that yields only even numbers from a range.
 * Iterates via next()/value().  Tests conditional logic in generators.
 *
 * Covers:
 *   - generator co_yield with conditional selection
 *   - Multiple yield points with gap skipping
 *   - End-of-sequence detection
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include <memory>
#include <atomic>
#include <thread>
#include "coroutine/coroutine_service.h"
#include "coroutine/awaitable.h"
using namespace std;

std::atomic<int> gen_result{0};

ezio::coroutine::generator<int> even_numbers(
    ezio::coroutine::coroutine_service& cs,
    int limit)
{
    (void)cs;
    for (int i = 1; i <= limit; ++i) {
        if (i % 2 == 0) {
            co_yield i;
        }
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

    auto gen = even_numbers(cs, 10);
    int sum = 0;
    int count = 0;
    int expected[] = {2, 4, 6, 8, 10};
    bool all_expected = true;

    int idx = 0;
    while (gen.next()) {
        int val = gen.value();
        std::cout << "EVEN_GEN: yield " << val << std::endl;
        if (idx < 5 && val != expected[idx]) {
            all_expected = false;
        }
        sum += val;
        ++count;
        ++idx;
    }

    bool ok = (count == 5 && sum == 30 && all_expected);
    std::cout << "EVEN_GEN: count=" << count << " sum=" << sum
              << " verify=" << (ok ? "PASS" : "FAIL") << std::endl;
    gen_result.store(ok ? 1 : 0);

    evt_service_ptr->close();
    evt_thread.join();

    bool pass = gen_result.load() == 1;
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 30" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
