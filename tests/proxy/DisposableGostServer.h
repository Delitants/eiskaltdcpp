#pragma once

#include "dcpp/Socket.h"
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <span>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

namespace proxy_test {
// Ephemeral identities only; private keys never leave memory.
struct Identity {
    std::shared_ptr<EVP_PKEY> key;
    std::shared_ptr<X509> cert;
    std::string pem;

    static Identity make(const Identity* issuer = nullptr,
                         const char* san = "IP:127.0.0.1,DNS:proxy.test",
                         bool expired = false) {
        Identity result;
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> gen(
            EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* key = nullptr;
        if(!gen || EVP_PKEY_keygen_init(gen.get()) != 1 ||
           EVP_PKEY_CTX_set_ec_paramgen_curve_nid(gen.get(), NID_X9_62_prime256v1) != 1 ||
           EVP_PKEY_keygen(gen.get(), &key) != 1)
            throw std::runtime_error("fixture key generation failed");
        result.key.reset(key, EVP_PKEY_free);
        result.cert.reset(X509_new(), X509_free);
        X509* cert = result.cert.get();
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), issuer ? 2 : 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
        X509_gmtime_adj(X509_getm_notAfter(cert), expired ? -60 : 3600);
        X509_set_pubkey(cert, key);
        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(issuer ? "disposable proxy" : "disposable CA"), -1, -1, 0);
        X509_set_issuer_name(cert, issuer ? X509_get_subject_name(issuer->cert.get()) : name);
        X509V3_CTX context{};
        X509V3_set_ctx(&context, issuer ? issuer->cert.get() : cert, cert, nullptr, nullptr, 0);
        auto extension = [&](int nid, const char* value) {
            std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ext(
                X509V3_EXT_conf_nid(nullptr, &context, nid, value), X509_EXTENSION_free);
            if(!ext || X509_add_ext(cert, ext.get(), -1) != 1)
                throw std::runtime_error("fixture extension failed");
        };
        extension(NID_basic_constraints, issuer ? "critical,CA:FALSE" : "critical,CA:TRUE");
        extension(NID_key_usage, issuer ? "critical,digitalSignature" : "critical,keyCertSign,cRLSign");
        if(issuer) {
            extension(NID_ext_key_usage, "serverAuth");
            extension(NID_subject_alt_name, san);
        }
        if(X509_sign(cert, issuer ? issuer->key.get() : key, EVP_sha256()) <= 0)
            throw std::runtime_error("fixture signing failed");
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
        if(PEM_write_bio_X509(bio.get(), cert) != 1)
            throw std::runtime_error("fixture PEM failed");
        char* data = nullptr;
        const auto size = BIO_get_mem_data(bio.get(), &data);
        result.pem.assign(data, size);
        return result;
    }
};

struct GostOptions {
    uint8_t methodVersion = 5, method = 0x82;
    uint8_t authVersion = 1, authStatus = 0;
    int tlsMax = TLS1_3_VERSION;
    bool immediateTls = false;
    bool stallTls = false;
    int methodDelayMs = 0, authDelayMs = 0, replyByteDelayMs = 0;
    int sessionMs = 3000, stallTlsMs = 700, echoDelayMs = 0;
    bool stallData = false, closeAfterReply = false;
    std::vector<uint8_t> initialData;
    std::string halfCloseReply;
    std::function<std::vector<uint8_t>(std::span<const uint8_t>)> transform;
    std::vector<uint8_t> reply{5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
};

class GostServer {
public:
    Identity ca;
    Identity leaf;
    GostOptions options;
    std::array<uint8_t, 3> hello{};
    std::string user, password, target;
    uint16_t targetPort = 0;
    uint8_t command = 0, atyp = 0;
    int bytesAfterSelection = -1;
    bool tlsAccepted = false, authReceived = false, requestReceived = false;
    bool sigpipeBlocked = false;
    size_t applicationBytes = 0;
    std::string error;
    std::vector<uint8_t> echoedInput;
    std::atomic<bool> commandReady{false};
    std::atomic<size_t> receivedDataBytes{0};

