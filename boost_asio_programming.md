# How to install lib and compile with

## Installation of Boost.Asio
```console
# Intsll on Ubuntu / Debian:
sudo apt update
sudo apt install libboost-all-dev

# Install on macOS (with Homebrew):
brew install boost
```

## Compile and link with Boost system and pthread:
```console
g++ -std=c++17 single_run.cpp -o single_run -lboost_system -lpthread
g++ -std=c++17 multi_run.cpp -o multi_run -lboost_system -lpthread
```

# Boost.Asio utility

## io_context

### io_context
The `io_context` class provides core I/O functionality for users of asynchronous I/O objects, including:
```C++
asio::ip::tcp::socket
asio::ip::tcp::acceptor
asio::ip::udp::socket
asio::deadline_timer
```

### is not thread-safe (by default)
Boost.Asio sockets are not thread-safe for concurrent access unless explicitly synchronized. Specifically:
\
You must not call async operations on the same **socket** from multiple threads at the same time.
\
Boost.Asio guarantees thread safety only if all access to the **socket** occurs through the same io_context and from within its execution threads (or explicitly synchronized).
```C++
# The follwoing cconcept code is unsafe
# thread_1 | thread_2
# --------------------------------------+------------ ----------------------------
socket.async_receive(...); | ...
... | socket.async_write_some(...);
```

### Submit Arbitrary Tasks to `io_context`
To submit functions to the `io_context`, use the free functions `asio::dispatch`, `asio::post`, or `asio::defer`.
```C++
#include <boost/asio.hpp>
#include <iostream>

void my_task(const std::string& name) {
    std::cout << "Running task: " << name << " on thread " << std::this_thread::get_id() << "\n";
}

int main() {
    boost::asio::io_context io;

    auto exec = io.get_executor();

    // Dispatch: may execute immediately if on the same thread
    boost::asio::dispatch(exec, [] {
        my_task("dispatch");
    });

    // Post: always queued, runs later
    boost::asio::post(exec, [] {
        my_task("post");
    });

    // Defer: like post, but allows for composed ops to delay scheduling
    //        when you're composing multiple asynchronous operations and want to preserve handler affinity
    //        , such as strand or coroutine context.
    boost::asio::defer(exec, [] {
        my_task("defer");
    });

    io.run();
    return 0;
}
```

### io_context.run()
`io_context.run()` will block until one of the following occurs:
- It has been called and returned from both `print` handlers, the receive operation has completed (either successfully or with failure), and its `handle_async_receive` handler has been called and returned.
- The `io_context` is explicitly stopped using `io_context.stop()`.
- An exception is thrown from within a handler.

### io_context.poll()
The `poll` function "runs the event processing loop of the `io_context` object to execute ready handlers."
\
Take the following example, in each iteration, `poll` checks if any asynchronous operation (like the timer) is ready to execute. If there are any ready handlers, it will process them; otherwise, it moves on.
```C++
/*
The following the end automatically if all works have been done
*/
#include <boost/asio.hpp>
#include <iostream>

int main() {
    asio::io_context io_context;

    // Create a work guard to prevent io_context from exiting immediately
    asio::io_context::work work(io_context);

    // Create a steady timer
    asio::steady_timer timer(io_context, std::chrono::seconds(1));

    // Set a handler that will be called when the timer expires
    timer.async_wait([](const asio::error_code& ec) {
        if (!ec) {
            std::cout << "Timer expired!" << std::endl;
        }
    });

    // Run the event loop and print the counter, polling each time
    for (int x = 0; x < 42; ++x) {
        io_context.poll();  // Poll to process ready handlers

        // Print the current counter
        std::cout << "Counter: " << x << std::endl;
    }

    return 0;
}
```

## work guard
Use work guard to keep program alive until KILL
- Can use work guard to prevent io_context from exiting immediately
- work.reset();: After releasing the work object, the io_context is allowed to exit once all asynchronous work is completed, including the timer task.
```C++
#include <boost/asio.hpp>
#include <iostream>

int main() {
    asio::io_context io_context;

    // Create a work guard to keep io_context running
    std::shared_ptr<asio::io_context::work> work(new asio::io_context::work(io_context));

    // Add an asynchronous task (a timer that expires after 1 second)
    asio::steady_timer timer(io_context, std::chrono::seconds(1));
    timer.async_wait([](const asio::error_code& ec) {
        if (!ec) {
            std::cout << "Timer expired!" << std::endl;
        }
    });

    // Run the io_context to process asynchronous events
    io_context.run();
    // This line will  !!! Not !!! be printed after the timer expires
    std::cout << "Do you reckon this line displays?" << std::endl;

/* With work reset
    // Reset the work guard, allowing io_context.run() to stop once all tasks are done
    work.reset();
    // Run the io_context to process asynchronous events
    io_context.run();
    // This line will be printed after the timer expires
    std::cout << "Do you reckon this line displays?" << std::endl;
*/
    return 0;
}
```

