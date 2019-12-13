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

namespace mt_bridge {
    using boost::asio::ip::tcp;

    /** \brief Класс для хранения бара
     */
    class MtCandle {
    public:
        double open;
        double high;
        double low;
        double close;
        double volume;
        uint64_t timestamp;

        MtCandle() :
            open(0),
            high(0),
            low (0),
            close(0),
            volume(0),
            timestamp(0) {
        };

        MtCandle(
                const double &_open,
                const double &_high,
                const double &_low,
                const double &_close,
                const uint64_t &_timestamp) :
            open(_open),
            high(_high),
            low (_low),
            close(_close),
            volume(0),
            timestamp(_timestamp) {
        }

        MtCandle(
                const double &_open,
                const double &_high,
                const double &_low,
                const double &_close,
                const double &_volume,
                const uint64_t &_timestamp) :
            open(_open),
            high(_high),
            low (_low),
            close(_close),
            volume(_volume),
            timestamp(_timestamp) {
        }
    };

    /** \brief Класс Моста между Metatrader и программой
     */
    template<class CANDLE_TYPE = MtCandle>
    class MetatraderBridge {
    private:
        std::thread thread_server;  /**< Поток сервера */

        const uint32_t MT_BRIDGE_MAX_VERSION = 1;

        std::atomic<bool> is_mt_connected;  /**< Флаг установленного соединения */
        std::atomic<bool> is_error;
        std::atomic<uint32_t> num_symbol;       /**< Количество символов */
        std::atomic<uint32_t> mt_bridge_version;/**< Версия MT-Bridge для metatrader */
        std::atomic<uint64_t> hist_init_len;    /**< Глубина исторических данных */
        std::vector<std::string> symbol_list;   /**< Список символов */
        std::mutex symbol_list_mutex;

        std::vector<double> symbol_bid; /**< Массив цены bid тиков символов */
        std::vector<double> symbol_ask; /**< Массив цены ask тиков символов */
        std::vector<uint64_t> symbol_timestamp;
        std::mutex symbol_tick_mutex;

        std::vector<std::vector<CANDLE_TYPE>> array_candles;
        std::mutex array_candles_mutex;

        std::atomic<uint64_t> server_timestamp;

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

    public:

