#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <packio/client.h>
#include <packio/server.h>

#include "misc.h"

using namespace std::chrono;
using namespace boost::asio;
using namespace packio;
using std::this_thread::sleep_for;

typedef ::testing::Types<boost::asio::ip::tcp, boost::asio::local::stream_protocol>
    ClientImplementations;

template <class T>
class Client : public ::testing::Test {
protected:
    using client_type = client<T>;
    using server_type = server<T>;
    using endpoint_type = typename T::endpoint;
    using socket_type = typename T::socket;
    using acceptor_type = typename T::acceptor;

    Client()
        : server_{acceptor_type(io_, get_endpoint<endpoint_type>())},
          client_{socket_type{io_}}
    {
    }

    ~Client()
    {
        io_.stop();
        if (runner_.joinable()) {
            runner_.join();
        }
    }

    void async_run()
    {
        runner_ = std::thread{[this] {
            DEBUG("running");
            io_.run();
            DEBUG("work done");
        }};
    }

    void connect()
    {
        auto ep = server_.acceptor().local_endpoint();
        client_.socket().connect(ep);
    }

    template <typename... Args>
    auto future_notify(std::string_view name, Args&&... args)
    {
        std::promise<boost::system::error_code> p;
        auto f = p.get_future();
        client_.async_notify(
            name,
            std::forward_as_tuple(args...),
            [p = std::move(p)](auto ec) mutable { p.set_value(ec); });
        return f;
    }

    template <typename... Args>
    auto future_call(std::string_view name, Args&&... args)
    {
        std::promise<std::tuple<boost::system::error_code, msgpack::object>> p;
        auto f = p.get_future();
        client_.async_call(
            name,
            std::forward_as_tuple(args...),
            [p = std::move(p)](auto ec, auto result) mutable {
                p.set_value({ec, std::move(result)});
            });
        return f;
    }

    boost::asio::io_context io_;
    server_type server_;
    client_type client_;
    std::thread runner_;
};

TYPED_TEST_CASE(Client, ClientImplementations);

TYPED_TEST(Client, test_connect)
{
    this->connect();
    ASSERT_TRUE(this->client_.socket().is_open());
}

TYPED_TEST(Client, test_typical_usage)
{
    {
        latch connected{1};
        this->server_.async_serve([&](auto ec, auto session) {
            ASSERT_FALSE(ec);
            session->start();
            connected.count_down();
        });
        this->connect();
        this->async_run();

        ASSERT_TRUE(this->client_.socket().is_open());
        ASSERT_TRUE(connected.wait_for(std::chrono::seconds{1}));
    }

    std::atomic<int> call_arg_received{0};
    latch call_latch{0};
    this->server_.dispatcher()->add_async(
        "echo", [&](packio::completion_handler handler, int i) {
            call_arg_received = i;
            call_latch.count_down();
            handler(i);
        });

    {
        call_latch.reset(1);

        auto f = this->future_notify("echo", 42);
        ASSERT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{1}));
        auto ec = f.get();
        ASSERT_FALSE(ec);
        ASSERT_TRUE(call_latch.wait_for(std::chrono::seconds{1}));
        ASSERT_EQ(42, call_arg_received.load());
    }

    {
        call_latch.reset(1);
        call_arg_received = 0;

        auto f = this->future_call("echo", 42);
        ASSERT_EQ(std::future_status::ready, f.wait_for(std::chrono::seconds{1}));
        auto [ec, result] = f.get();
        ASSERT_FALSE(ec);
        ASSERT_EQ(42, result.template as<int>());
        ASSERT_EQ(42, call_arg_received.load());
    }
}

TYPED_TEST(Client, test_timeout)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    std::mutex mtx;

    std::list<packio::completion_handler> pending;
    this->server_.dispatcher()->add_async(
        "block", [&](packio::completion_handler handler) {
            std::unique_lock l{mtx};
            pending.push_back(std::move(handler));
        });
    this->server_.dispatcher()->add_async(
        "unblock", [&](packio::completion_handler handler) {
            std::unique_lock l{mtx};
            for (auto& handler : pending) {
                handler();
            }
            pending.clear();
            handler();
        });

    {
        this->client_.set_timeout(std::chrono::milliseconds{1});
        auto f = this->future_call("block");
        auto [ec, res] = f.get();
        ASSERT_EQ(packio::error::timeout, ec);
        ASSERT_EQ(msgpack::type::STR, res.type);
    }

    {
        std::unique_lock l{mtx};
        pending.clear();
    }

    {
        this->client_.set_timeout(std::chrono::milliseconds{0});
        auto f = this->future_call("block");
        auto [ec, res] = this->future_call("unblock").get();
        auto [ec2, res2] = f.get();
        (void)res;
        (void)res2;
        ASSERT_FALSE(ec);
        ASSERT_FALSE(ec2);
    }

    this->io_.stop();
}

TYPED_TEST(Client, test_server_functions)
{
    // this just needs to compile
    this->server_.dispatcher()->add_async(
        "f001", [](packio::completion_handler handler) { handler(); });
    this->server_.dispatcher()->add_async(
        "f002", [](packio::completion_handler handler) { handler(42); });
    this->server_.dispatcher()->add_async(
        "f003", [](packio::completion_handler handler, int) { handler(); });
    this->server_.dispatcher()->add_async(
        "f004", [](packio::completion_handler handler, int i) { handler(i); });
    this->server_.dispatcher()->add_async(
        "f005",
        [](packio::completion_handler handler, std::string s) { handler(s); });
    this->server_.dispatcher()->add_async(
        "f006", [](packio::completion_handler handler, int i, std::string) {
            handler(i);
        });

    this->server_.dispatcher()->add("f011", []() {});
    this->server_.dispatcher()->add("f012", []() { return 42; });
    this->server_.dispatcher()->add("f013", [](int) {});
    this->server_.dispatcher()->add("f014", [](int i) { return i; });
    this->server_.dispatcher()->add("f015", [](std::string s) { return s; });
    this->server_.dispatcher()->add("f016", [](int i, std::string) { return i; });
}

