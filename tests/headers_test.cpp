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

// Собирает распарсенные HTTP-заголовки в вектор пар "имя, значение"
std::vector<std::pair<std::string, std::string>> gatherHeaders(std::string_view req) {
    std::vector<std::pair<std::string, std::string>> result;
    iterHeaders(req, [&](std::string_view name, std::string_view value) { result.emplace_back(name, value); });
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
INSTANTIATE_TEST_SUITE_P(IterHeaders, IterHeadersTest, ::testing::ValuesIn(iterHeadersTests),
                         [](const testing::TestParamInfo<IterHeadersTestCase> &info) { return info.param.name; });

TEST(findHostPort, Simple) {
    // code here
}

TEST(findHostPort, NoHost) {
    // code here
}

TEST(findContentLength, Simple) {
    // code here
}

TEST(findContentLength, NoContentLength) {
    // code here
}
