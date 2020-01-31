![logo](doc/mt-bridge-v1-640x160.png)
# mt-bridge

С++ header-only библиотека + советник для создания *моста* между программой и торговой платформой Metatrader.

## Описание

Данная библиотека представляет из себя сервер, который подключается через сокет к клиенту (советнику в Metatrader). 
Клиент передает на сервер (т.е. в программу на С++) следующий массив данных:

* Во время инициализации подключения передается 
	1. версия советника 
	2. количество символов (валютных пар), указанные в советнике
	3. имена символов
	4. также передаются значения для инициализации N баров всех символов (open,high,low,close,volume), чтобы в программе были доступны прошедшие бары для инициализации индикаторов.
	
* После инициализации советник передает в программу:
	1. состояния текущего бара для всех символов 
	2. цены bid и ask всех символов 
	3. метку времени сервера.

Если связь с советником потеряна, сервер попытается ее установить заново. Аналогично и с клиентом - если сервер перестал принимать данные, клиент попытается установитть связь заново.

В папке *code-blocks/example* расположен пример сервера. Исходные файлы советника для Metatrader находятся в папке *MQL4*.

## Настройки советника

- *Server hostname or IP address* - Имя хоста, по умолчанию *localhost*
- *Server port* - Порт сервера, по умолчанию *5555*
- *Array of used currency pairs* - Массив имен валютны пар, данные которых должен передавать советник. 
По умолчанию представлен следующий список:

	EURUSD,USDJPY,GBPUSD,USDCHF,USDCAD,EURJPY,AUDUSD,NZDUSD,
	EURGBP,EURCHF,AUDJPY,GBPJPY,CHFJPY,EURCAD,AUDCAD,CADJPY,
	NZDJPY,AUDNZD,GBPAUD,EURAUD,GBPCHF,EURNZD,AUDCHF,GBPNZD,
	GBPCAD,XAUUSD

- *Data update period (milliseconds)* - Период обновления данных (в миллисекундах). Чем меньше это время, тем чаще будут поступать данные на сервер.
- *Depth of history to initialize* - Глубина исторических данных во время инициализации. Это количество баров, которое будет передано на сервер во время подключения.

## Пример кода

Данный код после подключения к эксперту выведет на экран список символов, затем выведет исторические данные баров нулевого символа и после этого будет показывать текущую цену ask, цену закрытия бара, объем бара, время открытия бара и время сервера.

```C++
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
            << " ask: " << iMT.get_ask(symbol_index)
            << " c: " << candle.close
            << " v: " << candle.volume
            << " t: " << candle.timestamp
            << " s: " << iMT.get_server_timestamp()
            << std::endl;
    }
    return 0;
}
```

Также в конструктор класса *MtBridge* была добавлена возможность вызывать лямбду-функцию для обработки получения событий (получение нового тика или бара исторических данных).

```
#include <iostream>
#include <mt-bridge.hpp>

int main() {
    const uint32_t port = 5555;
    mt_bridge::MtBridge iMT(
            port,
            10, // количество баров в истории для первоначальной инициализации (не может быть больше, чем задано в советнике)
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
	
    while(true) {
        std::this_thread::yield();
    }
    return 0;
}
```

## Зависимости

* boost.asio (нужны только заголовочные файлы)
* для компилятора *mingw* добавить библиотеки *ws2_32* и *wsock32*.
