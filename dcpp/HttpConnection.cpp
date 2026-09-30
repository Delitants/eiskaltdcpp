/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2013 EiskaltDC++ developers
 * Copyright (C) 2019 Boris Pek <tehnick-8@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include "HttpConnection.h"
#include "SSLSocket.h"

#include "BufferedSocket.h"
#include "format.h"
#include "SettingsManager.h"
#include "TimerManager.h"
#include "version.h"
#include "ProxyResolver.h"
#include <array>
#include <chrono>
#include <stdexcept>

namespace dcpp {

HttpConnection::HttpConnection(DCContext& ctx, const string& aUserAgent, Connector aConnector) :
    userAgent(aUserAgent),
    port("80"),
    size(-1),
    done(0),
    speed(0),
    lastPos(0),
    lastTick(0),
    connState(CONN_UNKNOWN),
    connType(TYPE_POST),
    socket(0),
    ctx_(ctx),
    usingHttpProxy(false),
    connector(aConnector)
{
}

HttpConnection::~HttpConnection() {
    stopForShutdown();
}

HttpConnection::HttpConnection(DCContext& context, const HttpRequestPolicy& policy, const string& agent)
    : HttpConnection(context, agent) {
    strictPolicy = std::make_unique<const HttpRequestPolicy>(policy);
    strictWake = std::make_shared<SocketWake>();
}

void HttpConnection::downloadStrict(const string& requestedUrl) {
    using Clock = std::chrono::steady_clock;
    const auto& policy = *strictPolicy;
    const auto deadline = Clock::now() + std::chrono::milliseconds(policy.totalMs);
    const char* stage = "policy";
    const auto cancelled = [&] {
        return strictCancelled.load() || (policy.cancelled && policy.cancelled()) || !policy.route || !policy.route->revoked ||
            policy.route->revoked->load();
    };
    const auto check = [&] {
        if(cancelled()) throw std::runtime_error("HTTPS request cancelled");
        if(Clock::now() >= deadline) throw std::runtime_error("HTTPS request deadline exceeded");
    };
    const auto left = [&](Clock::time_point phaseDeadline) {
        check();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::min(deadline, phaseDeadline) - Clock::now()).count();
        if(remaining <= 0) throw std::runtime_error("HTTPS request timed out");
        return static_cast<uint32_t>(remaining);
    };
    try {
        check();
        if(!policy.prepare || !policy.route->valid || !policy.route->notifier ||
            policy.redirects > 5 || policy.limits.headerBytes > 65536 ||
            policy.limits.bodyBytes > 512ULL * 1024 * 1024 ||
            !policy.connectMs || policy.connectMs > 15000 || !policy.idleMs || policy.idleMs > 30000 ||
            !policy.totalMs || policy.totalMs > 30 * 60 * 1000)
            throw std::runtime_error("Invalid HTTPS policy");
        const auto& route = *policy.route;
        const auto type = route.proxy.type;
        if(!((route.mode == SettingsManager::OUTGOING_DIRECT && type == Socket::StreamProxyConfig::None) ||
             (route.mode == SettingsManager::OUTGOING_SOCKS5 && type == Socket::StreamProxyConfig::Socks5) ||
             (route.mode == SettingsManager::OUTGOING_SHADOWSOCKS && type == Socket::StreamProxyConfig::Shadowsocks) ||
             (route.mode == SettingsManager::OUTGOING_GOST && type == Socket::StreamProxyConfig::Gost)))
            throw std::runtime_error("Invalid HTTPS route");
        const auto subscription = route.notifier->subscribe(strictWake);
        const auto cancellationSubscription = policy.cancellationNotifier ?
            policy.cancellationNotifier->subscribe(strictWake) : std::shared_ptr<void>();
        const string agent = userAgent.empty() ? dcpp::fullHTTPVersionString : userAgent;
        if(agent.find_first_of("\r\n") != string::npos || agent.find('\0') != string::npos)
            throw std::runtime_error("Invalid HTTPS user agent");
        string location = requestedUrl, previous;
        for(unsigned redirects = 0;; ++redirects) {
            check();
            stage = "destination validation and DNS";
            const auto connectDeadline = Clock::now() + std::chrono::milliseconds(policy.connectMs);
            const auto target = policy.prepare(location, previous, route, left(connectDeadline), cancelled);
            check();
            in_addr v4{};
            in6_addr v6{};
            if(target.url.compare(0, 8, "https://") != 0 || target.port != "443" ||
                target.path.empty() || target.path.front() != '/' ||
                std::any_of(target.path.begin(), target.path.end(), [](unsigned char c) { return c <= 32 || c >= 127; }) ||
                (inet_pton(AF_INET, target.address.c_str(), &v4) != 1 &&
                 inet_pton(AF_INET6, target.address.c_str(), &v6) != 1))
                throw std::runtime_error("Invalid pinned HTTPS destination");
            url = target.url;
            done = 0;
            size = -1;
            stage = "TLS configuration";
            auto stream = SSLSocket::createVerifiedClient(target.host, policy.rootsPem);
            stream->setWaitWake(strictWake, cancelled);
            stage = "connection and TLS verification";
            if(type == Socket::StreamProxyConfig::None) {
                // No DCContext: mutable global settings cannot override this snapshot.
                stream->connect(target.address, target.port);
            } else {
                auto config = route.proxy;
                if(config.tls && !config.verifyTls) throw std::runtime_error("Unverified proxy TLS refused");
                const auto originalCancellation = config.cancelled;
                config.cancelled = [cancelled, originalCancellation] {
                    return cancelled() || (originalCancellation && originalCancellation());
                };
                config.cancellationNotifier.reset();
                if(config.connectHost.empty())
                    config.connectHost = resolveProxyEndpoint(config.host, left(connectDeadline), config.cancelled).front();
                stream->proxyConnect(target.address, target.port, config, left(connectDeadline));
            }
            if(!stream->waitConnected(left(connectDeadline))) throw std::runtime_error("HTTPS connection timed out");
            check();
            stage = "request write";
            const auto request = "GET " + target.path + " HTTP/1.1\r\nHost: " + target.host +
                "\r\nUser-Agent: " + agent + "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
            stream->writeAll(request.data(), static_cast<int>(request.size()), left(connectDeadline));
            stage = "response read";
            HttpResponseParser parser(policy.limits);
            std::array<uint8_t, 16384> buffer{};
            auto idleDeadline = Clock::now() + std::chrono::milliseconds(policy.idleMs);
            while(!parser.complete()) {
                const auto timeout = left(idleDeadline);
                const auto count = stream->read(buffer.data(), buffer.size());
                if(count < 0) {
                    stream->wait(timeout, Socket::WAIT_READ);
                    continue;
                }
                if(!count) { parser.eof(); break; }
                idleDeadline = Clock::now() + std::chrono::milliseconds(policy.idleMs);
                parser.feed(buffer.data(), static_cast<size_t>(count), [&](const uint8_t* data, size_t count) {
                    check();
                    done += static_cast<int64_t>(count);
                    fire(HttpConnectionListener::Data(), this, data, count);
                    check();
                });
            }
            stream->disconnect();
            check();
            if(!parser.redirect()) {
                fire(HttpConnectionListener::Complete(), this, url);
                return;
            }
            stage = "redirect validation";
            if(redirects >= policy.redirects) throw std::runtime_error("HTTPS redirect limit exceeded");
            previous = target.url;
            location = parser.location();
        }
    } catch(...) {
        // Never surface raw network/proxy errors, URLs, credentials, or signed queries.
        const string error = cancelled() ? "HTTPS request cancelled or proxy route changed" :
            string("HTTPS request failed during ") + stage;
        fire(HttpConnectionListener::Failed(), this, error);
    }
}

