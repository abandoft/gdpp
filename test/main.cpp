#include "support/test.hpp"

#include <exception>
#include <iostream>

int main() {
    std::size_t failures = 0;
    for (const auto& test_case : gdpp::test::registry()) {
        try {
            test_case.body();
            std::cout << "[pass] " << test_case.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[fail] " << test_case.name << "\n       " << error.what() << '\n';
        }
    }
    std::cout << gdpp::test::registry().size() - failures << "/" << gdpp::test::registry().size()
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
