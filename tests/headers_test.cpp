#include <gtest/gtest.h>

#include "headers.h"

#include <utility>
#include <vector>

// Структура, описывающая тестовый случай для iterHeaders
struct IterHeadersTestCase {
    std::string name;                                           // имя теста
    std::string_view request;                                   // HTTP-запрос/ответ
    std::vector<std::pair<std::string, std::string>> expected;  // ожидаемые заголовки
};

// Структура, описывающая тестовый случай для findHostPort
struct FindHostPortTestCase {
    std::string name;           // имя теста
    std::string_view request;   // HTTP-запрос
    bool expected_result;       // ожидаемый результат (успех/ошибка)
    std::string expected_host;  // ожидаемый хост (если успех)
    std::string expected_port;  // ожидаемый порт (если успех)
};

// Тестовые данные для iterHeaders
const std::vector<IterHeadersTestCase> iterHeadersTests = {
    {.name = "Empty", .request = "GET / HTTP/1.1\r\n\r\n", .expected = {}},
    {.name = "SkipRequestLine",
     .request = "Host: yandex.ru\r\n\r\n",  // нет строки запроса
     .expected = {{"Host", "yandex.ru"}}},
    {.name = "SingleHeader",
     .request = "GET / HTTP/1.1\r\nHost: yandex.ru\r\n\r\n",
     .expected = {{"Host", "yandex.ru"}}},
    {.name = "MultipleHeaders",
     .request = "GET / HTTP/1.1\r\nHost: yandex.ru\r\nUser-Agent: curl/7.68.0\r\nAccept: */*\r\n\r\n",
     .expected = {{"Host", "yandex.ru"}, {"User-Agent", "curl/7.68.0"}, {"Accept", "*/*"}}},
    {.name = "MultipleSameHeaders",
     .request = "GET / HTTP/1.1\r\nSet-Cookie: session=qwerty12345\r\nSet-Cookie: user=petr\r\n\r\n",
     .expected = {{"Set-Cookie", "session=qwerty12345"}, {"Set-Cookie", "user=petr"}}},
    {.name = "HeadersWithSpaces",
     .request = "GET / HTTP/1.1\r\nHost:   \tyandex.ru\r\nContent-Type:  text/html\r\n\r\n",
     .expected = {{"Host", "yandex.ru"}, {"Content-Type", "text/html"}}},
    {.name = "MissedColon",
     .request = "GET / HTTP/1.1\r\nBadHeader\r\nHost: yandex.ru\r\n\r\n",
     .expected = {{"Host", "yandex.ru"}}},
    {.name = "EmptyHeaderValue",
     .request = "GET / HTTP/1.1\r\nReferer:\r\nHost: yandex.ru\r\n\r\n",
     .expected = {{"Referer", ""}, {"Host", "yandex.ru"}}}};

// Тестовые данные для findHostPort
const std::vector<FindHostPortTestCase> findHostPortTests = {
    {"Simple", "GET / HTTP/1.1\r\nHost: ya.ru\r\n\r\n", true, "ya.ru", "80"},
    {"WithPort", "GET / HTTP/1.1\r\nHost: yandex.ru:8080\r\n\r\n", true, "yandex.ru", "8080"},
    {"WithSpaces", "GET / HTTP/1.1\r\nHost:   ya.ru:80  \r\n\r\n", true, "ya.ru", "80"},
    {"LowercaseHost", "GET / HTTP/1.1\r\nhost: yandex.ru\r\n\r\n", true, "yandex.ru", "80"},
    {"MixedCaseHost", "GET / HTTP/1.1\r\nHoSt: Ya.Ru\r\n\r\n", true, "Ya.Ru", "80"},
    {"IPv6Address", "GET / HTTP/1.1\r\nHost: [::1]:8080\r\n\r\n", true, "[::1]", "8080"},
    {"IPv6Yandex", "GET / HTTP/1.1\r\nHost: [2a02:6b8::1]:8080\r\n\r\n", true, "[2a02:6b8::1]", "8080"},

    // Ошибочные случаи
    {"NoHostHeader", "GET / HTTP/1.1\r\nUser-Agent: curl\r\n\r\n", false, "", ""},
    {"EmptyHostHeader", "GET / HTTP/1.1\r\nHost:\r\n\r\n", false, "", ""},
    {"MultipleHosts", "GET / HTTP/1.1\r\nHost: ya.ru\r\nHost: yandex.ru\r\n\r\n", false, "", ""},
    {"InvalidPortFormat", "GET / HTTP/1.1\r\nHost: ya.ru:abc\r\n\r\n", false, "", ""},
    {"InvalidEmptyPort", "GET / HTTP/1.1\r\nHost: ya.ru:\r\n\r\n", false, "", ""},
    {"NamelessHostWithPort", "GET / HTTP/1.1\r\nHost: :8080\r\n\r\n", false, "", ""},
};

// Собирает распарсенные HTTP-заголовки в вектор пар "имя, значение"
std::vector<std::pair<std::string, std::string>> gatherHeaders(std::string_view req) {
    std::vector<std::pair<std::string, std::string>> result;
    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        result.emplace_back(name, value);
        return true;  // продолжаем итерации
    });
    return result;
}

// Параметрический тест функции iterHeaders
class IterHeadersTest : public ::testing::TestWithParam<IterHeadersTestCase> {};

TEST_P(IterHeadersTest, ParseHeaders) {
    const auto &test = GetParam();
    auto headers = gatherHeaders(test.request);
    EXPECT_EQ(headers.size(), test.expected.size());
    for (size_t i = 0; i < headers.size(); ++i) {
        EXPECT_EQ(headers[i].first, test.expected[i].first)
            << "Test: " << test.name << ", header name mismatch at index " << i;
        EXPECT_EQ(headers[i].second, test.expected[i].second)
            << "Test: " << test.name << ", header value mismatch at index " << i;
    }
}

// Набор тестов для функции iterHeaders
INSTANTIATE_TEST_SUITE_P(IterHeadersTestSuite, IterHeadersTest, ::testing::ValuesIn(iterHeadersTests),
                         [](const testing::TestParamInfo<IterHeadersTestCase> &info) { return info.param.name; });

// Параметрический тест функции findHostPort
class FindHostPortTest : public ::testing::TestWithParam<FindHostPortTestCase> {};

TEST_P(FindHostPortTest, FindHostPort) {
    const auto &test = GetParam();
    auto result = findHostPort(test.request);

    if (test.expected_result) {
        ASSERT_TRUE(result.has_value()) << "Test: " << test.name;
        auto [host, port] = *result;
        EXPECT_EQ(host, test.expected_host) << "Test: " << test.name;
        EXPECT_EQ(port, test.expected_port) << "Test: " << test.name;
    } else {
        EXPECT_FALSE(result.has_value()) << "Test: " << test.name;
    }
}

// Набор тестов для функции findHostPort
INSTANTIATE_TEST_SUITE_P(FindHostPortTestSuite, FindHostPortTest, ::testing::ValuesIn(findHostPortTests),
                         [](const testing::TestParamInfo<FindHostPortTestCase> &info) { return info.param.name; });

TEST(findContentLength, Simple) {
    // code here
}

TEST(findContentLength, NoContentLength) {
    // code here
}
