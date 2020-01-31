#include <iostream>
#include <mt-bridge.hpp>

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

    const uint32_t symbol_index = 0; // first symbol in the list

    /* quotations stream */
    while(true) {
        if(!iMT.update_server_timestamp()) continue;
        std::vector<std::string> symbol_list = iMT.get_symbol_list();
        mt_bridge::MtCandle candle = iMT.get_candle(symbol_index);
        double price = (iMT.get_ask(symbol_index) + iMT.get_bid(symbol_index)) / 2;
        if(symbol_index >= symbol_list.size()) {
            std::cout << "error mt-bridge: symbol_list.size = " << symbol_list.size() << std::endl;
            continue;
        }
        std::cout
            << symbol_list[symbol_index]
            << " (ask+bid)/2 = " << price
            << " v: " << candle.volume
            << " t: " << candle.timestamp
            << " s: " << iMT.get_server_timestamp()
            << std::endl;
        mt_bridge::MtCandle candle2 = iMT.get_timestamp_candle(symbol_index, iMT.get_server_timestamp());
        std::cout
            << symbol_list[symbol_index]
            << " close = " << candle2.close
            << " v: " << candle2.volume
            << " t: " << candle2.timestamp
            << " s: " << iMT.get_server_timestamp()
            << std::endl;
        mt_bridge::MtCandle candle3 = iMT.get_timestamp_candle("EURCAD", iMT.get_server_timestamp());
        std::cout
            << "EURCAD close = " << candle3.close
            << " v: " << candle3.volume
            << " t: " << candle3.timestamp
            << " s: " << iMT.get_server_timestamp()
            << std::endl;
    }
    return 0;
}
