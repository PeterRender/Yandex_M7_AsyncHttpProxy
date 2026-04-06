#include "headers.h"

#include <charconv>  // для std::from_chars
#include <ranges>
#include <string_view>

#include <boost/url/grammar.hpp>   // для ci_compare
#include <boost/url/parse.hpp>     // для parse_authority
#include <boost/url/url_view.hpp>  // для результата парсинга parse_authority

using namespace std::string_view_literals;

// Обрабатывает все заголовки HTTP-запроса/ответа с помощью колбэк-функции
// (true - все заголовки успешно обработаны, false - есть ошибка обработки заголовка)
bool iterHeaders(std::string_view req, Callback &&callback) {
    // Разбиваем HTTP-запрос/ответ на строки
    auto all_lines = req | std::views::split("\r\n"sv);

    // Обрабатываем строки до первой пустой строки
    for (auto line : all_lines) {
        std::string_view line_str(line.begin(), line.end());

        // Пустая строка означает конец секции заголовков (по стандарту - это "\r\n\r\n")
        if (line_str.empty()) {
            break;  // нет строк для обработки, останавливаем итерацию
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

        // Обрабатываем заголовок с помощью колбэк-функции
        if (!callback(header_name, header_value)) {
            return false;  // ошибка обработки заголовка, останавливаем итерацию
        }
    }

    return true;  // все заголовки успешно обработаны
}

// Извлекает пару {имя хоста, порт} из HTTP-запроса
std::optional<HostPort> findHostPort(std::string_view req) {
    std::optional<HostPort> result;  // по умолчанию std::nullopt (хост еще не найден)

    // Лямбда для обработки заголовка Host
    auto process_host = [&](std::string_view header_name, std::string_view header_value) -> bool {
        // Проверяем, что у заголовка имя Host (без учета регистра, по стандарту HTTP)
        if (boost::urls::grammar::ci_compare(header_name, "Host") != 0) {
            return true;  // это не Host, продолжаем итерации
        }

        // Проверяем, что заголовок Host один (по стандарту HTTP) и имеет значение
        if (result.has_value() || header_value.empty()) {
            return false;  // невалидный HTTP-запрос, остановка итерации
        }

        // Парсим значение заголовка Host (формат "host:port")
        auto parsed = boost::urls::parse_authority(header_value);
        if (!parsed) {
            return false;  // невалидный формат хоста, остановка итерации
        }

        // Извлекаем имя хоста
        std::string host_name(parsed->host());
        if (host_name.empty()) {
            return false;  // пустое имя хоста, остановка итерации
        }

        // Извлекаем порт (по умолчанию "80", стандарт HTTP)
        std::string port = "80";
        if (parsed->has_port()) {
            std::string_view port_view = parsed->port();
            if (port_view.empty()) {
                return false;  // порт ожидается, но не задан - остановка итерации
            }
            port = port_view;
        }

        result = std::pair{std::move(host_name), std::move(port)};  // успех, сохраняем результат
        return true;                                                // продолжаем итерации, чтобы проверить дубликаты
    };

    // Запускаем итерацию по заголовкам
    if (!iterHeaders(req, process_host)) {
        return std::nullopt;  // ошибка обработки заголовка (дубликат или невалидный формат)
    }

    // Возвращаем {имя хоста, порт}, если Host найден и валиден, иначе - std::nullopt
    return result;
}

// Извлекает значение Content-Length из HTTP-ответа
std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;  // по умолчанию std::nullopt (Content-Length еще не найден)

    // Лямбда для обработки заголовка Content-Length
    auto process_content_length = [&](std::string_view header_name, std::string_view header_value) -> bool {
        // Проверяем, что у заголовка имя Content-Length (без учета регистра)
        if (boost::urls::grammar::ci_compare(header_name, "Content-Length") != 0) {
            return true;  // это не Content-Length, продолжаем итерации
        }

        // Проверяем, что заголовок Content-Length один (по стандарту HTTP) и имеет значение
        if (result.has_value() || header_value.empty()) {
            return false;  // невалидный HTTP-ответ, остановка итерации
        }

        // Преобразуем значение Content-Length в число
        size_t length = 0;  // беззнаковый, чтобы std::from_chars отбраковывала "-" значения
        const char *start = header_value.data();
        const char *end = start + header_value.size();
        auto [ptr, ec] = std::from_chars(start, end, length);

        // Проверяем, что парсинг прошел без ошибок и нет лишних символов после цифр
        if (ec != std::errc() || ptr != end) {
            return false;  // невалидное значение Content-Length, остановка итерации
        }

        result = length;  // успех, сохраняем результат
        return true;      // продолжаем итерации, чтобы проверить дубликаты
    };

    // Запускаем итерацию по заголовкам
    if (!iterHeaders(rsp, process_content_length)) {
        return std::nullopt;  // ошибка обработки заголовка (дубликат или невалидное значение)
    }

    // Возвращаем длину, если Content-Length найден и валиден, иначе - std::nullopt
    return result;
}
