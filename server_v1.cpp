#include <boost/asio/ip/ttcp.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <string>
#include <unordered_set>

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

bool starts_with(const std::string &str, const std::string &prefix) {
    return str.substr(0, prefix.size()) == prefix;
}

int main() {
    try {
        asio::io_context io_context;

        // Create and bind to the server endpoint
        tcp::acceptor acceptor(io_context, tcp::endpoint(asio::ip::address_v4::any(), 1234));

        std::unordered_set<beast::websocket::stream<tcp::socket>*> clients;

        while (true) {
            // Accept a new connection
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            // Perform WebSocket handshake
            auto ws = new beast::websocket::stream<tcp::socket>(std::move(socket));
            ws->accept();
            clients.insert(ws);

            // Echo back received WebSocket messages or print to console
            beast::flat_buffer buffer;
            std::thread([ws, &clients, &buffer]() {
                try {
                    while (true) {
                        ws->read(buffer);
                        std::string message = beast::buffers_to_string(buffer.data());

                        if (starts_with(message, "SERVER")) {
                            message.erase(0, std::string("SERVER").length());
                            std::cout << "SERVER: " << message << std::endl;
                        } else if (starts_with(message, "CLIENT")) {
                            message.erase(0, std::string("CLIENT").length());
                            std::cout << "CLIENT: " << message << std::endl;
                        } else {
                            std::cout << "UNKNOWN: " << message << std::endl;
                        }

                        // Broadcast the message to all clients
                        for (auto* client : clients) {
                            if (client != ws) {
                                client->write(boost::asio::buffer(message));
                            }
                        }

                        buffer.consume(buffer.size());
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Exception: " << e.what() << std::endl;
                    clients.erase(ws);
                    delete ws;
                }
            }).detach();
        }
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

// In WSL: g++ -o server webSocketServer.cpp

// Navigate to the directory containing index.html and run the following command: python3 -m http.server 8000
