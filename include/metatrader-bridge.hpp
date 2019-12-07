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
#include <atomic>
#include <string.h>
//#include <xtime.hpp>

namespace metatrader_bridge {
    using boost::asio::ip::tcp;

    class TickData {
    public:
        double bid;
        double ask;
        uint64_t timestamp;
    };

    class MetatraderBridge {
    private:

        /** \brief Класс соединения
         */
        class MtConnection {
        private:
            boost::asio::io_service mt_io_service;
            tcp::acceptor mt_acceptor;
            tcp::socket mt_socket;
        public:

            /** \brief Прочитать string
             * \return Строка
             */
            std::string read_string() {
                char data[sizeof(char)*32];
                boost::asio::read(mt_socket, boost::asio::buffer(data, sizeof(char)*32));
                size_t copy_bytes = strnlen(data, sizeof(char)*32);
                return std::string(data, copy_bytes);
            }

            /** \brief Прочитать uint32_t
             * \return значение числа типа uint32_t
             */
            uint32_t read_uint32() {
                uint8_t data[sizeof(uint32_t)];
                boost::asio::read(mt_socket, boost::asio::buffer(data, sizeof(uint32_t)));
                return ((uint32_t*)&data)[0];
            }

            /** \brief Прочитать double
             * \return значение числа типа double
             */
            double read_double() {
                uint8_t data[sizeof(double)];
                boost::asio::read(mt_socket, boost::asio::buffer(data, sizeof(double)));
                return ((double*)&data)[0];
            }

            /** \brief Прочитать uint64_t
             * \return значение числа типа uint64_t
             */
            uint64_t read_uint64() {
                uint8_t data[sizeof(uint64_t)];
                boost::asio::read(mt_socket, boost::asio::buffer(data, sizeof(uint64_t)));
                return ((uint64_t*)&data)[0];
            }

            MtConnection(const uint32_t port) :
                    mt_acceptor(mt_io_service, tcp::endpoint(tcp::v4(), port)),
                    mt_socket(mt_io_service) {
                mt_acceptor.accept(mt_socket);
            }
        };

        bool is_connected_ = false; /**< Флаг установленного соединения */
        uint32_t num_symbol = 0;    /**< Количество символов */
        std::vector<std::string> symbol_list;   /**< Список символов */

        std::vector<double> symbol_bid;
        std::vector<double> symbol_ask;

        std::vector<uint64_t> symbol_timestamp;
        uint64_t server_timestamp = 0;
        std::mutex symbol_data_mutex;
    public:

        /** \brief Конструктор моста метатрейдера
         * \param port Номер порта
         */
        MetatraderBridge(const uint32_t port) {
            /* запустим соединение в отдельном потоке */
            std::thread([&]{
                while(true) {
                    /* создадим соединение */
                    std::shared_ptr<MtConnection> connection =
                        std::make_shared<MtConnection>(port);
                    symbol_list.clear();
                    symbol_bid.clear();
                    symbol_ask.clear();
                    try {
                        /* сначала читаем количество символов */
                        num_symbol = connection->read_uint32();

                        /* инициализируем массивы данных */
                        symbol_list.reserve(num_symbol);
                        symbol_bid.resize(num_symbol);
                        symbol_ask.resize(num_symbol);
                        symbol_timestamp.resize(num_symbol);

                        /* читаем имена символов */
                        for(uint32_t s = 0; s < num_symbol; ++s) {
                            symbol_list.push_back(connection->read_string());
                            std::cout << symbol_list.back() << std::endl;
                        }

                        while(true) {
                            /* читаем данные символов */
                            for(uint32_t s = 0; s < num_symbol; ++s) {
                                symbol_bid[s] = connection->read_double();
                                symbol_ask[s] = connection->read_double();

                                const double open = connection->read_double();
                                const double high = connection->read_double();
                                const double low = connection->read_double();
                                const double close = connection->read_double();

                                const uint64_t volume = connection->read_uint64();
                                symbol_timestamp[s] = connection->read_uint64();
                                std::cout << "time: " << symbol_timestamp[s] << std::endl;
                                //std::cout << symbol_list[s] << " bid " << bid << " ask " << ask << std::endl;
                            }
                            /* читаем метку времени сервера */
                            server_timestamp = connection->read_uint64();
                            /* теперь мы вправе сказать что соединение удалось */
                            is_connected_ = true;
                            std::cout << "server time: " << server_timestamp << std::endl;
                        }
                    } catch (std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        is_connected_ = false;
                    }
                }
            }).detach();
        }

        /** \brief Проверить соединение
         * \return Вернет true, если соединение установлено
         */
        inline bool is_connected() {
            return is_connected_;
        }

        inline double get_bid(const uint32_t symbol_index) {
            if(!is_connected_ || symbol_index >= num_symbol) return 0.0;
            return symbol_bid[symbol_index];
        }

        inline double get_ask(const uint32_t symbol_index) {
            if(!is_connected_ || symbol_index >= num_symbol) return 0.0;
            return symbol_ask[symbol_index];
        }

    };
};

#endif // METATRADER_BRIDGE_HPP_INCLUDED
