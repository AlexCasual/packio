// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef PACKIO_NL_JSON_RPC_INCREMENTAL_BUFFERS_H
#define PACKIO_NL_JSON_RPC_INCREMENTAL_BUFFERS_H

#include <cassert>
#include <deque>
#include <optional>
#include <string>

namespace packio {
namespace nl_json_rpc {

class incremental_buffers {
public:
    std::size_t available_buffers() const
    { //
        return serialized_objects_.size();
    }

    std::optional<std::string> get_parsed_buffer()
    {
        if (serialized_objects_.empty()) {
            return std::nullopt;
        }

        auto buffer = std::move(serialized_objects_.front());
        serialized_objects_.pop_front();
        return buffer;
    }

    void feed(std::string_view data)
    {
        reserve_in_place_buffer(data.size());
        std::copy(begin(data), end(data), in_place_buffer());
        in_place_buffer_consumed(data.size());
    }

    char* in_place_buffer()
    { //
        return raw_buffer_.data() + buffer_.size();
    }

    std::size_t in_place_buffer_capacity() const
    { //
        return raw_buffer_.size() - buffer_.size();
    }

    void in_place_buffer_consumed(std::size_t bytes)
    {
        if (bytes == 0) {
            return;
        }
        incremental_parse(bytes);
    }

    void reserve_in_place_buffer(std::size_t bytes)
    {
        if (in_place_buffer_capacity() >= bytes) {
            return;
        }
        raw_buffer_.resize(buffer_.size() + bytes);
    }

private:
    void incremental_parse(std::size_t bytes)
    {
        if (buffer_.empty()) {
            std::string_view new_data{in_place_buffer(), bytes};
            auto first_pos = new_data.find_first_of("{[");
            if (first_pos == std::string::npos) {
                return;
            }

            initialize(new_data[first_pos]);
        }

        std::size_t token_pos = buffer_.size();
        buffer_ = std::string_view{raw_buffer_.data(), buffer_.size() + bytes};

        while (true) {
            token_pos = buffer_.find_first_of(tokens_, token_pos + 1);
            if (token_pos == std::string::npos) {
                break;
            }

            char token = buffer_[token_pos];
            if (token == '"' && !is_escaped(token_pos)) {
                in_string_ = !in_string_;
                continue;
            }

            if (in_string_) {
                continue;
            }

            if (token == last_char_) {
                if (--depth_ == 0) {
                    // found objet, store the interesting part of the buffer
                    std::size_t buffer_size = token_pos + 1;
                    std::string new_raw_buffer = raw_buffer_.substr(buffer_size);
                    raw_buffer_.resize(buffer_size);
                    serialized_objects_.push_back(std::move(raw_buffer_));
                    // then clear the buffer and re-feed the rest
                    std::size_t bytes_left = buffer_.size() - buffer_size;
                    raw_buffer_ = std::move(new_raw_buffer);
                    buffer_ = std::string_view{raw_buffer_.data(), bytes_left};
                    token_pos -= buffer_size;
                }
            }
            else {
                assert(token == first_char_);
                ++depth_;
            }
        }
    }

    bool is_escaped(std::size_t pos)
    {
        bool escaped = false;
        while (pos-- > 0u) {
            if (buffer_[pos] == '\\') {
                escaped = !escaped;
            }
            else {
                break;
            }
        }
        return escaped;
    }

    void initialize(char first_char)
    {
        first_char_ = first_char;
        if (first_char_ == '{') {
            last_char_ = '}';
            tokens_ = "{}\"";
        }
        else {
            assert(first_char_ == '[');
            last_char_ = ']';
            tokens_ = "[]\"";
        }
        depth_ = 1;
        in_string_ = false;
    }

    bool in_string_;
    int depth_;
    char first_char_;
    char last_char_;
    const char* tokens_;

    std::string_view buffer_;
    std::string raw_buffer_;

    std::deque<std::string> serialized_objects_;
};

} // nl_json_rpc
} // packio

#endif // PACKIO_NL_JSON_RPC_INCREMENTAL_BUFFERS_H