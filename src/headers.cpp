#include "headers.h"

#include <ranges>
#include <string_view>

#include <boost/url/grammar.hpp>   // для ci_compare
#include <boost/url/parse.hpp>     // для parse_authority
#include <boost/url/url_view.hpp>  // для результата парсинга parse_authority

using namespace std::string_view_literals;

using Callback = std::function<void(std::string_view, std::string_view)>;

// Извлекает из HTTP-запроса/ответа заголовки в формате {имя, значение} и передает их в колбэк
void iterHeaders(std::string_view req, Callback &&callback) {
    // Разбиваем HTTP-запрос/ответ на строки
    auto all_lines = req | std::views::split(std::string_view("\r\n"));

    // Обрабатываем строки до первой пустой строки
    for (auto line : all_lines) {
        std::string_view line_str(line.begin(), line.end());

        // Пустая строка означает конец секции заголовков (по стандарту - это "\r\n\r\n")
        if (line_str.empty()) {
            break;
        }

        // Пропускаем строки без двоеточия (первая строка запроса/статуса, невалидные заголовки)
        auto colon_pos = line_str.find(':');
        if (colon_pos == std::string_view::npos) {
            continue;
        }

        // Извлекаем имя и значение HTTP-заголовка
        std::string_view header_name = line_str.substr(0, colon_pos);
        std::string_view header_value = line_str.substr(colon_pos + 1);

        // Обрезаем ведущие и хвостовые пробелы/табуляции в значении HTTP-заголовка (могут быть по стандарту HTTP)
        auto start = header_value.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            header_value = "";  // значение HTTP-заголовка состоит только из пробелов (валидно по стандарту)
        } else {
            auto end = header_value.find_last_not_of(" \t");
            header_value = header_value.substr(start, end - start + 1);
        }

        // Вызываем колбэк для HTTP-заголовка
        callback(header_name, header_value);
    }
}

// Извлекает имя хоста и номер порта из HTTP-запроса
std::optional<std::pair<std::string, std::string>> findHostPort(std::string_view req) {
    std::string host_desc;  // дескриптор хоста в формате "имя_хоста:порт"
    int host_cnt = 0;       // запрос может иметь только один заголовок Host (по стандарту HTTP)

    // Ищем заголовок Host в HTTP-запросе
    iterHeaders(req, [&](std::string_view header_name, std::string_view header_value) {
        // Проверяем, что найден заголовок Host (без учёта регистра, по стандарту HTTP)
        if (boost::urls::grammar::ci_compare(header_name, "Host") == 0) {
            host_cnt++;
            if (host_cnt == 1) {
                host_desc = header_value;  // запоминаем только первый дескриптор хоста
            }
            // последующие дескрипторы хостов (если есть) просто считаем
        }
    });

    // Дескриптор хоста должен быть одним и непустым
    if ((host_cnt != 1) || host_desc.empty()) {
        return std::nullopt;  // невалидный HTTP-запрос
    }

    // Извлекаем имя хоста и порт из дескриптора хоста
    auto parsed = boost::urls::parse_authority(host_desc);
    if (!parsed) {
        return std::nullopt;  // невалидный формат хоста
    }

    // Проверяем, что имя хоста непустое
    std::string host_name(parsed->host());
    if (host_name.empty()) {
        return std::nullopt;
    }

    // Проверяем, что порт задан после разделителя ":"
    std::string port = "80";  // порт по умолчанию (по стандарту HTTP)
    if (parsed->has_port()) {
        std::string_view port_view = parsed->port();
        if (port_view.empty()) {
            return std::nullopt;  // порт ожидается, но не задан
        }
        port = port_view;
    }

    // Возвращаем имя хоста и порт
    return std::pair{std::move(host_name), std::move(port)};
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    return std::nullopt;  // заглушка
}