## asio::require
Another way to release work and end the program 
- asio::require(..., outstanding_work.tracked) tells Asio to keep the io_context alive until you release the work object.
- Setting work = asio::any_io_executor(); is how you release that hold.
- Without any other pending tasks, io_context.run() will then exit normally.
```C++
#include <boost/asio.hpp>
#include <iostream>
#include <thread>

int main() {
    asio::io_context io_context;

    // Create a work-tracking executor that prevents io_context from exiting
    asio::any_io_executor work = asio::require(
        io_context.get_executor(),
        asio::execution::outstanding_work.tracked
    );

    // Launch a thread to run the io_context
    std::thread t([&]() {
        std::cout << "Running io_context...\n";
        io_context.run();
        std::cout << "io_context stopped.\n";
    });

    // Simulate some delay (no tasks yet)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Release the work guard — allows io_context to stop when done
    work = asio::any_io_executor();

    // Join the thread
    t.join();

    return 0;
}
```

# Boost.Asio desgin

## Multi-thread desgin

### Each thread holds an io_context
This approach is commonly used in multi-threaded applications to ensure that each thread has its own io_context instance, allowing for concurrent execution of asynchronous operations without contention.
```C++
#pragma once
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>

class AsioIOContextPool {
public:
    using IOContext = asio::io_context;
    using Work = asio::io_context::work;
    using WorkPtr = std::unique_ptr<Work>;

    // Constructor: initialize with hardware concurrency or given size
    AsioIOContextPool(std::size_t size = std::thread::hardware_concurrency())
        : ioContexts_(size),
          works_(size),
          nextIOContext_(0)
    {
        // Assign a work object to each io_context to keep it running
        for (std::size_t i = 0; i < size; ++i) {
            works_[i] = std::make_unique<Work>(ioContexts_[i]);
        }

        // Launch a thread for each io_context
        for (std::size_t i = 0; i < ioContexts_.size(); ++i) {
            threads_.emplace_back([this, i]() {
                ioContexts_[i].run();
            });
        }
    }

    // Get the next io_context in round-robin (simple but not the best)
    asio::io_context& getIOContext() {
        auto& context = ioContexts_[nextIOContext_++];
        if (nextIOContext_ >= ioContexts_.size()) {
            nextIOContext_ = 0;
        }
        return context;
    }

    // Stop all contexts and join threads
    void stop() {
        // release the work guard, which allows the io_context to exit its event loop when run() is called. This is a standard way to gracefully shut down an io_context without abruptly stopping it.
        for (auto& work : works_) {
            work.reset();  // Releases the work guard
        }
        // stop io_context
        for (auto& ctx : ioContexts_) {
            ctx.stop();
        }
        // join thread
        for (auto& t : threads_) {
            if (t.joinable())
                t.join();
        }
    }

    // Destructor
    ~AsioIOContextPool() {
        stop();
    }

private:
    std::vector<asio::io_context> ioContexts_;
    std::vector<WorkPtr> works_;
    std::vector<std::thread> threads_;
    std::size_t nextIOContext_;
};

int main() {
    AsioIOContextPool pool;

    // Create a timer using a context from the pool
    std::mutex mtx;
    for (auto i = 0; i < 3; i++)
    {
        asio::steady_timer timer{pool.getIOContext(), std::chrono::seconds{1}};
        timer.async_wait([&mtx](const asio::error_code &ec) {
            if (!ec) {
                std::lock_guard<std::mutex> lock(mtx);
                std::cout << "Hello, World!" << std::endl;
            }
            });
    }

    // Sleep to wait for the timer before exiting (since we don't call io.run() here)
    std::this_thread::sleep_for(std::chrono::seconds{3});
    
    return 0;  // pool will be destroyed, threads joined
}
```

### Each thread run() the same global io_context
Asio provides io_context::strand; if multiple event handlers are dispatched through the same strand object, these event handlers are guaranteed to execute in order.
```C++
#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>

class AsioThreadPool {
public:
    explicit AsioThreadPool(std::size_t size = std::thread::hardware_concurrency())
        : work_(std::make_unique<boost::asio::io_context::work>(io_context_)) {
        for (std::size_t i = 0; i < size; ++i) {
            threads_.emplace_back([this]() {
                io_context_.run();
            });
        }
    }

    boost::asio::io_context& getIOContext() {
        return io_context_;
    }

    void stop() {
        work_.reset();  // Allow io_context.run() to exit
        io_context_.stop();  // Stop the io_context
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    ~AsioThreadPool() {
        stop();
    }

private:
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::io_context::work> work_;
    std::vector<std::thread> threads_;
};

int main()
{
    AsioThreadPool pool(3);
    asio::io_context::strand strand{pool.getIOContext()};

    // Create a timer using a context from the pool
    for (auto i = 0; i < 3; i++)
    {
        asio::steady_timer timer{pool.getIOContext(), std::chrono::seconds{1}};
        timer.async_wait(
            strand.wrap(
                [] (const asio::error_code &ec) {
                std::cout << "Hello, World! " << std::endl;
                }));
    }

    // Sleep to wait for the timer before exiting (since we don't call io.run() here)
    std::this_thread::sleep_for(std::chrono::seconds{3});
    
    return 0;  // pool will be destroyed, threads joined
}
```

