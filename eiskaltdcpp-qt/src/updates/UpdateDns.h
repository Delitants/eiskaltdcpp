#pragma once

#include "UpdateTransport.h"
#include <QByteArray>

namespace dcpp { class WakeNotifier; }
namespace updates::dns_detail {
enum class Provider { Cloudflare, Google };
struct Answer {
    bool retryable = false;
    std::vector<std::string> addresses;
};
QByteArray query(const std::string& host, uint16_t type);
Answer parse(const QByteArray& body, const std::string& host, uint16_t type);
dcpp::HttpRequestPolicy policy(Provider, const QByteArray& query,
    std::shared_ptr<const dcpp::ProxyRouteSnapshot>, uint32_t timeout,
    const std::function<bool()>& cancelled, std::shared_ptr<dcpp::WakeNotifier> = {});
transport_detail::Resolver resolver(std::shared_ptr<const dcpp::ProxyRouteSnapshot>,
    transport_detail::Backend, std::shared_ptr<dcpp::WakeNotifier> = {});
}
