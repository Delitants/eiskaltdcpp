#pragma once
#include "DisposableGostServer.h"

namespace proxy_test {
class NestedTlsEcho {
    proxy_test::Identity ca = proxy_test::Identity::make();
    proxy_test::Identity identity = proxy_test::Identity::make(&ca);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context{SSL_CTX_new(TLS_server_method()), SSL_CTX_free};
    std::unique_ptr<SSL, decltype(&SSL_free)> session{nullptr, SSL_free};
    std::string response;
    bool sent = false;
    bool closeNotify = false;
public:
    std::string request;
    std::function<std::string(const std::string&)> responder;
    const std::string& roots() const { return ca.pem; }
    explicit NestedTlsEcho(std::string reply = {}, bool closeAfter = false, const proxy_test::Identity* trust = nullptr)
        : ca(trust ? *trust : proxy_test::Identity::make()), identity(proxy_test::Identity::make(&ca)),
          response(std::move(reply)), closeNotify(closeAfter) {
        SSL_CTX_use_certificate(context.get(), identity.cert.get());
        SSL_CTX_use_PrivateKey(context.get(), identity.key.get());
        session.reset(SSL_new(context.get()));
        SSL_set_bio(session.get(), BIO_new(BIO_s_mem()), BIO_new(BIO_s_mem()));
        SSL_set_accept_state(session.get());
    }
    std::vector<uint8_t> exchange(std::span<const uint8_t> input) {
        BIO_write(SSL_get_rbio(session.get()), input.data(), input.size());
        if(!SSL_is_init_finished(session.get())) SSL_do_handshake(session.get());
        if(SSL_is_init_finished(session.get())) {
            char plain[1024];
            int count;
            while((count = SSL_read(session.get(), plain, sizeof(plain))) > 0) {
                if(response.empty() && !responder) SSL_write(session.get(), plain, count);
                else {
                    request.append(plain, count);
                    if(!sent && request.find("\r\n\r\n") != std::string::npos) {
                        if(responder) response = responder(request);
                        SSL_write(session.get(), response.data(), response.size());
                        if(closeNotify) SSL_shutdown(session.get());
                        sent = true;
                    }
                }
            }
        }
        std::vector<uint8_t> result(BIO_ctrl_pending(SSL_get_wbio(session.get())));
        if(!result.empty()) BIO_read(SSL_get_wbio(session.get()), result.data(), result.size());
        return result;
    }
};
}
