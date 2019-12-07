#ifndef METATRADER_BRIDGE_HPP_INCLUDED
#define METATRADER_BRIDGE_HPP_INCLUDED

#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <ctime>
#include <cstring>
//#include <xtime.hpp>

namespace metatrader_bridge {
    using boost::asio::ip::tcp;

    class MetatraderBridge {
    private:
//------------------------------------------------------------------------------
        /** \brief Класс соединения с метатрейдером
         */
        class MtConnection :
            public boost::enable_shared_from_this<MtConnection> {
        private:
            tcp::socket sock;           /**< Сокет */
            enum {max_length = 1024};   /**< Максимальное количество данных */
            uint8_t data[max_length];   /**< Принимаемые данные */
            uint8_t buffer[max_length]; /**< Временный буфер */
            uint32_t buffer_size = 0;   /**< Размер временного буфера */

            enum {symbol_name_length = 32}; /**< Максимальная длина имени символа */
            uint32_t num_symbols = 0;
            std::vector<std::string> symbols;

            enum {
                START_STATE = 0,
                GETTING_NUM_SYMBOLS_STATE = 1,
                GETTING_SYMBOLS_NAME_STATE = 2,
                RECEIVING_DATA_STATE = 3,
            };
            uint32_t state = START_STATE;

        public:
            typedef boost::shared_ptr<MtConnection> pointer;

            MtConnection(boost::asio::io_service& io_service): sock(io_service){}

            // creating the pointer
            static pointer create(boost::asio::io_service& io_service) {
                return pointer(new MtConnection(io_service));
            }

            //socket creation
            tcp::socket& socket() {
                return sock;
            }

            /** \brief Читать данные
             * \return код ошибки
             */
            int read_data() {
                sock.async_read_some(
                    boost::asio::buffer(data, max_length),
                    boost::bind(
                        &MtConnection::handle_read,
                        shared_from_this(),
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
            }

            /** \brief Обработчик события чтения данных
             * \param
             * \param
             */
            void handle_read(
                    const boost::system::error_code& err,
                    size_t bytes_transferred) {
                if(!err) {
                    /* если нет ошибки, принимаем данные */
                    size_t index = 0;
                    while(index < bytes_transferred) {
                        /* реализуем конечный автомат */
                        switch(state) {
                        case START_STATE:
                            symbols.clear();
                            num_symbols = 0;
                        /* получаем количество символов */
                        case GETTING_NUM_SYMBOLS_STATE:
                            while(index < bytes_transferred) {
                                buffer[buffer_size++] = data[index++];
                                std::cout << "c " << (int)data[index - 1] << std::endl;
                                if(buffer_size == sizeof(uint32_t)) {
                                    buffer_size = 0;
                                    uint32_t *ptr = (uint32_t*)buffer;
                                    num_symbols = ptr[0];
                                    std::cout << "num_symbols " << num_symbols << std::endl;
                                    state = GETTING_SYMBOLS_NAME_STATE;
                                    break;
                                }
                            }
                            break;
                        /* получаем имена символов */
                        case GETTING_SYMBOLS_NAME_STATE:
                            while(index < bytes_transferred) {
                                buffer[buffer_size++] = data[index++];
                                if(buffer_size == symbol_name_length) {
                                    buffer_size = 0;
                                    char *ptr = (char*)buffer;

                                    std::string symbol_name(
                                        ptr,
                                        (size_t)std::min(
                                            (size_t)std::strlen(ptr),
                                            (size_t)symbol_name_length));

                                    //std::cout << "symbol " << symbol_name << std::endl;
                                    symbols.push_back(symbol_name);
                                    if(symbols.size() == num_symbols) state = RECEIVING_DATA_STATE;
                                    break;
                                }
                            }
                            break;
                        /* получаем данные символов */
                        case RECEIVING_DATA_STATE:
                            while(index < bytes_transferred) {
                                buffer[0] = data[index++];
                            }
                            break;
                        };
                    }
                } else {
                    //std::cerr << "error: " << err.message() << std::endl;
                    //sock.close();
                }
            }

            void handle_write(const boost::system::error_code& err, size_t bytes_transferred) {
                if (!err) {

                } else {
                    std::cerr << "error: " << err.message() << std::endl;
                    //sock.close();
                }
            }
        };

//------------------------------------------------------------------------------
        /** \brief Сервер
         */
        class Server  {
        private:

            tcp::acceptor acceptor_;
            boost::asio::io_context& io_context_;

            void start_accept() {
                // socket
                MtConnection::pointer connection =
                    MtConnection::create(io_context_);

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
            Server(boost::asio::io_context& io_context, const uint32_t port):
                io_context_(io_context),
                acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
                start_accept();
            }

            void handle_accept(MtConnection::pointer connection, const boost::system::error_code& err) {
                if(!err){
                    std::cout << "new connection opened"<< std::endl;
                    std::thread connection_thread([connection]() {
                        while(true) {
                            connection->read_data();
                        }
                    });
                    connection_thread.detach();
                }
                start_accept();
            }
        };
    public:

        MetatraderBridge(const uint32_t port) {
            try {
                boost::asio::io_service io_service;
                Server server(io_service, port);
                io_service.run();
            }
            catch(std::exception& e) {
                std::cerr << e.what() << std::endl;
            }
        }

    };
};

#endif // METATRADER_BRIDGE_HPP_INCLUDED