## Socket with Boost.Asio

### Common objects
- asio::ip::tcp::endpoint
  - An endpoint is a connection to an address using a port.
- asio::ip::tcp::acceptor
  - Synchronous accept operations are thread-safe if the underlying operating system calls are also thread-safe. This means that a single socket object is allowed
  - Performs concurrent invocations of a synchronous accept operation. Other synchronous operations, such as opening or closing, are not thread-safe.
- ...

### Blocking programming
Client (writing)
```C++
#include <boost/asio.hpp>
#include <iostream>

int main() {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);

        // Connect to the server
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), 3333);
        socket.connect(endpoint);

        // Send data
        std::string message = "Hello, Server!";
        boost::asio::write(socket, boost::asio::buffer(message));

        std::cout << "Message sent: " << message << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
```
Server (reading)
```C++
#include <boost/asio.hpp>
#include <iostream>

int main() {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::acceptor acceptor(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 3333));

        std::cout << "Server is listening on port 3333..." << std::endl;

        for (;;) {
            boost::asio::ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            // Receive data
            char data[128];
            size_t length = socket.read_some(boost::asio::buffer(data));
            std::cout << "Received: " << std::string(data, length) << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
```

### Asynchronous programming
Client (writing)
```C++
#include <boost/asio.hpp>
#include <iostream>
#include <memory>

void handle_connect(const boost::system::error_code& error) {
    if (!error) {
        std::cout << "Connected to server!" << std::endl;

        // Prepare the message to send
        std::string message = "Hello, Server!";
        boost::asio::async_write(*socket, boost::asio::buffer(message),
            [](const boost::system::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    std::cout << "Message sent: " << bytes_transferred << " bytes." << std::endl;
                } else {
                    std::cerr << "Error during send: " << error.message() << std::endl;
                }
            });
    } else {
        std::cerr << "Failed to connect: " << error.message() << std::endl;
    }
}

int main() {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);

        // Resolve the server address
        boost::asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve("127.0.0.1", "3333");

        // Asynchronously connect to the server
        boost::asio::async_connect(socket, endpoints,
            [](const boost::system::error_code& error, const boost::asio::ip::tcp::endpoint& endpoint) {
                handle_connect(error);
            });

        // Run the io_context to perform the asynchronous operations
        io_context.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
```
Server (reading)
```C++
#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <vector>

class Server {
public:
    Server(short port)
        : io_context_(),
          work_guard_(boost::asio::make_work_guard(io_context_)),
          acceptor_(io_context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
        std::cout << "Server started on port " << port << std::endl;
        start_accept();
    }

    ~Server() {
        stop();
    }

    void run() {
        std::vector<std::thread> threads;
        for (std::size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([this]() {
                try {
                    io_context_.run();
                } catch (const std::exception& e) {
                    std::cerr << "Exception in io_context::run: " << e.what() << std::endl;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }

    void stop() {
        work_guard_.reset(); // Allow io_context::run() to exit
        io_context_.stop();  // Stop the io_context
    }

private:
    void start_accept() {
        auto new_connection = std::make_shared<boost::asio::ip::tcp::socket>(io_context_); // socket
        acceptor_.async_accept(*new_connection,
            [this, new_connection](const boost::system::error_code& error) {
                if (!error) {
                    std::cout << "New connection established." << std::endl;
                    handle_accept(error, new_connection);
                } else {
                    std::cerr << "Accept failed: " << error.message() << std::endl;
                }
                start_accept(); // Continue accepting new connections
            });
    }

    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred,
                    std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        if (!error) {
            std::vector<char> data(bytes_transferred);
            socket->read_some(boost::asio::buffer(data));
            std::cout << "Received: " << std::string(data.begin(), data.end()) << std::endl;
        } else {
            std::cerr << "Error during receive: " << error.message() << std::endl;
        }
    }

    void handle_accept(const boost::system::error_code& error,
                    std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        if (!error) {
            std::cout << "Client connected!" << std::endl;

            // Start reading data asynchronously
            std::vector<char> data(128);
            socket->async_read_some(boost::asio::buffer(data),
                [socket](const boost::system::error_code& error, std::size_t bytes_transferred) {
                    handle_read(error, bytes_transferred, socket);
                });
        } else {
            std::cerr << "Accept failed: " << error.message() << std::endl;
        }
    }

    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

int main() {
    try {
        Server server(3333);
        server.run(); // Start the server and begin accepting connections
    } catch (const std::exception& e) {
        std::cerr << "Server exception: " << e.what() << std::endl;
    }
    return 0;
}
```

## Some reference
- https://www.boost.org/doc/libs/master/doc/html/boost_asio.html
- https://www.cnblogs.com/blizzard8204/p/17562607.html