    explicit GostServer(GostOptions opts = {}, const char* san = "IP:127.0.0.1,DNS:proxy.test",
                        bool expired = false, const Identity* trust = nullptr)
        : ca(trust ? *trust : Identity::make()), leaf(Identity::make(&ca, san, expired)), options(std::move(opts)) {
        listener.create(dcpp::Socket::TYPE_TCP, AF_INET);
        listener.bind("0", "127.0.0.1");
        listener.listen();
    }
    ~GostServer() { requestStop(); join(); }
    int port() { return std::stoi(listener.getLocalPort()); }
    void start(dcpp::Socket* sharedListener = nullptr) {
        worker = std::jthread([this, sharedListener] { run(sharedListener ? *sharedListener : listener); });
    }
    void requestStop() { stopping = true; }
    void join() { if(worker.joinable()) worker.join(); }

private:
    dcpp::Socket listener;
    std::jthread worker;
    std::atomic<bool> stopping{false};
    void delay(int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while(!stopping && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    void run(dcpp::Socket& acceptFrom) {
        using dcpp::Socket;
        try {
#ifndef _WIN32
            sigset_t blockedSignals;
            sigemptyset(&blockedSignals);
            sigaddset(&blockedSignals, SIGPIPE);
            // This dedicated worker exits immediately after run(). Keep SIGPIPE
            // blocked until thread exit so pending socket-BIO signals are never
            // delivered by restoring a mask. Do not change process disposition.
            if(pthread_sigmask(SIG_BLOCK, &blockedSignals, nullptr) != 0)
                throw std::runtime_error("fixture SIGPIPE blocking failed");
            sigset_t currentMask;
            if(pthread_sigmask(SIG_BLOCK, nullptr, &currentMask) != 0)
                throw std::runtime_error("fixture signal mask query failed");
            sigpipeBlocked = sigismember(&currentMask, SIGPIPE) == 1;
#endif
            if(acceptFrom.wait(1500, Socket::WAIT_READ) != Socket::WAIT_READ) return;
            Socket peer;
            peer.accept(acceptFrom);
            peer.setBlocking(false);
#ifdef SO_NOSIGPIPE
            peer.setSocketOpt(SO_NOSIGPIPE, 1);
#endif
            if(!options.immediateTls) {
                if(peer.readAll(hello.data(), hello.size(), 1500) != 3) return;
                delay(options.methodDelayMs);
                const uint8_t selection[]{options.methodVersion, options.method};
                peer.writeAll(selection, 2, 1500);
                if(options.method != 0x82 || options.methodVersion != 5) {
                    uint8_t extra = 0;
                    bytesAfterSelection = peer.readAll(&extra, 1, 1500);
                    return;
                }
            }
            if(options.stallTls) { delay(options.stallTlsMs); return; }
            std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
            SSL_CTX_set_security_level(ctx.get(), 0);
            SSL_CTX_set_min_proto_version(ctx.get(), TLS1_VERSION);
            SSL_CTX_set_max_proto_version(ctx.get(), options.tlsMax);
            if(SSL_CTX_use_certificate(ctx.get(), leaf.cert.get()) != 1 ||
               SSL_CTX_use_PrivateKey(ctx.get(), leaf.key.get()) != 1)
                throw std::runtime_error("fixture TLS identity failed");
            std::unique_ptr<SSL, decltype(&SSL_free)> tls(SSL_new(ctx.get()), SSL_free);
            SSL_set_fd(tls.get(), peer.sock);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.sessionMs);
            auto retry = [&](int rc) {
                const int err = SSL_get_error(tls.get(), rc);
                if(err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) return false;
                if(stopping || std::chrono::steady_clock::now() >= deadline) return false;
                const int flag = err == SSL_ERROR_WANT_READ ? Socket::WAIT_READ : Socket::WAIT_WRITE;
                return (peer.wait(100, flag) & flag) != 0 || std::chrono::steady_clock::now() < deadline;
            };
            int rc;
            while((rc = SSL_accept(tls.get())) != 1) if(!retry(rc)) return;
            tlsAccepted = true;
            auto read = [&](void* output, size_t size) {
                size_t offset = 0;
                while(offset < size) {
                    int n = SSL_read(tls.get(), static_cast<char*>(output) + offset, int(size - offset));
                    if(n > 0) { offset += n; applicationBytes += n; }
                    else if(!retry(n)) return false;
                }
                return true;
            };
            auto write = [&](const void* input, size_t size) {
                size_t offset = 0;
                while(offset < size) {
                    int n = SSL_write(tls.get(), static_cast<const char*>(input) + offset, int(size - offset));
                    if(n > 0) offset += n;
                    else if(!retry(n)) return false;
                }
                return true;
            };
            if(options.immediateTls) {
                if(!read(hello.data(), hello.size())) return;
                const uint8_t selected[]{5, hello[2]};
                if(!write(selected, sizeof(selected))) return;
            }
            uint8_t auth[2]{};
            if(!read(auth, 2) || auth[0] != 1) return;
            user.resize(auth[1]);
            if(!read(user.data(), user.size())) return;
            uint8_t length = 0;
            if(!read(&length, 1)) return;
            password.resize(length);
            if(!read(password.data(), password.size())) return;
            authReceived = true;
            delay(options.authDelayMs);
            const uint8_t authenticated[]{options.authVersion, options.authStatus};
            if(!write(authenticated, 2)) return;
            uint8_t req[4]{};
            if(!read(req, 4)) return;
            requestReceived = true;
            command = req[1]; atyp = req[3];
            size_t count = atyp == 1 ? 4 : atyp == 4 ? 16 : 0;
            if(atyp == 3) { if(!read(&length, 1)) return; count = length; }
            if(!count) return;
            std::vector<uint8_t> address(count);
            if(!read(address.data(), count)) return;
            if(atyp == 3) target.assign(address.begin(), address.end());
            else {
                char text[INET6_ADDRSTRLEN]{};
                inet_ntop(atyp == 1 ? AF_INET : AF_INET6, address.data(), text, sizeof(text));
                target = text;
            }
            uint8_t portBytes[2]{};
            if(!read(portBytes, 2)) return;
            targetPort = uint16_t(portBytes[0] << 8 | portBytes[1]);
            for(auto byte : options.reply) {
                delay(options.replyByteDelayMs);
                if(!write(&byte, 1)) return;
            }
            commandReady = true;
            if(options.closeAfterReply) return;
            if(!options.initialData.empty() && !write(options.initialData.data(), options.initialData.size())) return;
            if(options.stallData) { delay(options.sessionMs); return; }
            // Echo opaque stream bytes, so clients can exercise both payload kinds.
            std::array<uint8_t, 4096> data{};
            while(!stopping && std::chrono::steady_clock::now() < deadline) {
                const int n = SSL_read(tls.get(), data.data(), data.size());
                if(n > 0) {
                    // Bounded observation, independent of the amount transferred.
                    const auto keep = std::min<size_t>(n, 1024 * 1024 - echoedInput.size());
                    echoedInput.insert(echoedInput.end(), data.begin(), data.begin() + keep);
                    receivedDataBytes += size_t(n);
                    delay(options.echoDelayMs);
                    if(stopping) return;
                    if(options.transform) {
                        const auto response = options.transform({data.data(), size_t(n)});
                        if(!response.empty() && !write(response.data(), response.size())) return;
                    } else if(options.halfCloseReply.empty() && !write(data.data(), n)) return;
                } else if(SSL_get_error(tls.get(), n) == SSL_ERROR_ZERO_RETURN) {
                    if(!options.halfCloseReply.empty()) write(options.halfCloseReply.data(), options.halfCloseReply.size());
                    SSL_shutdown(tls.get());
                    return;
                } else if(!retry(n)) return;
            }
        } catch(const std::exception& e) { error = e.what(); }
        catch(...) { error = "fixture socket failure"; }
    }
};
} // namespace proxy_test
