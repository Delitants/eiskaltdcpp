#include "ProxyTrust.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <openssl/x509v3.h>

namespace dcpp {
ProxyTrustError::ProxyTrustError(Reason reason)
    : std::runtime_error(reason == Unreadable ? "Cannot read the proxy CA certificate file" :
        reason == TooLarge ? "Proxy CA certificate files must not exceed 1 MiB" :
        "Proxy CA file must contain valid PEM CA certificates only"), reason_(reason) { }

namespace {
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool validBundle(const std::string& pem) {
    const std::string begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
    size_t offset = 0;
    bool found = false;
    while(offset < pem.size()) {
        while(offset < pem.size() && whitespace(pem[offset])) ++offset;
        if(offset == pem.size()) break;
        if(pem.compare(offset, begin.size(), begin) != 0) return false;
        offset += begin.size();
        const auto last = pem.find(end, offset);
        if(last == std::string::npos) return false;
        std::string encoded;
        for(; offset < last; ++offset)
            if(!whitespace(pem[offset])) encoded += pem[offset];
        if(encoded.empty() || encoded.size() % 4 != 0) return false;
        std::string der(encoded.size(), '\0');
        int length = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(der.data()),
            reinterpret_cast<const unsigned char*>(encoded.data()), static_cast<int>(encoded.size()));
        if(length < 0) return false;
        if(encoded.back() == '=') --length;
        if(encoded[encoded.size() - 2] == '=') --length;
        if(length <= 0) return false;
        std::string canonical(4 * ((length + 2) / 3) + 1, '\0');
        int size = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(canonical.data()),
            reinterpret_cast<const unsigned char*>(der.data()), length);
        canonical.resize(size);
        if(canonical != encoded) return false;
        const auto* cursor = reinterpret_cast<const unsigned char*>(der.data());
        const auto* start = cursor;
        std::unique_ptr<X509, decltype(&X509_free)> cert(d2i_X509(nullptr, &cursor, length), X509_free);
        if(!cert || cursor != start + length) return false;
        std::unique_ptr<BASIC_CONSTRAINTS, decltype(&BASIC_CONSTRAINTS_free)> constraints(
            static_cast<BASIC_CONSTRAINTS*>(X509_get_ext_d2i(cert.get(), NID_basic_constraints, nullptr, nullptr)),
            BASIC_CONSTRAINTS_free);
        if(!constraints || !constraints->ca) return false;
        found = true;
        offset = last + end.size();
    }
    return found;
}
}

std::string loadProxyCaPem(const std::string& path) {
    if(path.empty()) return {};
    std::error_code error;
    std::filesystem::path native;
    try { native = std::filesystem::u8path(path); }
    catch(const std::filesystem::filesystem_error&) { throw ProxyTrustError(ProxyTrustError::Unreadable); }
    if(path.find('\0') != std::string::npos || !std::filesystem::is_regular_file(native, error) || error)
        throw ProxyTrustError(ProxyTrustError::Unreadable);
    std::ifstream in(native, std::ios::binary);
    if(!in) throw ProxyTrustError(ProxyTrustError::Unreadable);
    constexpr size_t limit = 1024 * 1024;
    std::string pem(limit + 1, '\0');
    in.read(pem.data(), pem.size());
    if(in.bad()) throw ProxyTrustError(ProxyTrustError::Unreadable);
    pem.resize(static_cast<size_t>(in.gcount()));
    if(pem.size() > limit) throw ProxyTrustError(ProxyTrustError::TooLarge);
    if(!validBundle(pem)) throw ProxyTrustError(ProxyTrustError::Invalid);
    return pem;
}
}
