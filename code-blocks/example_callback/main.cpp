#include <iostream>
#include <mt-bridge.hpp>

int main() {
    const uint32_t port = 5555;
    mt_bridge::MtBridge iMT(
            port,
            10,
            [&](const std::map<std::string, mt_bridge::MtCandle> &candles,
                const mt_bridge::MtBridge::EventType event,
                const uint64_t timestamp) {
        mt_bridge::MtCandle candle = mt_bridge::MtBridge::get_candle("EURUSD", candles);
        switch(event) {
        case mt_bridge::MtBridge::EventType::HISTORICAL_DATA_RECEIVED:
            std::cout << "history bar: " << timestamp << " minute day: " << ((timestamp / 60) % 1440) << std::endl;
            if(mt_bridge::MtBridge::check_candle(candle)) {
                std::cout << "EURUSD, close: " << candle.close
                    << " volume: " << candle.volume
                    << " t: " << candle.timestamp << std::endl;
            } else {
                std::cout << "EURUSD, error"<< std::endl;
            }
            break;
        case mt_bridge::MtBridge::EventType::NEW_TICK:
            std::cout << "news tick: " << timestamp << std::endl;
            if(mt_bridge::MtBridge::check_candle(candle)) {
                std::cout << "EURUSD, close: " << candle.close
                    << " volume: " << candle.volume
                    << " t: " << candle.timestamp << std::endl;
            } else {
                std::cout << "EURUSD, error"<< std::endl;
            }
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

    /* quotations stream */
    while(true) {
        std::this_thread::yield();
    }
    return 0;
}
