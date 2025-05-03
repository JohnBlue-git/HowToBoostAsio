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


