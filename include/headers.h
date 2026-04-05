#pragma once

#include <functional>
#include <optional>
#include <string>

using Callback = std::function<void(std::string_view, std::string_view)>;

// Извлекает из HTTP-запроса/ответа заголовки в формате {имя, значение} и передает их в колбэк
void iterHeaders(std::string_view req, Callback &&callback);

std::pair<std::string, std::string> findHostPort(std::string_view req);

std::optional<size_t> findContentLength(std::string_view rsp);