bool HttpConnection::shouldUseOutgoingProxy(bool usingHttpProxy, int outgoingMode) {
    return !usingHttpProxy && outgoingMode != SettingsManager::OUTGOING_DIRECT;
}

bool HttpConnection::responseBodyWouldExceedSize(int64_t declaredSize, int64_t bytesDone, size_t nextLen) {
    if(declaredSize < 0) {
        return false;
    }

    if(bytesDone < 0 || bytesDone > declaredSize) {
        return true;
    }

    return static_cast<uint64_t>(nextLen) > static_cast<uint64_t>(declaredSize - bytesDone);
}

/**
 * Downloads a file and returns it as a string
 * @todo Report exceptions
 * @todo Abort download
 * @param aUrl Full URL of file
 * @return A string with the content, or empty if download failed
 */
void HttpConnection::downloadFile(const string& aFile) {
    if(shutdownStopped.load()) return;
    if(strictPolicy) { downloadStrict(aFile); return; }
    url = aFile;
    prepareRequest(TYPE_GET);
}

/**
 * Initiates a basic urlencoded form submission
 * @param aFile Fully qualified file URL
 * @param aData StringMap with the args and values
 */
void HttpConnection::download(const string& aUrl, const StringMap& postData) {
    if(shutdownStopped.load()) return;
    if(strictPolicy) {
        fire(HttpConnectionListener::Failed(), this, string("Strict HTTPS permits only GET"));
        return;
    }
    url = aUrl;
    requestBody.clear();

    for(auto& i : postData)
        requestBody += "&" + Util::encodeURI(i.first) + "=" + Util::encodeURI(i.second);

    if (!requestBody.empty()) requestBody = requestBody.substr(1);
    prepareRequest(TYPE_POST);
}