TYPED_TEST(Client, test_dispatcher)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "f001", [](packio::completion_handler handler) { handler(); }));
    ASSERT_TRUE(this->server_.dispatcher()->add("f002", []() {}));

    ASSERT_FALSE(this->server_.dispatcher()->add_async(
        "f001", [](packio::completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_.dispatcher()->add_async(
        "f002", [](packio::completion_handler handler) { handler(); }));
    ASSERT_FALSE(this->server_.dispatcher()->add("f001", []() {}));
    ASSERT_FALSE(this->server_.dispatcher()->add("f002", []() {}));

    {
        auto [ec, result] = this->future_call("f001").get();
        (void)result;
        ASSERT_FALSE(ec);
    }
    {
        auto [ec, result] = this->future_call("f002").get();
        (void)result;
        ASSERT_FALSE(ec);
    }

    ASSERT_TRUE(this->server_.dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));
    auto known = this->server_.dispatcher()->known();
    ASSERT_EQ(
        (std::set<std::string>{"f001", "f002"}),
        std::set<std::string>(begin(known), end(known)));

    this->server_.dispatcher()->remove("f001");
    {
        auto [ec, result] = this->future_call("f001").get();
        (void)result;
        ASSERT_EQ(packio::error::call_error, ec);
    }

    ASSERT_FALSE(this->server_.dispatcher()->has("f001"));
    ASSERT_TRUE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));

    ASSERT_EQ(1u, this->server_.dispatcher()->clear());

    ASSERT_FALSE(this->server_.dispatcher()->has("f001"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f002"));
    ASSERT_FALSE(this->server_.dispatcher()->has("f003"));
}

TYPED_TEST(Client, test_move_only)
{
    // this test just needs to compile
    this->connect();

    this->server_.dispatcher()->add_async(
        "f001", [ptr = std::unique_ptr<int>{}](packio::completion_handler) {});
    this->server_.dispatcher()->add("f002", [ptr = std::unique_ptr<int>{}]() {});

    this->server_.async_serve([ptr = std::unique_ptr<int>{}](auto, auto) {});

    this->client_.async_notify("f001", [ptr = std::unique_ptr<int>{}](auto) {});
    this->client_.async_call(
        "f001", [ptr = std::unique_ptr<int>{}](auto, auto) {});

    static_assert(std::is_move_assignable_v<packio::completion_handler>);
    static_assert(std::is_move_constructible_v<packio::completion_handler>);
}

TYPED_TEST(Client, test_shared_dispatcher)
{
    using server_type = typename std::decay_t<decltype(*this)>::server_type;
    using client_type = typename std::decay_t<decltype(*this)>::client_type;
    using socket_type = typename std::decay_t<decltype(*this)>::socket_type;
    using endpoint_type = typename std::decay_t<decltype(*this)>::endpoint_type;
    using acceptor_type = typename std::decay_t<decltype(*this)>::acceptor_type;

    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    // server2 is a different server but shares the same dispatcher as server_
    server_type server2{acceptor_type(this->io_, get_endpoint<endpoint_type>()),
                        this->server_.dispatcher()};

    client_type client2{socket_type{this->io_}};

    auto ep = server2.acceptor().local_endpoint();
    client2.socket().connect(ep);
    server2.async_serve_forever();

    ASSERT_NE(
        this->server_.acceptor().local_endpoint(),
        server2.acceptor().local_endpoint());

    latch l{2};
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "inc", [&](packio::completion_handler handler) {
            l.count_down();
            handler();
        }));

    this->client_.async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });
    client2.async_notify("inc", [](auto ec) { ASSERT_FALSE(ec); });

    ASSERT_TRUE(l.wait_for(std::chrono::seconds{1}));
}

TYPED_TEST(Client, test_errors_async)
{
    constexpr auto kErrorMessage{"error message"};

    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "error", [&](packio::completion_handler handler) {
            handler.set_error(kErrorMessage);
        }));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "empty_error",
        [](packio::completion_handler handler) { handler.set_error(); }));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "no_result", [&](packio::completion_handler) {}));
    ASSERT_TRUE(this->server_.dispatcher()->add_async(
        "add", [](packio::completion_handler handler, int a, int b) {
            handler(a + b);
        }));

    {
        auto [ec, res] = this->future_call("error").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ(kErrorMessage, res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("empty_error").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Error during call", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("no_result").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Call finished with no result", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("unexisting").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Unknown function", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add", 1, "two").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add", 1, 2, 3).get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
}

TYPED_TEST(Client, test_errors_sync)
{
    this->server_.async_serve_forever();
    this->connect();
    this->async_run();

    ASSERT_TRUE(this->server_.dispatcher()->add(
        "add", [](int a, int b) { return a + b; }));

    {
        auto [ec, res] = this->future_call("unexisting").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Unknown function", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add", 1, "two").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add").get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
    {
        auto [ec, res] = this->future_call("add", 1, 2, 3).get();
        ASSERT_EQ(packio::error::call_error, ec);
        ASSERT_EQ("Incompatible arguments", res.template as<std::string>());
    }
}

int main(int argc, char** argv)
{
    ::spdlog::default_logger()->set_level(::spdlog::level::trace);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
