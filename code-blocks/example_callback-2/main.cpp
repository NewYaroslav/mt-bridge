#include <iostream>
#include <mt-bridge.hpp>
#include <xtime.hpp>

int main() {
    const uint32_t port = 5555;
    mt_bridge::MtBridge iMT(
            port,
            100,
            [&](const std::map<std::string, mt_bridge::MtCandle> &candles,
                const mt_bridge::MtBridge::EventType event,
                const uint64_t timestamp) {
        mt_bridge::MtCandle candle = mt_bridge::MtBridge::get_candle("AUDCAD", candles);
        switch(event) {
        case mt_bridge::MtBridge::EventType::HISTORICAL_DATA_RECEIVED:
            std::cout << "history bar: " << xtime::get_str_date_time(timestamp) << " minute day: " << ((timestamp / 60) % 1440) << std::endl;
            if(mt_bridge::MtBridge::check_candle(candle)) {
                std::cout << "AUDCAD, close: " << candle.close
                    << " volume: " << candle.volume
                    << " t: " << xtime::get_str_date_time(candle.timestamp) << std::endl;
            } else {
                std::cout << "AUDCAD, error"<< std::endl;
            }
            break;
        case mt_bridge::MtBridge::EventType::NEW_TICK:
            if(mt_bridge::MtBridge::check_candle(candle)) {
                std::cout << "AUDCAD (t1), close: " << candle.close
                    << " volume: " << candle.volume
                    << " t: " << xtime::get_str_date_time(timestamp) << "\r";
            } else {
                std::cout << "AUDCAD, error (t1), close: " << candle.close
                    << " volume: " << candle.volume
                    << " t: " << xtime::get_str_date_time(timestamp) << "\r";
            }
            if(xtime::get_second_minute(timestamp) == 0) std::cout << std::endl;
        return 0;
            break;
        };
    });

    if(!iMT.wait()) {
        std::cout << "no connection" << std::endl;
        return 0;
    }
    std::cout << "connection established" << std::endl;

#if(0)
    /* list all symbols */
    std::vector<std::string> symbol_list = iMT.get_symbol_list();
    std::for_each(symbol_list.begin(), symbol_list.end(), [&](std::string &symbol) {
        static int n = 0;
        std::cout
            << "symbol["
            << std::to_string(n++)
            << "]: "
            << symbol
            << std::endl;
    });
    std::cout << "mt-bridge version: " << iMT.get_mt_bridge_version() << std::endl;
#endif

    /* stop */
    while(true) {
        std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}