HttpConnection::CallbackGuard::CallbackGuard(HttpConnection& value) : owner(value) {
    std::lock_guard lock(owner.callbackMutex);
    admitted = !owner.shutdownStopped.load();
    if(admitted) ++owner.activeCallbacks;
}

HttpConnection::CallbackGuard::~CallbackGuard() {
    if(!admitted) return;
    std::lock_guard lock(owner.callbackMutex);
    if(--owner.activeCallbacks == 0) owner.callbacksDone.notify_all();
}

void HttpConnection::stopForShutdown() {
    shutdownStopped.store(true);
    if(strictPolicy) {
        abort();
        return;
    }
    BufferedSocket* retired;
    {
        std::unique_lock lock(callbackMutex);
        callbacksDone.wait(lock, [this] { return activeCallbacks == 0; });
        retired = socket;
        socket = nullptr;
    }
    // Never hold the callback accounting lock across a socket listener barrier.
    // Late callbacks are rejected; admitted callbacks have all finished.
    BufferedSocket::putSocket(retired);
}

void HttpConnection::abort() {
    if(strictPolicy) {
        strictCancelled.store(true);
        strictWake->signal();
        return;
    }
    if(socket) {
        abortRequest(true);
    }
}

void HttpConnection::prepareRequest(RequestType type) {
    dcassert(Util::findSubString(url, "http://") == 0 || Util::findSubString(url, "https://") == 0);
    Util::sanitizeUrl(url);

    std::unique_ptr<BufferedSocket, void (*)(BufferedSocket*)> retiredSocket(
        socket, &BufferedSocket::putSocket);
    if(retiredSocket) {
        retiredSocket->removeListener(this);
        retiredSocket->disconnect(true);
        socket = nullptr;
    }

    // Reset the connection states
    if(connState == CONN_OK || connState == CONN_FAILED)
        userAgent.clear();

    size = -1;
    done = 0;
    speed = 0;
    lastPos = 0;
    lastTick = GET_TICK();

    connState = CONN_UNKNOWN;
    connType = type;

    // method selection
    method = (connType == TYPE_POST) ? "POST" : "GET";

    // set download type
    if(Util::stricmp(url.substr(url.size() - 4), ".bz2") == 0) {
        mimeType = "application/x-bzip2";
        fire(HttpConnectionListener::TypeBZ2(), this);
    } else {
        mimeType.clear();
        fire(HttpConnectionListener::TypeNormal(), this);
    }

    string proto, query, fragment;
    usingHttpProxy = !CTX_SETTING(HTTP_PROXY).empty() &&
        (publicHubListProxy || CTX_SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_DIRECT);

    if(!usingHttpProxy) {
        Util::decodeUrl(url, proto, server, port, file, query, fragment);
        if(file.empty())
            file = "/";
    } else {
        Util::decodeUrl(CTX_SETTING(HTTP_PROXY), proto, server, port, file, query, fragment);
        file = url;
    }

    if(!query.empty())
        file += '?' + query;

    if(port.empty())
        port = "80";

    if(userAgent.empty())
        userAgent = dcpp::fullHTTPVersionString;

    if(!socket)
        socket = BufferedSocket::getSocket(0x0a, ctx());


    socket->addListener(this);
    try {
        if(proto == "https") {
            SSLSocket::setSNIHint(server);
        }
        const bool useOutgoingProxy = shouldUseOutgoingProxy(
            usingHttpProxy, CTX_SETTING(OUTGOING_CONNECTIONS));
        if(connector) {
            connector(*socket, server, port, proto == "https", useOutgoingProxy);
        } else {
            socket->connect(server, port, (proto == "https"), true, useOutgoingProxy, Socket::PROTO_DEFAULT,
                Util::emptyString, publicHubListProxy && usingHttpProxy);
        }
        // keep SNI hint alive until the async TLS connect actually uses it

    } catch(const Exception& e) {
        connState = CONN_FAILED;
        fire(HttpConnectionListener::Failed(), this, str(dcpp::dcpp_fmt("%1% (%2%)") % e.getError() % url));
    }
}

