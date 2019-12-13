#include <iostream>
#include <mt-bridge.hpp>
#include <ctime>

//using boost::asio::ip::tcp;

int main() {
    const uint32_t port = 5555;
    mt_bridge::MtBridge iMT(port);

    if(!iMT.wait()) {
        std::cout << "no connection" << std::endl;
        return 0;
    }
    std::cout << "connection established" << std::endl;

    const uint32_t DELAY_WAIT = 5000;
    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT));

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

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT));

    const uint32_t symbol_index = 0; // first symbol in the list
    /* get historical data to initialize your indicators */
    std::cout << iMT.get_symbol_list()[symbol_index] << std::endl;
    std::vector<mt_bridge::MtCandle> candles = iMT.get_candles(symbol_index);
    for(size_t i = 0; i < candles.size(); ++i) {
        std::cout << "candle, o: " << candles[i].open
            << " h: " << candles[i].high
            << " l: " << candles[i].low
            << " c: " << candles[i].close
            << " v: " << candles[i].volume
            << " t: " << candles[i].timestamp
            << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_WAIT));

    /* quotations stream */
    while(true) {
        if(!iMT.update_server_timestamp()) continue;
        mt_bridge::MtCandle candle = iMT.get_candle(symbol_index);
        std::cout
            << iMT.get_symbol_list()[symbol_index]
            << " candle,"
            //<< " o: " << candle.open
            //<< " h: " << candle.high
            //<< " l: " << candle.low
            << " ask: " << iMT.get_ask(symbol_index)
            << " c: " << candle.close
            << " v: " << candle.volume
            << " t: " << candle.timestamp
            << " s: " << iMT.get_server_timestamp()
            << std::endl;
    }
    return 0;
}
