#pragma once

#include <functional>
#include <optional>
#include <string>

// Алиас колбэк-функции, обрабатывающей HTTP-заголовок (true - успех, false - ошибка)
using Callback = std::function<bool(std::string_view header_name, std::string_view header_value)>;

// Алиас пары {имя хоста, порт}
using HostPort = std::pair<std::string, std::string>;

// Обрабатывает все заголовки HTTP-запроса/ответа с помощью колбэк-функции
// (true - все заголовки успешно обработаны, false - есть ошибка обработки заголовка)
bool iterHeaders(std::string_view req, Callback &&callback);

// Извлекает пару {имя хоста, порт} из HTTP-запроса
std::optional<HostPort> findHostPort(std::string_view req);

// Извлекает значение Content-Length из HTTP-ответа
std::optional<size_t> findContentLength(std::string_view rsp);