void HttpConnection::abortRequest(bool disconnect) {
    dcassert(socket);

    socket->removeListener(this);
    if(disconnect) socket->disconnect();

    BufferedSocket::putSocket(socket);
    socket = NULL;
}

void HttpConnection::updateSpeed() {
    if(done > lastPos) {
        auto tick = GET_TICK();

        auto tickDelta = static_cast<double>(tick - lastTick);
        if(tickDelta > 0) {
            speed = static_cast<double>(done - lastPos) / tickDelta * 1000.0;
        }

        lastPos = done;
        lastTick = tick;
    }
}

void HttpConnection::on(BufferedSocketListener::Connected) {
    CallbackGuard callback(*this);
    if(!callback.admitted) return;
    dcassert(socket);
    socket->write(method + " " + file + " HTTP/1.1\r\n");

    string sRemoteServer = server;
    if(usingHttpProxy)
    {
        string tfile, tport, proto, query, fragment;
        Util::decodeUrl(file, proto, sRemoteServer, tport, tfile, query, fragment);
    }

#ifdef WITH_DHT
    if (sRemoteServer == "strongdc.sourceforge.net")
        socket->write("User-Agent: StrongDC++ v2.42\r\n");
    else
        socket->write("User-Agent: " + userAgent + "\r\n");
#else
    socket->write("User-Agent: " + userAgent + "\r\n");
#endif

    socket->write("Host: " + sRemoteServer + "\r\n");
    socket->write("Cache-Control: no-cache\r\n");
    if(connType == TYPE_POST)
    {
        socket->write("Content-Type: application/x-www-form-urlencoded\r\n");
        socket->write("Content-Length: " + Util::toString(requestBody.size()) + "\r\n");
    }
    socket->write("Connection: close\r\n\r\n");    // we'll only be doing one request

    if (connType == TYPE_POST)
        socket->write(requestBody);
}

