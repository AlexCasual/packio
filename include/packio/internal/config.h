// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_CONFIG_H
#define PACKIO_CONFIG_H

#include <unordered_map>

#if defined(PACKIO_STANDALONE_ASIO)
#include <asio.hpp>
#else // defined(PACKIO_STANDALONE_ASIO)
#include <boost/asio.hpp>
#endif // defined(PACKIO_STANDALONE_ASIO)

#define PACKIO_HAS_MSGPACK __has_include(<msgpack.hpp>)
#define PACKIO_HAS_NLOHMANN_JSON __has_include(<nlohmann/json.hpp>)

namespace packio {

#if defined(PACKIO_STANDALONE_ASIO)
namespace net = ::asio;
using error_code = std::error_code;
using system_error = std::system_error;
using error_category = std::error_category;
#else // defined(PACKIO_STANDALONE_ASIO)
namespace net = ::boost::asio;
using error_code = boost::system::error_code;
using system_error = boost::system::system_error;
using error_category = boost::system::error_category;
#endif // defined(PACKIO_STANDALONE_ASIO)

#if defined(BOOST_ASIO_HAS_CO_AWAIT) || defined(ASIO_HAS_CO_AWAIT) \
    || defined(PACKIO_DOCUMENTATION)
#define PACKIO_HAS_CO_AWAIT 1
#endif

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) || defined(ASIO_HAS_LOCAL_SOCKETS) \
    || defined(PACKIO_DOCUMENTATION)
#define PACKIO_HAS_LOCAL_SOCKETS 1
#endif

#if defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN)
#define PACKIO_DEFAULT_COMPLETION_TOKEN(e) \
    BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(e)
#elif defined(ASIO_DEFAULT_COMPLETION_TOKEN)
#define PACKIO_DEFAULT_COMPLETION_TOKEN(e) ASIO_DEFAULT_COMPLETION_TOKEN(e)
#elif defined(PACKIO_DOCUMENTATION)
#define PACKIO_DEFAULT_COMPLETION_TOKEN(e) \
    = typename net::default_completion_token<e>::type()
#else
#define PACKIO_DEFAULT_COMPLETION_TOKEN(e)
#endif // defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN)

#if defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)
#define PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(e) \
    BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#elif defined(ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)
#define PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(e) \
    ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#elif defined(PACKIO_DOCUMENTATION)
#define PACKIO_DEFAULT_COMPLETION_TOKEN(e) \
    = typename net::default_completion_token<e>::type
#else
#define PACKIO_DEFAULT_COMPLETION_TOKEN_TYPE(e)
#endif // defined(BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE)

#if defined(BOOST_ASIO_COMPLETION_TOKEN_FOR)
#define PACKIO_COMPLETION_TOKEN_FOR(s) BOOST_ASIO_COMPLETION_TOKEN_FOR(s)
#elif defined(ASIO_COMPLETION_TOKEN_FOR)
#define PACKIO_COMPLETION_TOKEN_FOR(s) ASIO_COMPLETION_TOKEN_FOR(s)
#else
#define PACKIO_COMPLETION_TOKEN_FOR(s) typename
#endif // defined (BOOST_ASIO_COMPLETION_TOKEN_FOR)

template <typename... Args>
using default_map = std::unordered_map<Args...>;
using default_mutex = std::mutex;

} // packio

#endif // PACKIO_CONFIG_H
