#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include "Socket.h"
#include "ZUtils.h"
#include "format.h"

namespace dcpp {

class SocketInputStream {
public:
    enum Modes { MODE_LINE, MODE_ZPIPE, MODE_DATA };
    struct Handlers {
        std::function<void(const std::string&)> line;
        std::function<void(uint8_t*, size_t)> data;
        std::function<void()> modeChange;
        std::function<bool()> active;
    };

    Modes getMode() const { return mode; }
    void setMode(Modes value, size_t bytesReturned = 0) {
        if(mode == value)
            return;
        if(value == MODE_ZPIPE) {
            if(filter)
                throw SocketException("Nested compressed command stream");
            filter = std::make_unique<UnZFilter>();
        } else if(value == MODE_LINE) {
            rollback = bytesReturned;
        }
        mode = value;
    }

    void setDataMode(int64_t bytes = -1) {
        if(bytes < -1)
            throw SocketException("Invalid data length");
        if(mode != MODE_DATA)
            dataReturnMode = mode;
        dataBytes = bytes;
        mode = MODE_DATA;
    }

    void feed(uint8_t* bytes, size_t size, char& separator, size_t limit, const Handlers& handlers) {
        size_t pos = 0;
        bool drain = false;
        while((pos < size || drain) && handlers.active()) {
            if(!filter) {
                pos += consume(bytes + pos, size - pos, separator, limit, handlers, false);
                continue;
            }

            std::array<uint8_t, 1024> output;
            size_t used = size - pos;
            size_t produced = output.size();
            // An empty drain can yield buffered output without consuming wire bytes.
            (*filter)(used ? bytes + pos : nullptr, used, output.data(), produced);
            pos += used;
            const bool ended = filter->finished();
            consume(output.data(), produced, separator, limit, handlers, true);
            if(ended) {
                filter.reset();
                if(mode == MODE_ZPIPE) mode = MODE_LINE;
                if(dataReturnMode == MODE_ZPIPE) dataReturnMode = MODE_LINE;
            }
            drain = !ended && produced == output.size();
            if(used == 0 && produced == 0) {
                if(pos < size)
                    throw SocketException(_("Error during decompression"));
                break;
            }
        }
    }

private:
    Modes mode = MODE_LINE;
    Modes dataReturnMode = MODE_LINE;
    std::unique_ptr<UnZFilter> filter;
    int64_t dataBytes = 0;
    size_t rollback = 0;
    std::string line;

    size_t consume(uint8_t* bytes, size_t size, char& separator, size_t limit,
            const Handlers& handlers, bool decoded) {
        size_t pos = 0;
        while((pos < size || (mode == MODE_DATA && dataBytes == 0)) && handlers.active()) {
            if(!decoded && filter)
                break;
            if(mode == MODE_DATA) {
                if(dataBytes == 0) {
                    mode = dataReturnMode;
                    handlers.modeChange();
                    continue;
                }
                const size_t available = size - pos;
                if(dataBytes == -1) {
                    handlers.data(bytes + pos, available);
                    if(!handlers.active()) return size;
                    if(rollback > available)
                        throw SocketException("Invalid data rollback");
                    pos += available - rollback;
                    rollback = 0;
                } else {
                    const auto count = static_cast<size_t>(std::min(uint64_t(dataBytes), uint64_t(available)));
                    handlers.data(bytes + pos, count);
                    if(!handlers.active()) return size;
                    pos += count;
                    dataBytes -= count;
                }
            } else {
                if(separator == 0) separator = bytes[pos] == '$' ? '|' : '\n';
                const std::string_view remaining(reinterpret_cast<char*>(bytes + pos), size - pos);
                const auto end = remaining.find(separator);
                const auto count = end == std::string_view::npos ? remaining.size() : end;
                if(line.size() > limit || count > limit - line.size())
                    throw SocketException(_("Maximum command length exceeded"));
                line.append(remaining.data(), count);
                pos += count;
                if(end != std::string_view::npos) {
                    ++pos;
                    auto command = std::move(line);
                    line.clear();
                    if(!command.empty()) handlers.line(command);
                }
            }
        }
        return pos;
    }
};

} // namespace dcpp
