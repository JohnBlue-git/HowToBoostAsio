## openBMC bmcweb is single io_context design
There are mainly two kind of requesst be handled using io_context in bmcweb.
\
And they both get and use the same io_context in bmcweb
- HTTP request handling
- DBus request handling
Here are the singleton function in bmcweb (bmcweb/include/io_context_singleton.hpp)
```console
# that take advantage of the static feature
inline boost::asio::io_context& getIoContext()
{
    static boost::asio::io_context io;
    return io;
}
```

## How io_context.run()
bmcweb/src/webserver_run.cpp
```c++
int run()
{
    ...

    app.run();

    systemBus->request_name("xyz.openbmc_project.bmcweb");

    io.run();

    crow::connections::systemBus = nullptr;

    return 0;
}
```
It is possible to use a pretty way to improve it
```c++
int run()
{
    auto io = std::make_shared<boost::asio::io_context>();
    App app(io);

...

    app.run();
    // io->run();
    // Run the io_context in the thread pool
    boost::asio::thread_pool pool(2);
    boost::asio::post(pool, [io](){ io->run(); });
    boost::asio::post(pool, [io](){ io->run(); });
    pool.join();

    crow::connections::systemBus = nullptr;

    return 0;
}
```

## How io_context relete to HTTP handling
bmcweb/http/http_server.hpp
```c++
    void doAccept()
    {
        SocketPtr socket = std::make_unique<Adaptor>(getIoContext());
        // Keep a raw pointer so when the socket is moved, the pointer is still
        // valid
        Adaptor* socketPtr = socket.get();
        for (Acceptor& accept : acceptors)
        {
            accept.acceptor.async_accept(
                *socketPtr,
                std::bind_front(&self_t::afterAccept, this, std::move(socket),
                                accept.httpType));
        }
    }
```
However, in the original source code, the asynchronous accept operations are initiated without wrapping the completion handlers in a strand. This means that if afterAccept is invoked concurrently from multiple threads, it could lead to race conditions when accessing shared resources (socket).​
\
Recommended Approach with strand
\
(caution: getIoContext() have to be revised for the entire project, the following are just example usage of strand)
```c++
void doAccept()
{
    auto strand = boost::asio::make_strand(getIoContext());
    SocketPtr socket = std::make_unique<Adaptor>(strand);

    // Keep a raw pointer so when the socket is moved, the pointer remains valid
    Adaptor* socketPtr = socket.get();

    for (Acceptor& accept : acceptors)
    {
        accept.acceptor.async_accept(
            *socketPtr,
            boost::asio::bind_executor(
                strand, 
                std::bind_front(&self_t::afterAccept, this, std::move(socket), accept.httpType)));
    }
}
```
The global way to make_strand() io_context is to replace getIoContext() with getStrand().
\
But it is still based on the verion of BOOST library, because older version didn't support boost::asio::make_strand().
```console
inline boost::asio::strand<boost::asio::io_context::executor_type> getStrand()
{
    static boost::asio::io_context io;
    static boost::asio::strand<boost::asio::io_context::executor_type> strand(io.get_executor());
    return strand;
}
```
Look deeper to Multiple Strands VS Global Strands
- Multiple Strands from the Same `io_context`
  - This is a common approach when different parts of your application need their own serialization guarantees.
  - Each strand ensures that handlers associated with it execute without concurrent overlap, even if other handlers (from different strands) are running in parallel.
- Using a Global Strand
  - A single strand shared across multiple parts of your application ensures strict sequential execution of all handlers using it.
  - If your entire codebase needs to serialize operations that must never run in parallel, a global strand makes sense.
  - However, overusing a single strand might introduce bottlenecks if tasks don’t truly require strict sequencing.
- Best Practice
  - **Use multiple strands for different components** that need local thread-safety.
    - Like the cases to requesting DBUS
  - **Avoid a global strand unless truly required**, because it may serialize operations unnecessarily and limit performance.
  - If multiple handlers belong to the same logical group (like managing a shared resource), use a **dedicated strand** for that group.

## How io_context relete to DBus handling
bmcweb/src/webserver_run.cpp
```c++
    std::shared_ptr<sdbusplus::asio::connection> systemBus =
        std::make_shared<sdbusplus::asio::connection>(io);
    crow::connections::systemBus = systemBus.get();
```
bmcweb/src/dbus_utility.cpp
```c++
    sdbusplus::asio::getAllProperties(*crow::connections::systemBus, service,
                                      objectPath, interface,
                                      std::move(callback));
```

## Look deeper on how boost::asio::strand is used in `sdbusplus`
In the implementation of sdbusplus::asio::connection, asynchronous operations such as async_send and async_method_call_timed are initiated using boost::asio::bind_executor. This function binds the completion handlers to a specific executor, which is typically a boost::asio::strand. By binding handlers to a strand, the library ensures that these handlers are executed serially, even if they are posted from multiple threads. (include/sdbusplus/asio/connection.hpp - openbmc/sdbusplus - Gitiles). This approach effectively serializes the execution of handlers, preventing concurrent access to shared resources and ensuring thread safety in asynchronous operations.

## Look deeper on multi-threading c onsiderations in `sdbusplus`
While sdbusplus::asio::connection provides thread safety for individual asynchronous operations, it's important to note that the underlying DBus connection (sd_bus) is not inherently thread-safe. Therefore, when using multiple threads, each thread should operate on its own sdbusplus::asio::connection instance to avoid undefined behavior.
In summary, sdbusplus::asio::connection leverages boost::asio::strand to ensure thread-safe execution of asynchronous DBus operations, making it suitable for multi-threaded applications with proper instance management.
