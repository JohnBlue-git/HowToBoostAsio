#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    boost::asio::io_context io;

    io.post([] {
        std::cout << "Handler 1 start\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Handler 1 end\n";
    });

    io.post([] {
        std::cout << "Handler 2\n";
    });

    io.run();

    return 0;
}
