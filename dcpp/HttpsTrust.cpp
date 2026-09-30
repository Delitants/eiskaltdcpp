#include "HttpsTrust.h"
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <memory>
#ifdef __APPLE__
#include <Security/Security.h>
#endif

namespace dcpp {
namespace https_trust_detail {
bool installAnchors(SSL_CTX* context, const Anchors& anchors) {
    if(!context || anchors.empty() || anchors.size() > 4096) return false;
    size_t total = 0;
    for(const auto& bytes : anchors) {
        if(bytes.empty() || bytes.size() > 64 * 1024) return false;
        total += bytes.size();
        if(total > 16 * 1024 * 1024) return false;
    }
    std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> store(X509_STORE_new(), X509_STORE_free);
    if(!store) return false;
    for(const auto& bytes : anchors) {
        const unsigned char* cursor = bytes.data();
        std::unique_ptr<X509, decltype(&X509_free)> certificate(
            d2i_X509(nullptr, &cursor, static_cast<long>(bytes.size())), X509_free);
        if(!certificate || cursor != bytes.data() + bytes.size() || X509_check_ca(certificate.get()) <= 0 ||
            X509_STORE_add_cert(store.get(), certificate.get()) != 1) {
            ERR_clear_error();
            return false;
        }
    }
    // Replace only after validating the whole set; never mix it with DC/proxy
    // trust, SSL_CERT_FILE/SSL_CERT_DIR, or a previous context's root store.
    SSL_CTX_set_cert_store(context, store.release());
    return true;
}
}

bool loadDefaultHttpsRoots(SSL_CTX* context) {
    if(!context) return false;
#ifdef __APPLE__
    // Copy the OS's public default anchors locally. Do not invoke SecTrust
    // evaluation: its issuer/OCSP fetching could escape the selected proxy.
    CFArrayRef certificates = nullptr;
    const auto status = SecTrustCopyAnchorCertificates(&certificates);
    std::unique_ptr<const void, decltype(&CFRelease)> releaseCertificates(certificates, CFRelease);
    if(status != errSecSuccess || !certificates || CFGetTypeID(certificates) != CFArrayGetTypeID()) return false;
    const auto count = CFArrayGetCount(certificates);
    if(count <= 0 || count > 4096) return false;
    https_trust_detail::Anchors anchors;
    anchors.reserve(static_cast<size_t>(count));
    size_t total = 0;
    for(CFIndex i = 0; i < count; ++i) {
        const auto value = CFArrayGetValueAtIndex(certificates, i);
        if(!value || CFGetTypeID(value) != SecCertificateGetTypeID()) return false;
        const auto certificate = static_cast<SecCertificateRef>(const_cast<void*>(value));
        const auto data = SecCertificateCopyData(certificate);
        std::unique_ptr<const void, decltype(&CFRelease)> releaseData(data, CFRelease);
        if(!data) return false;
        const auto size = CFDataGetLength(data);
        if(size <= 0 || size > 64 * 1024) return false;
        total += static_cast<size_t>(size);
        if(total > 16 * 1024 * 1024) return false;
        anchors.emplace_back(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + size);
    }
    return https_trust_detail::installAnchors(context, anchors);
#else
    return SSL_CTX_set_default_verify_paths(context) == 1;
#endif
}
}
