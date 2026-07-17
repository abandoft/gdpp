#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gdpp::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

class Registration final {
  public:
    Registration(std::string name, std::function<void()> body) {
        registry().push_back({std::move(name), std::move(body)});
    }
};

template <typename Left, typename Right>
void require_equal(const Left& left, const Right& right, const char* left_text,
                   const char* right_text, const char* file, int line) {
    if (!(left == right)) {
        std::ostringstream message;
        message << file << ':' << line << ": expected " << left_text << " == " << right_text;
        throw std::runtime_error{message.str()};
    }
}

inline void require(bool condition, const char* text, const char* file, int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ':' << line << ": requirement failed: " << text;
        throw std::runtime_error{message.str()};
    }
}

} // namespace gdpp::test

#define GDPP_CONCAT_INNER(left, right) left##right
#define GDPP_CONCAT(left, right) GDPP_CONCAT_INNER(left, right)
#define TEST_CASE(name)                                                                            \
    static void GDPP_CONCAT(test_case_, __LINE__)();                                               \
    static const ::gdpp::test::Registration GDPP_CONCAT(registration_, __LINE__){                  \
        name, GDPP_CONCAT(test_case_, __LINE__)};                                                  \
    static void GDPP_CONCAT(test_case_, __LINE__)()
#define REQUIRE(condition) ::gdpp::test::require((condition), #condition, __FILE__, __LINE__)
#define REQUIRE_EQ(left, right)                                                                    \
    ::gdpp::test::require_equal((left), (right), #left, #right, __FILE__, __LINE__)
