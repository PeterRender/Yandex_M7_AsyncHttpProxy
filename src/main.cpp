#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <print>
#include <string_view>
#include <sys/syscall.h>
#include <unistd.h>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::dynamic_buffer;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

// Разделитель между HTTP-заголовками и телом сообщения (по стандрату два CRLF)
constexpr std::string_view delimiter = "\r\n\r\n";

// Max размер секции заголовков HTTP-запроса/ответа (в байтах), обрабатываемый прокси-сервером
constexpr size_t MAX_HEADERS_SIZE = 65536;

// Размер порции для чтения тела ответа, в байтах
constexpr size_t CHUNK_SIZE = 8192;

// Устанавливает TCP-соединение с целевым HTTP-сервером
awaitable<tcp::socket> connectToServer(io_service &io_service, const std::string &host, const std::string &port) {
    tcp::socket server_socket(io_service);  // пустой сокет
    tcp::resolver dns(io_service);          // DNS-клиент

    try {
        // Запрашиваем у DNS-клиента все IP-адреса для хоста (у одного домена может быть несколько IP)
        auto ip_list = co_await dns.async_resolve(host, port, use_awaitable);

        // Пытаемся подключиться к хосту по первому IP (при неудаче пробуем следующие IP, потом - исключение)
        co_await async_connect(server_socket, ip_list, use_awaitable);

        co_return std::move(server_socket);  // соединение успешно установлено
    } catch (const std::exception &e) {
        std::println(stderr, "Failed to connect to {}:{}: {}", host, port, e.what());
        throw;  // пробрасываем дальше
    }
}

// Пересылает полное HTTP-сообщение (заголовки + тело) от источника к получателю
awaitable<void> forwardHttpMessage(tcp::socket &src_socket, tcp::socket &dst_socket, const std::string &src_headers,
                                   size_t src_headers_length) {
    // Пересылаем получателю только заголовки от источника
    co_await async_write(dst_socket, buffer(src_headers.data(), src_headers_length), use_awaitable);

    // Извлекаем длину тела HTTP-сообщения (в байтах) из источника
    auto content_length = findContentLength(src_headers);

    // Проверяем, есть ли тело в HTTP-сообщении
    if (!content_length.has_value() || (*content_length == 0)) {
        co_return;  // тела нет, завершаем пересылку сообщения
    }

    // Вначале пересылаем часть тела, которую прочитали вместе с заголовками (не может превышать длины тела)
    size_t already_read = std::min(src_headers.size() - src_headers_length, *content_length);
    if (already_read > 0) {
        co_await async_write(dst_socket, buffer(src_headers.data() + src_headers_length, already_read), use_awaitable);
    }

    // Проверяем длину оставшейся части ("хвоста") тела
    size_t remaining = *content_length - already_read;
    if (remaining == 0) {
        co_return;  // все тело уже получено, завершаем пересылку сообщения
    }

    // Читаем и отправляем "хвост" тела по порциям
    std::array<char, CHUNK_SIZE> chunk;
    while (remaining > 0) {
        size_t bytes_to_read = std::min(CHUNK_SIZE, remaining);
        size_t n =
            co_await async_read(src_socket, buffer(chunk.data(), bytes_to_read),
                                transfer_at_least(1),  // не возвращать управление, пока не считается хотя бы 1 байт
                                use_awaitable);
        co_await async_write(dst_socket, buffer(chunk.data(), n), use_awaitable);
        remaining -= n;
    }
}

