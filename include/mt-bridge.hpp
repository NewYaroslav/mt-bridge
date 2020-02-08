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
#include <future>
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
        //std::thread thread_server;
        std::future<void> server_future;    /**< Поток сервера */
        std::future<void> callback_future;

        const uint32_t MT_BRIDGE_MAX_VERSION = 1;

        const uint64_t SECONDS_IN_MINUTE = 60;

        std::atomic<bool> is_mt_connected;  /**< Флаг установленного соединения */
        std::atomic<bool> is_error;
        std::atomic<uint32_t> num_symbol;       /**< Количество символов */
        std::atomic<uint32_t> mt_bridge_version;/**< Версия MT-Bridge для metatrader */
        std::atomic<uint64_t> hist_init_len;    /**< Глубина исторических данных */
        std::vector<std::string> symbol_list;   /**< Список символов */
        std::map<std::string,uint32_t> symbol_name_to_index;
        std::mutex symbol_list_mutex;

        std::vector<double> symbol_bid; /**< Массив цены bid тиков символов */
        std::vector<double> symbol_ask; /**< Массив цены ask тиков символов */
        std::vector<uint64_t> symbol_timestamp;
        std::mutex symbol_tick_mutex;

        std::vector<std::vector<CANDLE_TYPE>> array_candles;
        std::mutex array_candles_mutex;

        std::atomic<uint64_t> server_timestamp; /**< Метка времени сервера */
        std::atomic<uint64_t> last_server_timestamp;

        std::atomic<bool> is_stop_command;      /**< Команда закрытия соединения */

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

        void init_historical_data(
                std::vector<std::map<std::string, CANDLE_TYPE>> &candles,
                const uint64_t date_timestamp,
                const uint32_t number_bars) {
            const int64_t first_timestamp = (date_timestamp / SECONDS_IN_MINUTE) * SECONDS_IN_MINUTE;
            const int64_t start_timestamp = first_timestamp - (number_bars - 1) * SECONDS_IN_MINUTE;
            std::lock_guard<std::mutex> lock(symbol_list_mutex);
            candles.resize(number_bars);
            for(uint32_t symbol_index = 0;
                symbol_index < num_symbol;
                ++symbol_index) {
                std::string symbol_name = symbol_list[symbol_index];
                for(size_t i = 0; i < candles.size(); ++i) {
                    candles[i][symbol_name].timestamp = i * SECONDS_IN_MINUTE + start_timestamp;
                }
                for(size_t i = 0; i < array_candles[symbol_index].size(); ++i) {
                    const int64_t index = ((int64_t)array_candles[symbol_index][i].timestamp - start_timestamp) / (int64_t)SECONDS_IN_MINUTE;
                    if(index < 0) continue;
                    if(index >= number_bars) continue;
                    candles[index][symbol_name] = array_candles[symbol_index][i];
                }
            }
        }

        /* реализуем замер смещения времени за 256 отсчетов */
        const uint32_t array_offset_timestamp_size = 256;
        std::array<double, 256> array_offset_timestamp; /**< Массив смещения метки времени */
        uint8_t index_array_offset_timestamp = 0;       /**< Индекс элемента массива смещения метки времени */
        uint32_t index_array_offset_timestamp_count = 0;
        double last_offset_timestamp_sum = 0;
        std::atomic<double> offset_timestamp;           /**< Смещение метки времени */
        std::atomic<bool> is_autoupdate_logger_offset_timestamp;

        /** \brief Обновить смещение метки времени
         *
         * Данный метод использует оптимизированное скользящее среднее
         * для выборки из 256 элеметов для нахождения смещения метки времени сервера
         * относительно времени компьютера
         * \param offset смещение метки времени
         */
        inline void update_offset_timestamp(const double offset) {
            if(index_array_offset_timestamp_count != array_offset_timestamp_size) {
                array_offset_timestamp[index_array_offset_timestamp] = offset;
                index_array_offset_timestamp_count = (uint32_t)index_array_offset_timestamp + 1;
                last_offset_timestamp_sum += offset;
                offset_timestamp = last_offset_timestamp_sum / (double)index_array_offset_timestamp_count;
                ++index_array_offset_timestamp;
                return;
            }
            /* находим скользящее среднее смещения метки времени сервера относительно компьютера */
            last_offset_timestamp_sum = last_offset_timestamp_sum +
                (offset - array_offset_timestamp[index_array_offset_timestamp]);
            array_offset_timestamp[index_array_offset_timestamp++] = offset;
            offset_timestamp = last_offset_timestamp_sum/
                (double)array_offset_timestamp_size;
        }

        inline  uint64_t get_timestamp(
            const uint32_t &day,
            const uint32_t &month,
            const uint32_t &year,
            const uint32_t &hour,
            const uint32_t &minute,
            const uint32_t &second) {
            uint64_t _secs;
            long _mon, _year;
            long long _days; // для предотвращения проблемы 2038 года переменная должна быть больше 32 бит
            _mon = month - 1;
            const long _TBIAS_YEAR = 1900;
            const long long _DAYS_IN_YEAR = 365;
            const long	lmos[] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335};
            const long	mos[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            _year = year - _TBIAS_YEAR;
            _days = (((_year - 1) / 4) + ((((_year) & 03) || ((_year) == 0)) ? mos[_mon] : lmos[_mon])) - 1;
            _days += _DAYS_IN_YEAR * _year;
            _days += day;
            const long _TBIAS_DAYS = 25567;
            const uint64_t _SECONDS_IN_MINUTE = 60;
            const uint64_t _SECONDS_IN_HOUR = 3600;
            const uint64_t _SECONDS_IN_DAY = 86400;
            _days -= _TBIAS_DAYS;
            _secs = _SECONDS_IN_HOUR * hour;
            _secs += _SECONDS_IN_MINUTE * minute;
            _secs += second;
            _secs += _days * _SECONDS_IN_DAY;
            return _secs;
        }

        inline uint64_t get_timestamp() {
            time_t rawtime;
            time(&rawtime);
            struct tm* ptm;
            ptm = gmtime(&rawtime);
            return get_timestamp(ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year + 1900, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
        }

        inline double get_ftimestamp() {
            uint64_t t = get_timestamp();
            timeb tb;
            ftime(&tb);
            return (double)t + (double)tb.millitm/1000.0;
        }

    public:

        /** \brief Получить время сервера с дробной частью
         * \return время сервера
         */
        inline double get_server_ftimestamp() {
            return get_ftimestamp() + offset_timestamp;
        }

        /// Типы События
        enum class EventType {
            NEW_TICK,                   /**< Получен новый тик */
            HISTORICAL_DATA_RECEIVED,   /**< Получены исторические данные */
        };

        /// Типы цены
        enum class PriceType {
            PRICE_BID,          /**< Цена Bid */
            PRICE_ASK,          /**< Цена Ask */
            PRICE_BID_ASK_DIV2  /**< Цена (bid+ask)/2 */
        };

        /** \brief Конструктор моста метатрейдера
         * \param port Номер порта
         * \param number_bars
         * \param callback
         */
        MetatraderBridge(
                const uint32_t port,
                const uint32_t number_bars = 1440,
                std::function<void(
                    const std::map<std::string, CANDLE_TYPE> &candles,
                    const EventType event,
                    const uint64_t timestamp)> callback = nullptr) {
            is_mt_connected = false;
            is_error = false;
            is_stop_command = false;
            num_symbol = 0;
            mt_bridge_version = 0;
            hist_init_len = 0;
            server_timestamp = 0;
            last_server_timestamp = 0;
            offset_timestamp = 0;
            /* запустим соединение в отдельном потоке */
            //thread_server = std::thread([&, port]{
            server_future = std::async(std::launch::async,[&, port]() {
                while(!is_stop_command) {
                    /* создадим соединение */
                    std::shared_ptr<MtConnection> connection =
                        std::make_shared<MtConnection>(port);
                    /* очистим список символов */
                    {
                        std::lock_guard<std::mutex> lock(symbol_list_mutex);
                        symbol_list.clear();
                        symbol_name_to_index.clear();
                    }
                    /* очистим массивы символов */
                    {
                        std::lock_guard<std::mutex> lock(array_candles_mutex);
                        array_candles.clear();
                    }
                    /* очищаем массивы для тиков */
                    {
                        std::lock_guard<std::mutex> lock(symbol_tick_mutex);
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
                            std::lock_guard<std::mutex> lock(symbol_tick_mutex);
                            symbol_bid.resize(num_symbol);
                            symbol_ask.resize(num_symbol);
                            symbol_timestamp.resize(num_symbol);
                        }

                        /* инициализируем массив баров */
                        {
                            std::lock_guard<std::mutex> lock(array_candles_mutex);
                            array_candles.resize(num_symbol);
                        }

                        /* читаем имена символов */
                        {
                            std::lock_guard<std::mutex> lock(symbol_list_mutex);
                            symbol_list.reserve(num_symbol);
                            for(uint32_t s = 0; s < num_symbol; ++s) {
                                symbol_list.push_back(connection->read_string());
                                symbol_name_to_index[symbol_list.back()] = s;
                            }
                        }

                        /* читаем глубину истории для инициализации */
                        hist_init_len = connection->read_uint32();
                        uint64_t read_len = 0;
                        while(!is_stop_command) {
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
                                    std::lock_guard<std::mutex> lock(symbol_tick_mutex);
                                    symbol_bid[s] = bid;
                                    symbol_ask[s] = ask;
                                    symbol_timestamp[s] = timestamp;
                                }
                                /* сохраняем бар */
                                {
                                    std::lock_guard<std::mutex> lock(array_candles_mutex);
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

                            /* если метка времени поменялась, найдем время сервера */
                            if(last_server_timestamp != server_timestamp) {
                                last_server_timestamp = (uint64_t)server_timestamp;
                                double pc_time = get_ftimestamp();
                                double offset_time = (double)server_timestamp - pc_time;
                                update_offset_timestamp(offset_time);
                            }

                            ++read_len;
                            if(read_len > hist_init_len) {
                                /* теперь мы вправе сказать, что соединение удалось */
                                is_error = false;
                                is_mt_connected = true;
                            }
                        } // while
                    } catch (std::exception& e) {
                        std::cerr << "error: " << e.what() << std::endl;
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
            //thread_server.detach();
            if(callback == nullptr) return;
            /* создаем поток обработки событий */
            //std::thread stream_thread = std::thread([&,number_bars, callback] {
            callback_future = std::async(std::launch::async,[&, number_bars, callback]() {
                while(!is_mt_connected) {
                    std::this_thread::yield();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    if(is_stop_command) return;
                }
                /* сначала инициализируем исторические данные */
                uint32_t hist_data_number_bars = number_bars;
                while(!is_stop_command) {
                    const uint64_t init_date_timestamp =
                        ((server_timestamp / SECONDS_IN_MINUTE) * SECONDS_IN_MINUTE) - SECONDS_IN_MINUTE;
                    std::vector<std::map<std::string, CANDLE_TYPE>> hist_array_candles;
                    init_historical_data(hist_array_candles, init_date_timestamp, hist_data_number_bars);
                    /* далее отправляем загруженные данные в callback */
                    uint64_t start_timestamp = init_date_timestamp - (hist_data_number_bars - 1) * SECONDS_IN_MINUTE;
                    for(size_t i = 0; i < hist_array_candles.size(); ++i) {
                        const uint64_t timestamp = i * SECONDS_IN_MINUTE + start_timestamp;
                        if(callback != nullptr) callback(
                            hist_array_candles[i],
                            EventType::HISTORICAL_DATA_RECEIVED,
                            timestamp);
                    }
                    const uint64_t end_date_timestamp =
                        ((server_timestamp / SECONDS_IN_MINUTE) * SECONDS_IN_MINUTE) -
                       SECONDS_IN_MINUTE;
                    if(end_date_timestamp == init_date_timestamp) break;
                    hist_data_number_bars = (end_date_timestamp - init_date_timestamp) / SECONDS_IN_MINUTE;
                }

                /* далее занимаемся получением новых тиков */
                uint64_t last_timestamp = (uint64_t)get_server_ftimestamp();;
                uint64_t last_minute = last_timestamp / SECONDS_IN_MINUTE;
                while(!is_stop_command) {
                    uint64_t timestamp = (uint64_t)get_server_ftimestamp();;
                    if(timestamp <= last_timestamp || !is_mt_connected) {
                        std::this_thread::yield();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    /* начало новой секунды,
                     * собираем актуальные цены бара и вызываем callback
                     */
                    last_timestamp = timestamp;
                    std::map<std::string, CANDLE_TYPE> candles;
                    {
                        std::lock_guard<std::mutex> lock(symbol_list_mutex);
                        for(uint32_t symbol_index = 0;
                            symbol_index < num_symbol;
                            ++symbol_index) {
                            std::string symbol_name(symbol_list[symbol_index]);
                            candles[symbol_name] = get_timestamp_candle(symbol_index, timestamp);
                        }
                    }

                    /* вызов callback */
                    if(callback != nullptr) callback(candles, EventType::NEW_TICK, timestamp);

                    /* загрузка исторических данных и повторный вызов callback,
                     * если нужно
                     */
                    uint64_t server_minute = timestamp / SECONDS_IN_MINUTE;
                    if(server_minute <= last_minute) {
                        std::this_thread::yield();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    hist_data_number_bars = server_minute - last_minute;
                    const int64_t start_timestamp = last_minute * SECONDS_IN_MINUTE;
                    last_minute = server_minute;

                    /* загружаем исторические данные в несколько потоков */
                    const uint64_t download_date_timestamp =
                        ((timestamp / SECONDS_IN_MINUTE) * SECONDS_IN_MINUTE) -
                        SECONDS_IN_MINUTE;

                    std::vector<std::map<std::string, CANDLE_TYPE>> hist_array_candles;
                    init_historical_data(
                        hist_array_candles,
                        download_date_timestamp,
                        hist_data_number_bars);
                    for(size_t i = 0; i < hist_array_candles.size(); ++i) {
                        if(callback != nullptr) callback(
                            hist_array_candles[i],
                            EventType::HISTORICAL_DATA_RECEIVED,
                            start_timestamp + i * SECONDS_IN_MINUTE);
                    }
                    std::this_thread::yield();
                } // while
            });
            //stream_thread.detach();
        }

        ~MetatraderBridge() {
            is_stop_command = true;
            /* Существует проблема с циклом yield().
             * Если поток, вызывающий деструктор, имеет более высокий приоритет, чем завершаемый поток,
             * то ваш проект может вечно жить в однопроцессорной системе.
             * Даже в многоядерной системе может быть большая задержка.
             * https://coderoad.ru/7927773
             */
            if(server_future.valid()) {
                try {
                    server_future.wait();
                    server_future.get();
                }
                catch(const std::exception &e) {
                    std::cerr << "Error: ~MetatraderBridge(), what: " << e.what() << std::endl;
                }
                catch(...) {
                    std::cerr << "Error: ~MetatraderBridge()" << std::endl;
                }
            }
            if(callback_future.valid()) {
                try {
                    callback_future.wait();
                    callback_future.get();
                }
                catch(const std::exception &e) {
                    std::cerr << "Error: ~MetatraderBridge(), what: " << e.what() << std::endl;
                }
                catch(...) {
                    std::cerr << "Error: ~MetatraderBridge()" << std::endl;
                }
            }
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
                std::this_thread::yield();
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
            if(!is_mt_connected) return false;
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
            std::lock_guard<std::mutex> lock(symbol_list_mutex);
            return symbol_list;
        }

        /** \brief Получить цену bid символа
         * \param symbol_index Индекс символа
         * \return Цена bid
         */
        inline double get_bid(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return 0.0;
            std::lock_guard<std::mutex> lock(symbol_tick_mutex);
            return symbol_bid[symbol_index];
        }

        /** \brief Получить цену ask символа
         * \param symbol_index Индекс символа
         * \return Цена ask
         */
        inline double get_ask(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return 0.0;
            std::lock_guard<std::mutex> lock(symbol_tick_mutex);
            return symbol_ask[symbol_index];
        }

        /** \brief Получить бар
         * \param symbol_index Индекс символа
         * \param offset Смещение относительно последнего бара
         * \return Бар
         */
        inline CANDLE_TYPE get_candle(const uint32_t symbol_index, const uint32_t offset = 0) {
            if(!is_mt_connected || symbol_index >= num_symbol) return CANDLE_TYPE();
            std::lock_guard<std::mutex> lock(array_candles_mutex);
            const size_t array_size = array_candles[symbol_index].size();
            if(offset >= array_size) return CANDLE_TYPE();
            return array_candles[symbol_index][array_size - offset - 1];
        }

        /** \brief Получить бар
         * \param symbol_name Имя символа
         * \param offset Смещение относительно последнего бара
         * \return Бар
         */
        inline CANDLE_TYPE get_candle(const std::string &symbol_name) {
            if(!is_mt_connected) return CANDLE_TYPE();
            uint32_t symbol_index;
            {
                std::lock_guard<std::mutex> lock(symbol_list_mutex);
                auto it = symbol_name_to_index.find(symbol_name);
                if(it == symbol_name_to_index.end()) return CANDLE_TYPE();
                symbol_index = it->second;
            }
            return get_candle(symbol_index);
        }

        /** \brief Получить массив баров
         * \param symbol_index Индекс символа
         * \return Массив баров
         */
        inline std::vector<CANDLE_TYPE> get_candles(const uint32_t symbol_index) {
            if(!is_mt_connected || symbol_index >= num_symbol) return  std::vector<CANDLE_TYPE>();
            std::lock_guard<std::mutex> lock(array_candles_mutex);
            return array_candles[symbol_index];
        }


        /** \brief Получить массив баров
         * \param symbol_name Имя символа
         * \return Массив баров
         */
        inline std::vector<CANDLE_TYPE> get_candles(const std::string &symbol_name) {
            if(!is_mt_connected) return  std::vector<CANDLE_TYPE>();
            uint32_t symbol_index;
            {
                std::lock_guard<std::mutex> lock(symbol_list_mutex);
                auto it = symbol_name_to_index.find(symbol_name);
                if(it == symbol_name_to_index.end()) return CANDLE_TYPE();
                symbol_index = it->second;
            }
            return get_candles(symbol_index);
        }

        /** \brief Получить бар по метке времени
         *
         * \param symbol_index Индекс символа
         * \param timestamp Метка времени
         * \param price_type Тип цены
         * \return Цена bid, ask или (bid+ask)/2
         */
        inline CANDLE_TYPE get_timestamp_candle(
                const uint32_t symbol_index,
                const uint64_t timestamp,
                const PriceType price_type = PriceType::PRICE_BID) {
            if(!is_mt_connected || symbol_index >= num_symbol) return CANDLE_TYPE();
            const uint64_t first_timestamp = (timestamp / SECONDS_IN_MINUTE) * SECONDS_IN_MINUTE;
            std::lock_guard<std::mutex> lock(array_candles_mutex);

            const size_t array_candles_size =
                array_candles[symbol_index].size();
            if(array_candles_size == 0) return CANDLE_TYPE();
            size_t index = array_candles_size - 1;
            /* особый случай, бар еще не успел сформироваться */
            if(array_candles[symbol_index].back().timestamp == (first_timestamp - SECONDS_IN_MINUTE)) {
                double price = 0;
                {
                    std::lock_guard<std::mutex> lock2(symbol_tick_mutex);
                    const double ask = symbol_ask[symbol_index];
                    const double bid = symbol_bid[symbol_index];
                    price = price_type == PriceType::PRICE_BID_ASK_DIV2 ?
                        (bid + ask) /2.0 : price_type == PriceType::PRICE_BID ?
                        bid : price_type == PriceType::PRICE_ASK ?
                        ask : bid;
                }
                return CANDLE_TYPE(price, price, price, price,
                    0, first_timestamp);
            }
            while(true) {
                if(array_candles[symbol_index][index].timestamp == first_timestamp) {
                    return array_candles[symbol_index][index];
                }
                if(index > 0) --index;
                else break;
            }
            return CANDLE_TYPE();
        }


        /** \brief Получить бар по метке времени
         *
         * \param symbol_name Имя символа
         * \param timestamp Метка времени
         * \param price_type Тип цены
         * \return Цена bid, ask или (bid+ask)/2
         */
        inline CANDLE_TYPE get_timestamp_candle(
                const std::string &symbol_name,
                const uint64_t timestamp,
                const PriceType price_type = PriceType::PRICE_BID) {
            if(!is_mt_connected) return CANDLE_TYPE();
            uint32_t symbol_index;
            {
                std::lock_guard<std::mutex> lock(symbol_list_mutex);
                auto it = symbol_name_to_index.find(symbol_name);
                if(it == symbol_name_to_index.end()) return CANDLE_TYPE();
                symbol_index = it->second;
            }
            return get_timestamp_candle(symbol_index, timestamp, price_type);
        }

        /** \brief Получить бар по имени
         * \param symbol_name Имя валютной пары
         * \param candles Карта баров валютных пар
         * \return Бар
         */
        inline const static CANDLE_TYPE get_candle(
                const std::string &symbol_name,
                const std::map<std::string, CANDLE_TYPE> &candles) {
            auto it = candles.find(symbol_name);
            if(it == candles.end()) return CANDLE_TYPE();
            if(it->second.close == 0 || it->second.timestamp == 0) return CANDLE_TYPE();
            return it->second;
        }

        /** \brief Проверить бар
         * \param candle Бар
         * \return Вернет true, если данные по бару корректны
         */
        inline const static bool check_candle(CANDLE_TYPE &candle) {
            if(candle.close == 0 || candle.timestamp == 0) return false;
            return true;
        }

        std::map<std::string, CANDLE_TYPE> get_candles(const uint64_t timestamp) {
            std::map<std::string, CANDLE_TYPE> candles;
            {
                std::lock_guard<std::mutex> lock(symbol_list_mutex);
                for(uint32_t symbol_index = 0;
                    symbol_index < num_symbol;
                    ++symbol_index) {
                    std::string symbol_name(symbol_list[symbol_index]);
                    candles[symbol_name] = get_timestamp_candle(symbol_index, timestamp);
                }
            }
            return candles;
        }
    };

    typedef MetatraderBridge<> MtBridge; /**< Класс Моста между Metatrader
        * и программой со стандартным классом для хранения баров
        */
};

#endif // METATRADER_BRIDGE_HPP_INCLUDED
