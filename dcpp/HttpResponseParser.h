#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace dcpp {

// Streaming response framing for strict GET clients. Does not decode content,
// follow redirects, allocate body-sized buffers, or decide whether URLs are safe.
class HttpResponseParser {
public:
    struct Limits {
        uint64_t bodyBytes = 1024 * 1024;
        size_t headerBytes = 64 * 1024;
    };
    using BodySink = std::function<void(const uint8_t*, size_t)>;

    explicit HttpResponseParser(Limits limits, bool allowUnavailable = false);
    void feed(const uint8_t* data, size_t size, const BodySink& sink);
    void eof();
    bool complete() const;
    bool redirect() const;
    // Status classification only; framing is not trusted until complete().
    bool unavailable() const;
    unsigned status() const { return statusCode; }
    uint64_t received() const { return bodyReceived; }
    const std::string& location() const { return redirectLocation; }
    const std::string& contentType() const { return mime; }

private:
    enum class State { Status, Headers, FixedBody, CloseBody, ChunkSize,
        ChunkBody, ChunkCR, ChunkLF, Trailers, Complete, Redirect, Failed };
    void finishLine();
    void parseHeader(bool trailer);
    [[noreturn]] void fail(const char* message);

    Limits limits;
    bool allowUnavailable;
    State state = State::Status;
    std::string line;
    size_t headerReceived = 0;
    bool carriageReturn = false;
    bool hasLength = false;
    bool hasTransferEncoding = false;
    bool hasContentEncoding = false;
    bool hasLocation = false;
    bool hasMime = false;
    uint64_t remaining = 0;
    unsigned statusCode = 0;
    uint64_t bodyReceived = 0;
    std::string redirectLocation;
    std::string mime;
};

}