        /** \brief Конструктор моста метатрейдера
         * \param port Номер порта
         */
        MetatraderBridge(const uint32_t port) {
            is_mt_connected = false;
            is_error = false;
            num_symbol = 0;
            mt_bridge_version = 0;
            hist_init_len = 0;
            server_timestamp = 0;
            //last_server_timestamp = 0;
            /* запустим соединение в отдельном потоке */
            thread_server = std::thread([&, port]{
                while(true) {
                    /* создадим соединение */
                    std::shared_ptr<MtConnection> connection =
                        std::make_shared<MtConnection>(port);
                    /* очистим список символов */
                    {
                        std::lock_guard<std::mutex> _mutex(symbol_list_mutex);
                        symbol_list.clear();
                    }
                    /* очистим массивы символов */
                    {
                        std::lock_guard<std::mutex> _mutex(array_candles_mutex);
                        array_candles.clear();
                    }
                    /* очищаем массивы для тиков */
                    {
                        std::lock_guard<std::mutex> _mutex(symbol_tick_mutex);
                        symbol_bid.clear();
                        symbol_ask.clear();
                        symbol_timestamp.clear();
                    }
                    try {
                        /* читаем версию эксперта для Metatrdaer */
                        mt_bridge_version = connection->read_uint32();
                        if(mt_bridge_version > MT_BRIDGE_MAX_VERSION)
                            throw("Error! Unsupported expert version for metatrader");

                        /* читаем количество символов */
                        num_symbol = connection->read_uint32();

                        /* инициализируем массивы тиковых данных */
                        {
                            std::lock_guard<std::mutex> _mutex(symbol_tick_mutex);
                            symbol_bid.resize(num_symbol);
                            symbol_ask.resize(num_symbol);
                            symbol_timestamp.resize(num_symbol);
                        }

                        /* инициализируем массив баров */
                        {
                            std::lock_guard<std::mutex> _mutex(array_candles_mutex);
                            array_candles.resize(num_symbol);
                        }

                        /* читаем имена символов */
                        {
                            std::lock_guard<std::mutex> _mutex(symbol_list_mutex);
                            symbol_list.reserve(num_symbol);
                            for(uint32_t s = 0; s < num_symbol; ++s) {
                                symbol_list.push_back(connection->read_string());
                            }
                        }

                        /* читаем глубину истории для инициализации */
                        hist_init_len = connection->read_uint32();
                        uint64_t read_len = 0;
                        while(true) {
                            /* читаем данные символов */
                            for(uint32_t s = 0; s < num_symbol; ++s) {
                                const double bid = connection->read_double();
                                const double ask = connection->read_double();

                                const double open = connection->read_double();
                                const double high = connection->read_double();
                                const double low = connection->read_double();
                                const double close = connection->read_double();

                                const uint64_t volume = connection->read_uint64();
                                const uint64_t timestamp = connection->read_uint64();
                                /* сохраняем тик */
                                {
                                    std::lock_guard<std::mutex> _mutex(symbol_tick_mutex);
                                    symbol_bid[s] = bid;
                                    symbol_ask[s] = ask;
                                    symbol_timestamp[s] = timestamp;
                                }
                                /* сохраняем бар */
                                {
                                    std::lock_guard<std::mutex> _mutex(array_candles_mutex);
                                    if(array_candles[s].size() == 0 || array_candles[s].back().timestamp < timestamp) {
                                        array_candles[s].push_back(CANDLE_TYPE(open, high, low, close, volume, timestamp));
                                    } else
                                    if(array_candles[s].back().timestamp == timestamp) {
                                        array_candles[s].back().open = open;
                                        array_candles[s].back().high = high;
                                        array_candles[s].back().low = low;
                                        array_candles[s].back().close = close;
                                        array_candles[s].back().volume = volume;
                                        array_candles[s].back().timestamp = timestamp;
                                    }
                                }
                            }
                            /* читаем метку времени сервера */
                            server_timestamp = connection->read_uint64();
                            ++read_len;
                            if(read_len > hist_init_len) {
                                /* теперь мы вправе сказать, что соединение удалось */
                                is_error = false;
                                is_mt_connected = true;
                            }
                        } // while
                    } catch (std::exception& e) {
                        std::cerr << e.what() << std::endl;
                        is_mt_connected = false;
                        is_error = true;
                    } catch (...) {
                        std::cerr << "error" << std::endl;
                        is_mt_connected = false;
                        is_error = true;
                    }
                    const uint32_t DELAY_WAIT = 1000;
                    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT));
                }
            });
            thread_server.detach();
        }

        /** \brief Проверить соединение
         * \return Вернет true, если соединение установлено
         */
        inline bool connected() {
            return is_mt_connected;
        }

        /** \brief Подождать соединение
         *
         * Данный метод ждет, пока не установится соединение
         * \return вернет true, если соединение есть, иначе произошла ошибка
         */
        inline bool wait() {
            while(!is_error && !is_mt_connected) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return is_mt_connected;
        }

        /** \brief Получить метку времени сервера MetaTrader
         * \return Метка времени сервера MetaTrader
         */
        inline uint64_t get_server_timestamp() {
            return server_timestamp;
        }

        inline bool update_server_timestamp() {
            static uint64_t last_server_timestamp = 0;
            if(server_timestamp != last_server_timestamp) {
                last_server_timestamp = server_timestamp;
                return true;
            }
            return false;
        }

        /** \brief Получить версию MT-Bridge
         * \return Версия MT-Bridge, начиная с 1. Если вернет 0, значит нет подключения
         */
        inline uint32_t get_mt_bridge_version() {
            return mt_bridge_version;
        }

        /** \brief Получить список имен символов/валютных пар
         * \return список имен символов/валютных пар
         */
        std::vector<std::string> get_symbol_list() {
            if(!is_mt_connected) return std::vector<std::string>();
            std::lock_guard<std::mutex> _mutex(symbol_list_mutex);
            return symbol_list;
        }

        /** \brief Получить цену bid символа
         * \param symbol_index Индекс символа
         * \return Цена bid
         */
        inline double get_bid(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return 0.0;
            std::lock_guard<std::mutex> _mutex(symbol_tick_mutex);
            return symbol_bid[symbol_index];
        }

        /** \brief Получить цену ask символа
         * \param symbol_index Индекс символа
         * \return Цена ask
         */
        inline double get_ask(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return 0.0;
            std::lock_guard<std::mutex> _mutex(symbol_tick_mutex);
            return symbol_ask[symbol_index];
        }

        /** \brief Получить бар
         * \param symbol_index Индекс символа
         * \param offset Смещение относительно последнего бара
         * \return Бар/Свеча
         */
        CANDLE_TYPE get_candle(const uint32_t symbol_index, const uint32_t offset = 0) {
            if(!is_mt_connected || symbol_index >= num_symbol) return CANDLE_TYPE();
            std::lock_guard<std::mutex> _mutex(array_candles_mutex);
            const size_t array_size = array_candles[symbol_index].size();
            if(offset >= array_size) return CANDLE_TYPE();
            return array_candles[symbol_index][array_size - offset - 1];
        }

        /** \brief Получить массив баров
         * \param symbol_index Индекс символа
         * \return Массив баров
         */
        std::vector<CANDLE_TYPE> get_candles(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return  std::vector<CANDLE_TYPE>();
            std::lock_guard<std::mutex> _mutex(array_candles_mutex);
            return array_candles[symbol_index];
        }

    };

    typedef MetatraderBridge<> MtBridge; /**< Класс Моста между Metatrader
        * и программой со стандартным классом для хранения баров
        */
};

#endif // METATRADER_BRIDGE_HPP_INCLUDED
