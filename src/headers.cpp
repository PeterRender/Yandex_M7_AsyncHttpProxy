#include "headers.h"

#include <ranges>
#include <string_view>

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

        // Удаляем пробелы и табуляции в начале значения HTTP-заголовка (могут быть по стандарту HTTP)
        auto value_pos = header_value.find_first_not_of(" \t");  // позиция первого "непробельного" символа значения
        if (value_pos != std::string_view::npos) {
            header_value.remove_prefix(value_pos);
        }  // пустое значение заголовка также валидно

        // Вызываем колбэк для HTTP-заголовка
        callback(header_name, header_value);
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    // code here
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    // code here
}