// Сопрограмма, реализующая прокси-сессию для одного клиента
awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    // Выводим id процесса и id текущего потока (должны совпадать)
    static auto pid = getpid();              // id процесса (один раз при первом вызове)
    auto current_tid = syscall(SYS_gettid);  // id текущего потока
    std::println("PID: {}, TID: {}", pid, current_tid);

    try {
        // 1. Читаем клиентский HTTP-запрос до конца секции заголовков (может захватить часть тела после разделителя)
        std::string client_req;
        size_t req_headers_length = co_await async_read_until(
            client_socket, dynamic_buffer(client_req, MAX_HEADERS_SIZE), delimiter, use_awaitable);

        // 2. Извлекаем пару {имя хоста, порт} из HTTP-запроса
        auto host_port = findHostPort(client_req);
        if (!host_port.has_value()) {
            std::println(stderr, "Invalid or missing Host header");
            co_return;  // не удалось извлечь хост, завершаем сессию
        }
        auto [host, port] = *host_port;

        // 3. Проверка на зацикливание (только для локальных подключений)
        auto local_endpoint = client_socket.local_endpoint();
        int proxy_port = local_endpoint.port();
        if ((host == "127.0.0.1" || host == "localhost") && port == std::to_string(proxy_port)) {
            std::println(stderr, "Recursive proxy request detected, aborting session");
            co_return;
        }

        // 4. Устанавливаем TCP-соединение с целевым HTTP-сервером
        auto server_socket = co_await connectToServer(io_service, host, port);

        // 5. Пересылаем полный HTTP-запрос клиента серверу (заголовки + тело)
        co_await forwardHttpMessage(client_socket, server_socket, client_req, req_headers_length);

        // 6. Читаем серверный HTTP-ответ до конца секции заголовков (может захватить часть тела после разделителя)
        std::string server_rsp;
        size_t rsp_headers_length = co_await async_read_until(
            server_socket, dynamic_buffer(server_rsp, MAX_HEADERS_SIZE), delimiter, use_awaitable);

        // 7. Пересылаем полный HTTP-ответ сервера клиенту (заголовки + тело)
        co_await forwardHttpMessage(server_socket, client_socket, server_rsp, rsp_headers_length);
    } catch (const std::exception &e) {
        std::println(stderr, "Session failed: {}", e.what());
    }

    // RAII закроет клиентский и серверный сокеты при выходе из области видимости
}

// Класс HTTP прокси-сервера
class Server {
public:
    // Конструктор сервера
    Server(io_service &io_service, short port)
        : io_service_(io_service),  // запоминаем ссылку на диспетчера ввода-вывода Asio
          acceptor_(                // cоздаем акцептор для приема TCP-соединений (слушаем IPv4 на заданном порту)
              io_service, tcp::endpoint(tcp::v4(), port)),
          socket_(io_service)  // cоздаем пустой сокет для будущих подключений
    {
        do_accept();  // запускаем асинхронное ожидание первого клиента
    }

private:
    // Запускает асинхронное ожидание входящего TCP-соединения
    void do_accept() {
        // Запускаем асинхронный прием TCP-соединения
        acceptor_.async_accept(socket_, [this](error_code ec) {
            if (!ec)  // клиент успешно подключился
            {
                // Запускаем прокси-сессию для обслуживания клиента (сокет передаем в сессию, сессию не ждем)
                co_spawn(io_service_, session(std::move(socket_), io_service_), boost::asio::detached);

            } else {  // ошибка - выводим лог
                std::println(stderr, "Failed to accept connection: {}", ec.message());
            }

            socket_ = tcp::socket(io_service_);  // новый сокет для следующего клиента
            do_accept();                         // ожидаем следующего клиента
        });
    }

    io_service &io_service_;  // ссылка на диспетчер ввода-вывода Asio
    tcp::acceptor acceptor_;  // акцептор для приема входящих TCP-соединений
    tcp::socket socket_;      // сокет для текущего клиента
};

int main(int argc, char *argv[]) {
    try {
        // Проверяем, что задан TCP-порт в командной строке
        if (argc != 2) {
            std::cerr << "Usage: proxy_server <listen_port>\n";
            return 1;
        }

        // Создаем диспетчер ввода-вывода Asio (1 поток выполнения)
        io_service io_service(1);

        // Создаем сервер на заданном TCP-порту
        Server server(io_service, std::atoi(argv[1]));

        // Запускаем цикл обработки асинхронных событий Asio
        // Здесь основной поток блокируется, пока не будут обработаны все асинхронные операции (или остановка сервера)
        std::println("Proxy server started on port {}", argv[1]);
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
