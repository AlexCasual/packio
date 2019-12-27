# PackIO, the header-only msgpack-RPC library built for Boost.Asio

This library requires C++17 and is designed as an extension Boost.Asio. It will let you built asynchronous servers or client for msgpack-RPC.

The library is still under development and is therefore subject heavy API changes.

# Primer

```cpp
// Declare a server and a client, sharing the same io_context
boost::asio::io_context io;
ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
packio::server<ip::tcp> server{ip::tcp::acceptor{io, bind_ep}};
packio::client<ip::tcp> client{ip::tcp::socket{io}};
```

```cpp
// Declare a synchronous callback
server.dispatcher()->add("sum", [](int a, int b) { return a+b; });
// Declare an asynchronous callback
server.dispatcher()->add_async("multiply",
    [&](packio::completion_handler complete, int a, int b) {
        complete(a*b);
    });
```

```cpp
// Connect the client
client.socket().connect(server.acceptor().local_endpoint());
// Accept connections
server.async_serve_forever();
// Run the io_context
std::thread thread{[&] { io.run(); }};
```

```cpp
// Make an asynchronous call
client.async_call("add", std::make_tuple(42, 24),
    [&](boost::system::error_code, msgpack::object r) {
        std::cout << "The result is: " << r.as<int>() << std::endl;
    });
```

# Bonus

Let's compute fibonacci's numbers recursively using PackIO on a single thread.

```cpp
#include <iostream>
#include <boost/asio.hpp>
#include <packio/client.h>
#include <packio/server.h>

namespace ip = boost::asio::ip;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "I require one argument" << std::endl;
        return 1;
    }
    const int n = std::atoi(argv[1]);

    boost::asio::io_context io;
    ip::tcp::endpoint bind_ep{ip::make_address("127.0.0.1"), 0};
    packio::server<ip::tcp> server{ip::tcp::acceptor{io, bind_ep}};
    packio::client<ip::tcp> client{ip::tcp::socket{io}};

    server.dispatcher()->add_async(
        "fibonacci", [&](packio::completion_handler complete, int n) {
            if (n == 0) {
                complete(0);
            }
            else if (n == 1) {
                complete(1);
            }
            else {
                client.async_call(
                    "fibonacci",
                    std::make_tuple(n - 1),
                    [=, &client, complete = std::move(complete)](
                        boost::system::error_code,
                        msgpack::object result1) mutable {
                        int r1 = result1.as<int>();
                        client.async_call(
                            "fibonacci",
                            std::make_tuple(n - 2),
                            [=, complete = std::move(complete)](
                                boost::system::error_code,
                                msgpack::object result2) mutable {
                                int r2 = result2.as<int>();
                                complete(r1 + r2);
                            });
                    });
            }
        });

    client.socket().connect(server.acceptor().local_endpoint());
    server.async_serve_forever();

    std::optional<int> result;

    client.async_call(
        "fibonacci",
        std::make_tuple(n),
        [&](boost::system::error_code, msgpack::object r) {
            result = r.as<int>();
        });

    while (!result) {
        io.run_one();
    }

    std::cout << "F{" << n << "} = " << *result << std::endl;

    return 0;
}
```