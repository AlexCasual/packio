#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <packio/packio.h>

#include "misc.h"

using namespace std::chrono_literals;
using namespace packio::net;
using namespace packio;

typedef ::testing::Types<
#if defined(PACKIO_HAS_LOCAL_SOCKETS)
    local::stream_protocol,
#endif // defined(PACKIO_HAS_LOCAL_SOCKETS)
    ip::tcp>
    Protocols;

template <class Protocol>
class Server : public ::testing::Test {
protected:
    using server_type = packio::msgpack_rpc::server<typename Protocol::acceptor>;
    using client_type = packio::msgpack_rpc::client<typename Protocol::socket>;
    using endpoint_type = typename Protocol::endpoint;
    using socket_type = typename Protocol::socket;
    using acceptor_type = typename Protocol::acceptor;

    Server()
        : server_{std::make_shared<server_type>(
            acceptor_type(io_, get_endpoint<endpoint_type>()))}
    {
        server_->async_serve_forever();
    }

    ~Server()
    {
        io_.stop();
        for (auto& runner : runners_) {
            runner.join();
        }
    }

    void run(int threads)
    {
        for (int i = 0; i < threads; ++i) {
            runners_.emplace_back([this] { io_.run(); });
        }
    }

    endpoint_type local_endpoint()
    {
        return server_->acceptor().local_endpoint();
    }

    std::vector<std::shared_ptr<client_type>> create_clients(int n)
    {
        std::vector<std::shared_ptr<client_type>> clients;
        for (int i = 0; i < n; ++i) {
            clients.emplace_back(std::make_shared<client_type>(socket_type{io_}));
        }
        return clients;
    }

    std::vector<std::shared_ptr<client_type>> create_connected_clients(int n)
    {
        std::vector<std::shared_ptr<client_type>> clients = create_clients(n);
        for (auto& client : clients) {
            client->socket().connect(local_endpoint());
        }
        return clients;
    }

    io_context io_;
    std::shared_ptr<server_type> server_;
    std::vector<std::thread> runners_;
};

TYPED_TEST_SUITE(Server, Protocols);

TYPED_TEST(Server, test_same_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    this->server_->dispatcher()->add("double", [&](int i) {
        calls.count_down();
        return 2 * i;
    });
    this->run(kNThreads);
    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        for (auto& client : clients) {
            client->async_call(
                "double", std::make_tuple(42), [&](auto ec, auto res) {
                    ASSERT_FALSE(ec);
                    ASSERT_EQ(84, get<int>(res.result));
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(10s));
    ASSERT_TRUE(calls.wait_for(10s));
}

TYPED_TEST(Server, test_big_msg)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();
    const std::string big_msg(100'000, '0');

    latch done{kNCalls * kNClients};
    latch calls{kNCalls * kNClients};
    this->server_->dispatcher()->add("echo", [&](std::string s) {
        EXPECT_EQ(big_msg, s);
        calls.count_down();
        return s;
    });
    this->run(kNThreads);
    auto clients = this->create_connected_clients(kNClients);

    for (int i = 0; i < kNCalls; ++i) {
        for (auto& client : clients) {
            client->async_call(
                "echo", std::make_tuple(big_msg), [&](auto ec, auto res) {
                    ASSERT_FALSE(ec);
                    ASSERT_EQ(big_msg, get<std::string>(res.result));
                    done.count_down();
                });
        }
    }

    ASSERT_TRUE(done.wait_for(10s));
    ASSERT_TRUE(calls.wait_for(10s));
}

TYPED_TEST(Server, test_many_func)
{
    constexpr int kNCalls{100};
    const int kNThreads = 10 * std::thread::hardware_concurrency();
    const int kNClients = 2 * std::thread::hardware_concurrency();

    latch done{kNCalls * kNClients * 2};
    latch calls{kNCalls * kNClients * 2};
    for (int i = 0; i < kNClients; ++i) {
        this->server_->dispatcher()->add(std::to_string(i), [&](int n) {
            calls.count_down();
            return n;
        });
    }

    this->run(kNThreads);

    auto clients = this->create_connected_clients(kNClients);
    for (int i = 0; i < kNCalls; ++i) {
        int j = 0;
        for (auto& client : clients) {
            std::string name = std::to_string(j++);
            client->async_call(name, std::make_tuple(42), [&](auto ec, auto res) {
                ASSERT_FALSE(ec);
                ASSERT_EQ(42, get<int>(res.result));
                done.count_down();
            });
            client->async_notify(name, std::make_tuple(42), [&](auto ec) {
                ASSERT_FALSE(ec);
                done.count_down();
            });
        }
    }

    ASSERT_TRUE(done.wait_for(10s));
    ASSERT_TRUE(calls.wait_for(10s));
}
