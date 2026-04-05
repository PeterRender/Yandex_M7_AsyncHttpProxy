#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
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

// Сопрограмма, реализующая прокси-сессию для одного клиента
awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    std::println("The session is running for the client");  // заглушка
    co_return;
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
        // Запускаем асинхронный прием TCP-соединения (в сокет пишется инфо о клиенте)
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
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }

        // Создаем диспетчер ввода-вывода Asio (1 поток выполнения)
        io_service io_service(1);

        // Создаем сервер на заданном TCP-порту
        Server server(io_service, std::atoi(argv[1]));

        // Запускаем цикл обработки асинхронных событий Asio
        // Здесь основной поток блокируется, пока не будут обработаны все асинхронные операции (или остановка сервера)
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
