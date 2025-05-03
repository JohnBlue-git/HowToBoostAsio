## What is Boost.Asio
**Boost.Asio** is a cross-platform C++ library for network and low-level I/O programming. It uses modern C++ techniques to provide developers with a consistent asynchronous model.

## What is the Boost.Asio Event Loop?
Boost.Asio uses an io_context (formerly io_service) as the core of its asynchronous event handling system.
\
You can think of it as a task queue:
- Handlers (callbacks) are scheduled.
- The io_context.run() method processes those handlers.
- It waits for I/O events (like sockets, timers, D-Bus signals) and dispatches them to your registered callbacks.

## Interesting question: Will Different Processes Get the Same io_context?
No — not by default. Each process gets its own copy, even if the code is identical.
- Processes don't share memory unless you're using shared memory (shm) or inter-process communication (IPC).
- A boost::asio::io_context lives in memory — so unless you're specifically sharing it using shared memory mechanisms, each process gets its own.

## Interesting question: Does io_context.run() Wait for One Callback to Finish?
Yes - boost::asio::io_context is synchronous in how it dispatches callbacks.
```C++
/*
The output will be:
Handler 1 start
Handler 1 end
Handler 2
*/
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
```

## Interesting question: Single-threaded vs Multi-threaded Event Loops
Then io_context can run multiple handlers in parallel — one per thread:
- Handlers can be processed concurrently.
- Each thread runs one handler at a time, but overall multiple handlers can be executing if you have multiple threads.
```C++
/*

The output will be:

Handler 1 start
Handler 2
Handler 1 end

or

Handler 2
Handler 1 start
Handler 1 end

*/
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

    std::thread t1([&] { io.run(); });
    std::thread t2([&] { io.run(); });

    t1.join();
    t2.join();

    return 0;
}
```
