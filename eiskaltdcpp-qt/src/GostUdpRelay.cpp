#ifdef USE_TORRENT
#include "dcpp/stdinc.h"
#include "GostUdpRelay.h"
#include "dcpp/GostUdpRelay.h"

struct GostUdpRelay::Impl {
    dcpp::GostUdpRelay relay;
    explicit Impl(dcpp::Socket::StreamProxyConfig config) : relay(std::move(config)) { }
};

GostUdpRelay::GostUdpRelay(dcpp::Socket::StreamProxyConfig config)
    : d(std::make_unique<Impl>(std::move(config))) { }
GostUdpRelay::~GostUdpRelay() = default;
quint16 GostUdpRelay::start(const QHostAddress& bind, const QHostAddress& sender,
                           quint16 port, QString* error) {
    if(error) error->clear();
    try {
        return d->relay.start(bind.toString().toStdString(), sender.toString().toStdString(), port);
    } catch(const dcpp::Exception& failure) {
        if(error) *error = QString::fromStdString(failure.getError());
        return 0;
    }
}
void GostUdpRelay::requestStop() { d->relay.requestStop(); }
void GostUdpRelay::join() { d->relay.join(); }
bool GostUdpRelay::isRunning() const { return d->relay.isRunning(); }
#endif
