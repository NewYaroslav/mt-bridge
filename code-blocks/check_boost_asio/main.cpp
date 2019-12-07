#include <ctime>
#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

std::string make_daytime_string() {
    using namespace std; // For time_t, time and ctime;
    time_t now = time(0);
    return ctime(&now);
}

class tcp_connection :
    public boost::enable_shared_from_this<tcp_connection> {
public:
    typedef boost::shared_ptr<tcp_connection> pointer;

    static pointer create(boost::asio::io_service& io_service) {
        return pointer(new tcp_connection(io_service));
    }

    tcp::socket& socket() {
        return socket_;
    }

    void start() {
        message_ = make_daytime_string();
        /*
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(message_),
            boost::bind(
                &tcp_connection::handle_write, shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
        */
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(read_message_),
                boost::bind(&tcp_connection::handle_read, shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
        std::cout << "read_message_ (2): " << read_message_ << std::endl;
    }

private:
    tcp_connection(boost::asio::io_service& io_service)
        : socket_(io_service) {
    }

    void handle_write(
            const boost::system::error_code& /*error*/,
            size_t /*bytes_transferred*/) {
    }

    void handle_read(
            const boost::system::error_code& /*error*/,
            size_t bytes /*bytes_transferred*/) {
        std::cout << "read " << bytes << std::endl;
        std::cout << "read_message_ " << read_message_ << std::endl;
    }

    tcp::socket socket_;
    std::string message_;
    std::string read_message_;
};

class tcp_server {
public:
    tcp_server(boost::asio::io_context& io_context)
        : io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), 5555)) {
        start_accept();
    }

private:

    void start_accept() {
        new_connection_ = tcp_connection::create(io_context_);

        acceptor_.async_accept(new_connection_->socket(),
            boost::bind(&tcp_server::handle_accept,
                this,
                new_connection_,
                boost::asio::placeholders::error));

    }

    void handle_accept(
            tcp_connection::pointer &new_connection,
            const boost::system::error_code& error) {
        if (!error) {
            new_connection->start();
            new_connection->start();
            new_connection->start();
            new_connection->start();
        }

        //start_accept();
    }

    boost::asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    tcp_connection::pointer new_connection_;
};

std::string read_(tcp::socket & socket) {
       boost::asio::streambuf buf;
       boost::asio::read_until( socket, buf, "\n" );
       std::string data = boost::asio::buffer_cast<const char*>(buf.data());
       return data;
}

void handler_(
      const boost::system::error_code& error,
      std::size_t bytes_transferred) {
    std::cout << "handler_ " <<  error << std::endl;
}

uint32_t read_u32(tcp::socket & socket) {
       //boost::asio::streambuf buf;
       //boost::asio::read_until( socket, buf, "\n" );
       uint8_t data[4];
       boost::asio::async_read(socket, boost::asio::buffer(data, 4), handler_);
       return ((uint32_t*)&data)[0];
}

void send_(tcp::socket & socket, const std::string& message) {
       const std::string msg = message + "\n";
       boost::asio::write( socket, boost::asio::buffer(message) );
}

class con_handler : public boost::enable_shared_from_this<con_handler> {
private:
    tcp::socket sock;
    std::string message="Hello From Server!";
    enum { max_length = 1024 };
    char data[max_length];

public:
    typedef boost::shared_ptr<con_handler> pointer;
    con_handler(boost::asio::io_service& io_service): sock(io_service){}
    // creating the pointer
    static pointer create(boost::asio::io_service& io_service) {
        return pointer(new con_handler(io_service));
    }
    //socket creation
    tcp::socket& socket() {
        return sock;
    }

    void start() {
        /*
        std::thread read_thread([&]() {
            while(true) {
                sock.async_read_some(
                    boost::asio::buffer(data, max_length),
                    boost::bind(&con_handler::handle_read,
                    shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
            }
        });
        read_thread.detach();
        */

        sock.async_read_some(
            boost::asio::buffer(data, max_length),
            boost::bind(&con_handler::handle_read,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));

        /*
        sock.async_write_some(
        boost::asio::buffer(message, max_length),
        boost::bind(&con_handler::handle_write,
        shared_from_this(),
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
        */
    }

    void handle_read(const boost::system::error_code& err, size_t bytes_transferred) {
        if (!err) {
            //std::cout << data << std::endl;
            for(size_t i = 0; i < bytes_transferred; ++i) {
                //if(data[i] == 'E' || data[i] == 'U' || data[i] == 'S' || data[i] == 'D') std::cout << data[i];// << std::endl;
            }
        } else {
            //std::cerr << "error: " << err.message() << std::endl;
            //sock.close();
        }
    }
    void handle_write(const boost::system::error_code& err, size_t bytes_transferred) {
        if (!err) {
            std::cout << "Server sent Hello message!"<< std::endl;
        } else {
            std::cerr << "error: " << err.message() << std::endl;
            sock.close();
        }
    }
};

class Server  {
private:
    tcp::acceptor acceptor_;
    boost::asio::io_context& io_context_;

    void start_accept() {
        // socket
        con_handler::pointer connection = con_handler::create(io_context_);

        // асинхронная операция принятия и ожидания нового соединения.
        acceptor_.async_accept(connection->socket(),
            boost::bind(
                &Server::handle_accept,
                this,
                connection,
                boost::asio::placeholders::error));
    }
public:

    //constructor for accepting connection from client
    Server(boost::asio::io_context& io_context):
        io_context_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), 5555)) {
        start_accept();
    }

    void handle_accept(con_handler::pointer connection, const boost::system::error_code& err) {
        if (!err) {
            std::cout << "new connect!"<< std::endl;
            std::thread connection_thread([connection]() {
                //connection->start();
                while(true) {
                    connection->start();
                    //std::system("pause");
                }
            });
            connection_thread.detach();
        }
        start_accept();
    }
};

int main() {
#if(0)
    try {
        boost::asio::io_service io_service;
        tcp_server server(io_service);
        io_service.run();
        std::cout << "run" << std::endl;
        while(true) {

        }
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
#endif
#if(1)
    boost::asio::io_service io_service;
    tcp::acceptor acceptor_(io_service, tcp::endpoint(tcp::v4(), 5555));
    tcp::socket socket_(io_service);
    acceptor_.accept(socket_);
    uint32_t num = read_u32(socket_);
    std::cout << num << std::endl;
    while(true) {
        //std::string message = read_(socket_);
        //std::cout << message << std::endl;
        uint32_t num = read_u32(socket_);
       // std::cout << num << std::endl;
    }
      //send_(socket_, "Hello From Server!");
      //cout << "Servent sent Hello message to Client!" << endl;
#endif
#if(0)
    try {
        boost::asio::io_service io_service;
        Server server(io_service);
        io_service.run();
    }
    catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
#endif
    return 0;
}
