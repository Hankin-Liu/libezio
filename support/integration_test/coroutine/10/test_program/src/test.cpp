/****************************************************************************************
 * @file test.cpp
 * @brief Coroutine test 10: generator<T>
 *
 * Tests the minimal generator coroutine type.  A generator yields
 * values via co_yield; the consumer calls next() and reads value().
 *
 * Covers:
 *   - generator<T> (co_yield)
 *   - generator::next() / value()
 *   - Generator lifecycle
 ***************************************************************************************/

#include <iostream>
#include <cassert>
#include "coroutine/coroutine_service.h"
using namespace std;

// Generator: yields 0 .. n-1
ezio::coroutine::generator<int32_t> count_up(int32_t n)
{
    for (int32_t i = 0; i < n; ++i) {
        co_yield i;
    }
    co_return;
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    auto g = count_up(5);
    int32_t expected = 0;
    int32_t count = 0;

    while (g.next()) {
        int32_t v = g.value();
        std::cout << "GENERATOR: " << v << std::endl;
        if (v != expected) {
            std::cerr << "MISMATCH: got " << v << " expected " << expected << std::endl;
            break;
        }
        ++expected;
        ++count;
    }

    bool pass = (count == 5 && expected == 5);
    std::cout << "#####################################################" << std::endl;
    std::cout << "# Directory  : coroutine" << std::endl;
    std::cout << "# CASE       : 10" << std::endl;
    std::cout << "# RESULT     : " << (pass ? "PASS" : "FAILED") << std::endl;
    std::cout << "#####################################################" << std::endl;
    return pass ? 0 : -1;
}
