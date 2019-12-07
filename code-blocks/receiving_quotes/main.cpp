#include <iostream>
#include <metatrader-bridge.hpp>
#include <ctime>

using boost::asio::ip::tcp;

int main() {
    metatrader_bridge::MetatraderBridge iMT(5555);
    while(true) {

    }
    return 0;
}
