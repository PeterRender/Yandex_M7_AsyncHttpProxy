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

// Пересылает тело серверного HTTP-ответа клиенту порциями
awaitable<void> forwardBodyByChunks(tcp::socket &server_socket, tcp::socket &client_socket, size_t content_length) {
    std::array<char, CHUNK_SIZE> chunk;
    size_t bytes_to_send = content_length;

    while (bytes_to_send > 0) {
        size_t bytes_to_read = std::min(CHUNK_SIZE, bytes_to_send);

        size_t n =
            co_await async_read(server_socket, buffer(chunk.data(), bytes_to_read),
                                transfer_at_least(1),  // не возвращать управление, пока не считается хотя бы 1 байт
                                use_awaitable);

        co_await async_write(client_socket, buffer(chunk.data(), n), use_awaitable);
        bytes_to_send -= n;
    }
}

// Сопрограмма, реализующая прокси-сессию для одного клиента
awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    try {
        // 1. Читаем клиентский HTTP-запрос до конца секции заголовков (delimiter)
        std::string client_req;
        co_await async_read_until(client_socket, dynamic_buffer(client_req), delimiter, use_awaitable);

        // 2. Извлекаем пару {имя хоста, порт} из HTTP-запроса
        auto host_port = findHostPort(client_req);
        if (!host_port.has_value()) {
            std::println(stderr, "Invalid or missing Host header");
            co_return;  // не удалось извлечь хост, завершаем сессию
        }
        auto [host, port] = *host_port;

        // 3. Устанавливаем TCP-соединение с целевым HTTP-сервером
        auto server_socket = co_await connectToServer(io_service, host, port);

        // 4. Пересылаем клиентский HTTP-запрос целевому HTTP-серверу
        co_await async_write(server_socket, buffer(client_req), use_awaitable);

        // 5. Читаем серверный HTTP-ответ до конца секции заголовков (delimiter)
        std::string server_rsp;
        co_await async_read_until(server_socket, dynamic_buffer(server_rsp), delimiter, use_awaitable);

        // 6. Пересылаем заголовки серверного HTTP-ответа клиенту
        co_await async_write(client_socket, buffer(server_rsp), use_awaitable);

        // 7. Извлекаем длину тела (в байтах) из серверного HTTP-ответа
        auto content_length = findContentLength(server_rsp);

        // 8. Пересылаем тело серверного HTTP-ответа (если есть) клиенту по порциям
        if (content_length.has_value() && *content_length > 0) {
            co_await forwardBodyByChunks(server_socket, client_socket, *content_length);
        }
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
