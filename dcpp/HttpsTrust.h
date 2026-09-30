#pragma once
#include <openssl/ssl.h>
#include <cstdint>
#include <vector>

namespace dcpp {
// Only for the updater's independent, newly created HTTPS context. Does not
// consult DC certificates, proxy roots or relaxed hub verification preferences.
bool loadDefaultHttpsRoots(SSL_CTX* context);
namespace https_trust_detail {
using Anchors = std::vector<std::vector<uint8_t>>;
// Atomic, bounded replacement with DER CA certificates, no file/network I/O.
bool installAnchors(SSL_CTX* context, const Anchors& anchors);
}
}