void HttpConnection::on(BufferedSocketListener::Line, const string& aLine) {
    CallbackGuard callback(*this);
    if(!callback.admitted) return;
    if(connState == CONN_CHUNKED && aLine.size() > 1) {
        string::size_type i;
        string chunkSizeStr;
        if((i = aLine.find(";")) == string::npos) {
            chunkSizeStr = aLine.substr(0, aLine.length() - 1);
        } else chunkSizeStr = aLine.substr(0, i);

        unsigned long chunkSize = strtoul(chunkSizeStr.c_str(), NULL, 16);
        if(chunkSize == 0 || chunkSize == ULONG_MAX) {
            abortRequest(true);

            if(chunkSize == 0) {
                connState = CONN_OK;
                fire(HttpConnectionListener::Complete(), this, url);
            } else {
                connState = CONN_FAILED;
                fire(HttpConnectionListener::Failed(), this, str(F_("Transfer-encoding error (%1%)") % url));
            }

        } else socket->setDataMode(chunkSize);
    } else if(connState == CONN_UNKNOWN) {
        statusLine = Util::trimCopy(aLine);
        if(aLine.find("200") != string::npos) {
            connState = CONN_OK;
        } else if(aLine.find("301") != string::npos || aLine.find("302") != string::npos) {
            connState = CONN_MOVED;
        } else {
            abortRequest(true);
            connState = CONN_FAILED;
            fire(HttpConnectionListener::Failed(), this, str(dcpp::dcpp_fmt("%1% (%2%)") % statusLine % url));
        }
    } else if(connState == CONN_MOVED && Util::findSubString(aLine, "Location") != string::npos) {
        abortRequest(true);

        string location = aLine.substr(10, aLine.length() - 10);
        Util::sanitizeUrl(location);

        // make sure we can also handle redirects with relative paths
        if(location.find("://") == string::npos) {
            if(location[0] == '/') {
                string proto, query, fragment;
                Util::decodeUrl(url, proto, server, port, file, query, fragment);
                string tmp = proto + "://" + server;
                if(port != "80" || port != "443")
                    tmp += ':' + port;
                location = tmp + location;
            } else {
                auto i = url.rfind('/');
                dcassert(i != string::npos);
                location = url.substr(0, i + 1) + location;
            }
        }

        if(location == url) {
            connState = CONN_FAILED;
            fire(HttpConnectionListener::Failed(), this, str(F_("Endless redirection loop (%1%)") % url));
            return;
        }

        fire(HttpConnectionListener::Redirected(), this, location);
        downloadFile(location);
    } else if(aLine[0] == 0x0d) {
        if(size != -1) {
            socket->setDataMode(size);
        } else connState = CONN_CHUNKED;
    } else if(Util::findSubString(aLine, "Content-Length") != string::npos) {
        size = Util::toInt(aLine.substr(16, aLine.length() - 17));
    } else if(Util::findSubString(aLine, "Content-Encoding") != string::npos) {
        if(aLine.substr(18, aLine.length() - 19) == "x-bzip2")
            fire(HttpConnectionListener::TypeBZ2(), this);
    } else if(mimeType.empty()) {
        if(Util::findSubString(aLine, "Content-Encoding") != string::npos) {
            if(aLine.substr(18, aLine.length() - 19) == "x-bzip2")
                mimeType = "application/x-bzip2";
        } else if(Util::findSubString(aLine, "Content-Type") != string::npos) {
            mimeType = aLine.substr(14, aLine.length() - 15);
        }
    }
}

void HttpConnection::on(BufferedSocketListener::Failed, const string& aLine) {
    CallbackGuard callback(*this);
    if(!callback.admitted) return;
    abortRequest(false);
    connState = CONN_FAILED;
    statusLine = Util::trimCopy(aLine);
    fire(HttpConnectionListener::Failed(), this, str(dcpp::dcpp_fmt("%1% (%2%)") % statusLine % url));
}

void HttpConnection::on(BufferedSocketListener::ModeChange) {
    CallbackGuard callback(*this);
    if(!callback.admitted) return;
    if(connState != CONN_CHUNKED) {
        abortRequest(true);

        fire(HttpConnectionListener::Complete(), this, url);
    }
}
void HttpConnection::on(BufferedSocketListener::Data, uint8_t* aBuf, size_t aLen) {
    CallbackGuard callback(*this);
    if(!callback.admitted) return;
    if(responseBodyWouldExceedSize(size, done, aLen)) {
        abortRequest(true);

        connState = CONN_FAILED;
        fire(HttpConnectionListener::Failed(), this, str(F_("Too much data in response body (%1%)") % url));
        return;
    }

    done += aLen;
    updateSpeed();
    fire(HttpConnectionListener::Data(), this, aBuf, aLen);
}

} // namespace dcpp
