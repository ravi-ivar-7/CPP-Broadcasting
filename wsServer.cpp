#include <boost/asio.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <set>
#include <fstream>
#include <nlohmann/json.hpp>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

std::string getCurrentTime()
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

class WebsocketSession : public std::enable_shared_from_this<WebsocketSession>
{
    beast::websocket::stream<tcp::socket> m_ws;
    beast::flat_buffer m_fbuffer;
    std::set<std::shared_ptr<WebsocketSession>> &m_sessions;
    std::mutex &m_sessions_mutex;

public:
    WebsocketSession(tcp::socket socket, std::set<std::shared_ptr<WebsocketSession>> &sessions, std::mutex &sessions_mutex)
        : m_ws(std::move(socket)), m_sessions(sessions), m_sessions_mutex(sessions_mutex) {}

    void start()
    {
        m_ws.async_accept([self = shared_from_this()](beast::error_code ec)
                          { self->on_accept(ec); });
    }

    void send(const std::string &message)
    {   // writing to sockets
        m_ws.async_write(asio::buffer(message), [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
                         {
            if (ec) {
                std::cerr << "WRITE ERROR: " << ec.message() << std::endl;
            } });
    }

private:
    void on_accept(beast::error_code ec)
    {
        if (ec)
        {
            std::cerr << "ACCEPT ERROR: " << ec.message() << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(m_sessions_mutex);
        m_sessions.insert(shared_from_this());

        fetchJsonAndSendMessages(); // Fetch and send existing messages to the client
        do_read();
    }

    void do_read()
    {
        m_ws.async_read(m_fbuffer, [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
                        { self->on_read(ec, bytes_transferred); });
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        if (ec)
        {
            if (ec == beast::websocket::error::closed)
            {
                // Handle the case where the WebSocket session is closed
                std::cerr << "WebSocket session closed" << std::endl;
                std::lock_guard<std::mutex> lock(m_sessions_mutex);
                m_sessions.erase(shared_from_this());
                return;
            }
            else
            {
                // Handle other read errors
                std::cerr << "READ ERROR: " << ec.message() << std::endl;
                return;
            }
        }

        std::string message_content = beast::buffers_to_string(m_fbuffer.data());

        // Parse the received message as JSON
        json received_json;
        try
        {
            received_json = json::parse(message_content);
        }
        catch (const std::exception &e)
        {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
            return;
        }

        // Extract data from the JSON object
        std::string who = received_json["who"];
        auto time = received_json["time"];
        std::string what = received_json["what"];

        // Remove 'who' and 'time' information from the message
        std::string message = what;

        // Write the message to json file
        writeMessageToJson(time, who, message);

        // Broadcast the message to all clients
        for (auto &session : m_sessions)
        {
            session->send(message_content);
        }

        m_fbuffer.consume(m_fbuffer.size());
        do_read();
    }

    
    void writeMessageToCSV(const std::string &time, const std::string &who, const std::string &what)
    {
        std::ofstream file("data.csv", std::ios::app);
        if (!file.is_open())
        {
            std::cerr << "Cannot open and write to CSV file" << std::endl;
            return;
        }

        file << time << "," << who << "," << what << std::endl;
        file.close();
    }

    void writeMessageToJson(const std::string &time, const std::string &who, const std::string &what)
    { // json is written in wrong format and so when we read it in html file, it gives error and socket is closed(unresolved)
        json message = {
            {"time", time},
            {"who", who},
            {"what", what}};

        std::ofstream file("data.json", std::ios::app);
        if (!file.is_open())
        {
            std::cerr << "Cannot open and write to json file" << std::endl;
            return;
        }

        file << message.dump() << std::endl;
        file.close();
    }

    void fetchJsonAndSendMessages()
    {
        std::ifstream file("data.json");
        if (!file.is_open())
        {
            std::cerr << "Cannot open json file for reading" << std::endl;
            auto time = getCurrentTime();
            json no_message = {{"time", time}, {"who", ""}, {"message", "file DNE."}};
            send(no_message.dump());
            return;
        }

        json messages = json::array();
        std::string line;
        while (std::getline(file, line))
        {
            auto message = json::parse(line);
            messages.push_back(message);
        }
        file.close();

        std::string messages_str = messages.dump();
        send(messages_str);
    }
};

class WebsocketServer
{
    asio::io_context &m_ioc;
    tcp::acceptor m_acceptor;
    std::set<std::shared_ptr<WebsocketSession>> m_sessions;
    std::mutex m_sessions_mutex;

public:
    WebsocketServer(asio::io_context &ioc, tcp::endpoint endpoint)
        : m_ioc(ioc), m_acceptor(ioc, endpoint) {}

    void start()
    {
        do_accept();
    }

private:
    void do_accept()
    {
        m_acceptor.async_accept([this](beast::error_code ec, tcp::socket socket)
                                {
            if (!ec) {
                std::make_shared<WebsocketSession>(std::move(socket), m_sessions, m_sessions_mutex)->start();
            }
            do_accept(); });
    }
};

int main()
{
    try
    {
        asio::io_context io_context;
        tcp::endpoint endpoint(asio::ip::address_v4::any(), 1234);
        WebsocketServer server(io_context, endpoint);
        server.start();
        io_context.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
