#include "HttpResponseParser.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace dcpp {
namespace {
bool token(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

std::string lower(std::string_view value) {
    std::string result(value);
    for(char& c : result) if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}

std::string_view trim(std::string_view value) {
    while(!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}

uint64_t number(std::string_view value, int base) {
    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if(value.empty() || parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Invalid HTTP body length");
    return result;
}
}

HttpResponseParser::HttpResponseParser(Limits value, bool unavailable)
    : limits(value), allowUnavailable(unavailable) {}

[[noreturn]] void HttpResponseParser::fail(const char* message) {
    state = State::Failed;
    throw std::runtime_error(message);
}

void HttpResponseParser::parseHeader(bool trailer) {
    const auto colon = line.find(':');
    if(colon == std::string::npos || colon == 0 ||
        !std::all_of(line.begin(), line.begin() + colon, token)) fail("Invalid HTTP header name");
    const auto name = lower(std::string_view(line).substr(0, colon));
    const auto value = trim(std::string_view(line).substr(colon + 1));
    if(trailer) {
        if(name == "content-length" || name == "transfer-encoding" || name == "content-encoding" ||
            name == "location" || name == "host" || name == "connection" || name == "trailer")
            fail("Forbidden HTTP trailer");
        return;
    }
    if(name == "content-length") {
        if(hasLength || hasTransferEncoding) fail("Ambiguous HTTP body framing");
        hasLength = true;
        remaining = number(value, 10);
    } else if(name == "transfer-encoding") {
        if(hasLength || hasTransferEncoding || lower(value) != "chunked") fail("Unsupported HTTP transfer encoding");
        hasTransferEncoding = true;
    } else if(name == "content-encoding") {
        if(hasContentEncoding || lower(value) != "identity") fail("Unsupported HTTP content encoding");
        hasContentEncoding = true;
    } else if(name == "content-type") {
        if(hasMime) fail("Duplicate HTTP content type");
        hasMime = true;
        mime = lower(value);
    } else if(name == "location") {
        if(hasLocation || value.empty()) fail("Invalid HTTP redirect location");
        hasLocation = true;
        redirectLocation = std::string(value);
    }
}

void HttpResponseParser::finishLine() {
    if(state == State::Status) {
        if(line.size() < 13 || (line.compare(0, 9, "HTTP/1.0 ") != 0 &&
            line.compare(0, 9, "HTTP/1.1 ") != 0) || line[12] != ' ' ||
            !std::all_of(line.begin() + 9, line.begin() + 12, [](char c) { return c >= '0' && c <= '9'; }))
            fail("Invalid HTTP status line");
        statusCode = static_cast<unsigned>(number(std::string_view(line).substr(9, 3), 10));
        if(statusCode != 200 && statusCode != 301 && statusCode != 302 && statusCode != 303 &&
            statusCode != 307 && statusCode != 308 && !unavailable()) fail("Unexpected HTTP response status");
        state = State::Headers;
    } else if(state == State::Headers) {
        if(!line.empty()) {
            parseHeader(false);
        } else if(statusCode != 200 && !unavailable()) {
            if(!hasLocation) fail("Missing HTTP redirect location");
            state = State::Redirect;
        } else if(hasLength) {
            if(remaining > limits.bodyBytes) fail("HTTP body exceeds limit");
            state = remaining ? State::FixedBody : State::Complete;
        } else {
            state = hasTransferEncoding ? State::ChunkSize : State::CloseBody;
        }
    } else if(state == State::ChunkSize) {
        const auto semicolon = line.find(';');
        remaining = number(std::string_view(line).substr(0, semicolon), 16);
        if(remaining > limits.bodyBytes - bodyReceived) fail("HTTP body exceeds limit");
        state = remaining ? State::ChunkBody : State::Trailers;
    } else if(state == State::Trailers) {
        if(line.empty()) state = State::Complete;
        else parseHeader(true);
    }
    line.clear();
}

void HttpResponseParser::feed(const uint8_t* data, size_t size, const BodySink& sink) {
    if(state == State::Failed) fail("HTTP parser already failed");
    try {
        while(size) {
            if(state == State::Redirect) return;
            if(state == State::Complete) fail("Unexpected bytes after HTTP response");
            if(state == State::FixedBody || state == State::CloseBody || state == State::ChunkBody) {
                const size_t count = state == State::CloseBody ? size :
                    static_cast<size_t>(std::min<uint64_t>(size, remaining));
                if(count > limits.bodyBytes - bodyReceived) fail("HTTP body exceeds limit");
                sink(data, count);
                bodyReceived += count;
                data += count;
                size -= count;
                if(state != State::CloseBody) {
                    remaining -= count;
                    if(!remaining) state = state == State::FixedBody ? State::Complete : State::ChunkCR;
                }
                continue;
            }
            const unsigned char c = *data++;
            --size;
            if(state == State::ChunkCR || state == State::ChunkLF) {
                if(c != (state == State::ChunkCR ? '\r' : '\n')) fail("Invalid HTTP chunk terminator");
                state = state == State::ChunkCR ? State::ChunkLF : State::ChunkSize;
                continue;
            }
            if(state != State::ChunkSize) {
                if(headerReceived == limits.headerBytes) fail("HTTP headers exceed limit");
                ++headerReceived;
            } else if(line.size() >= 1024) {
                fail("HTTP chunk metadata exceeds limit");
            }
            if(carriageReturn) {
                if(c != '\n') fail("Invalid HTTP line ending");
                carriageReturn = false;
                finishLine();
            } else if(c == '\r') {
                carriageReturn = true;
            } else {
                if((c < 32 && c != '\t') || c == 127) fail("Invalid HTTP header character");
                line.push_back(static_cast<char>(c));
            }
        }
    } catch(...) {
        state = State::Failed;
        throw;
    }
}

void HttpResponseParser::eof() {
    if(state == State::CloseBody) state = State::Complete;
    if(!complete()) fail("Truncated HTTP response");
}

bool HttpResponseParser::complete() const { return state == State::Complete || state == State::Redirect; }
bool HttpResponseParser::redirect() const { return state == State::Redirect; }
bool HttpResponseParser::unavailable() const {
    return allowUnavailable && (statusCode == 500 || statusCode == 502 || statusCode == 503 || statusCode == 504);
}
}
