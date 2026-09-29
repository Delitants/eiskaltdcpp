/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2019 EiskaltDC++ developers
 * Copyright (C) 2019 Boris Pek <tehnick-8@yandex.ru>
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
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

#include "BufferedSocket.h"
#include "SocketWake.h"

#include <algorithm>
#include <limits>

#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "SettingsManager.h"
#include "SSLSocket.h"
#include "Streams.h"
#include "ThrottleManager.h"
#include "TimerManager.h"
#include "ZUtils.h"
#include "DCPlusPlus.h"

namespace dcpp {

using std::min;
using std::max;

BufferedSocket::BufferedSocket(char aSeparator, DCContext& ctx) :
    separator(aSeparator), state(STARTING),
    ctx_(ctx), disconnecting(false), taskWake(std::make_shared<SocketWake>())
{
    start();

    sockets.inc();
}

Atomic<long,memory_ordering_strong> BufferedSocket::sockets(0);

BufferedSocket::~BufferedSocket() {
    sockets.dec();
}

bool BufferedSocket::dataModeCanConsumeMore(int64_t bytesLeftInBlock, int bufferedBytesLeft) {
    return bufferedBytesLeft > 0 && (bytesLeftInBlock == -1 || bytesLeftInBlock > 0);
}

void BufferedSocket::setMode (Modes aMode, size_t aRollback) {
    input.setMode(aMode, aRollback);
}

void BufferedSocket::setSocket(std::unique_ptr<Socket> s) {
    dcassert(!sock.get());
    s->setWaitWake(taskWake, [this] { return disconnecting.load(); });
    s->setContext(&ctx());
    if(ctx().getSettingsManager()->get(SettingsManager::SOCKET_IN_BUFFER) > 0)
        s->setSocketOpt(SO_RCVBUF, ctx().getSettingsManager()->get(SettingsManager::SOCKET_IN_BUFFER));
    if(ctx().getSettingsManager()->get(SettingsManager::SOCKET_OUT_BUFFER) > 0)
        s->setSocketOpt(SO_SNDBUF, ctx().getSettingsManager()->get(SettingsManager::SOCKET_OUT_BUFFER));
    s->setSocketOpt(SO_REUSEADDR, 1);   // NAT traversal

    inbuf.resize(s->getSocketOptInt(SO_RCVBUF));

    sock = std::move(s);
}

void BufferedSocket::accept(const Socket& srv, bool secure, bool allowUntrusted) {
    dcdebug("BufferedSocket::accept() %p\n", (void*)this);

    std::unique_ptr<Socket> s(secure ? ctx().getCryptoManager()->getServerSocket(allowUntrusted) : new Socket);

    s->accept(srv);

    setSocket(std::move(s));

    Lock l(cs);
    addTask(ACCEPTED, 0);
}

void BufferedSocket::connect(const string& aAddress, const string& aPort, bool secure, bool allowUntrusted, bool proxy, Socket::Protocol proto, const string& expKP, bool publicHttpProxy) {
    connect(aAddress, aPort, Util::emptyString, NAT_NONE, secure, allowUntrusted, proxy, proto, expKP, publicHttpProxy);
}

void BufferedSocket::connect(const string& aAddress, const string& aPort, const string& localPort, NatRoles natRole, bool secure, bool allowUntrusted, bool proxy, Socket::Protocol proto, const string& expKP, bool publicHttpProxy) {
    (void)expKP;
    const bool gost = ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_GOST && !publicHttpProxy;
    if(gost && natRole != NAT_NONE)
        throw SocketException("GOST does not support inbound NAT traversal");
    dcdebug("BufferedSocket::connect() %p\n", (void*)this);
    std::unique_ptr<Socket> s(secure ? (natRole == NAT_SERVER ? ctx().getCryptoManager()->getServerSocket(allowUntrusted) : ctx().getCryptoManager()->getClientSocket(allowUntrusted, proto)) : new Socket);

    const bool useIPv6 = ctx().getSettingsManager()->getBool(SettingsManager::USE_IPV6);
    s->create(Socket::TYPE_TCP, useIPv6 ? AF_INET6 : AF_INET);
    setSocket(std::move(s));
    sock->setGlobalProxyExempt(publicHttpProxy);
    const string bindIp = ctx().getSettingsManager()->get(SettingsManager::BIND_IFACE)
        ? (useIPv6 ? sock->getIfaceI6(ctx().getSettingsManager()->get(SettingsManager::BIND_IFACE_NAME))
                   : sock->getIfaceI4(ctx().getSettingsManager()->get(SettingsManager::BIND_IFACE_NAME)))
        : (useIPv6 ? ctx().getSettingsManager()->get(SettingsManager::BIND_ADDRESS6)
                   : ctx().getSettingsManager()->get(SettingsManager::BIND_ADDRESS));
    sock->bind(localPort, bindIp);

    Lock l(cs);
    const int outgoing = ctx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS);
    addTask(CONNECT, new ConnectInfo(aAddress, aPort, localPort, natRole, gost || (proxy && outgoing != SettingsManager::OUTGOING_DIRECT)));
}

#define LONG_TIMEOUT 30000
#define SHORT_TIMEOUT 1000
void BufferedSocket::threadConnect(const string& aAddr, const string &aPort, const string &localPort, NatRoles natRole, bool proxy) {
    dcassert(state == STARTING);

    dcdebug("threadConnect %s:%s/%s\n", aAddr.c_str(), localPort.c_str(), aPort.c_str());
    fire(BufferedSocketListener::Connecting());

    const uint64_t endTime = GET_TICK() + LONG_TIMEOUT;
    state = RUNNING;

    while (GET_TICK() < endTime) {
        dcdebug("threadConnect attempt to addr \"%s\"\n", aAddr.c_str());
        try {
            if(auto* sslSock = dynamic_cast<SSLSocket*>(sock.get())) {
                sslSock->setServerName(aAddr);
                SSLSocket::setSNIHint(aAddr);
            }

            if(proxy) {
                sock->proxyConnect(aAddr, aPort, LONG_TIMEOUT);
            } else if(auto* sslSock = dynamic_cast<SSLSocket*>(sock.get())) {
                sslSock->connect(aAddr, aPort);
            } else {
                sock->connect(aAddr, aPort);
            }

            const auto now = GET_TICK();
            const bool connSucceeded = sock->waitConnected(static_cast<uint32_t>(now < endTime ? endTime - now : 0));

            if (connSucceeded) {
                fire(BufferedSocketListener::Connected());
                return;
            }
        }
        catch (const SSLSocketException&) {
            throw;
        } catch (const SocketException&) {
            if (natRole == NAT_NONE)
                throw;
            if(disconnecting) return;
            if(taskWake->wait(SHORT_TIMEOUT)) taskWake->consume();
            if(disconnecting) return;
        }
    }

    throw SocketException(_("Connection timeout"));
}

void BufferedSocket::threadAccept() {
    dcassert(state == STARTING);

    dcdebug("threadAccept\n");

    state = RUNNING;

    if(!sock->waitAccepted(LONG_TIMEOUT)) throw SocketException(_("Connection timeout"));
}

void BufferedSocket::threadRead() {
    if(state != RUNNING)
        return;

    const int received = getMode() == MODE_DATA
        ? ctx().getThrottleManager()->read(sock.get(), inbuf.data(), static_cast<int>(inbuf.size()))
        : sock->read(inbuf.data(), static_cast<int>(inbuf.size()));
    if(received == -1)
        return;
    if(received == 0)
        throw SocketException(_("Connection closed"));

    const int configuredLimit = ctx().getSettingsManager()->get(SettingsManager::MAX_COMMAND_LENGTH);
    input.feed(inbuf.data(), static_cast<size_t>(received), separator,
        configuredLimit > 0 ? static_cast<size_t>(configuredLimit) : 0,
        {
            [this](const string& command) { fire(BufferedSocketListener::Line(), command); },
            [this](uint8_t* bytes, size_t size) { fire(BufferedSocketListener::Data(), bytes, size); },
            [this] { fire(BufferedSocketListener::ModeChange()); },
            [this] { return !inputStopped.load(std::memory_order_relaxed) && !disconnecting && state == RUNNING; }
        });
}

void BufferedSocket::threadSendFile(InputStream* file) {
    if(state != RUNNING)
        return;

    if(disconnecting)
        return;
    dcassert(file != NULL);
    size_t sockSize = (size_t)sock->getSocketOptInt(SO_SNDBUF);
    size_t bufSize = max(sockSize, (size_t)64*1024);

    ByteVector readBuf(bufSize);
    ByteVector writeBuf(bufSize);

    size_t readPos = 0;

    bool readDone = false;
    dcdebug("Starting threadSend\n");
    while(!disconnecting) {
        if(!readDone && readBuf.size() > readPos) {
            // Fill read buffer
            size_t bytesRead = readBuf.size() - readPos;
            size_t actual = file->read(&readBuf[readPos], bytesRead);

            if(bytesRead > 0) {
                fire(BufferedSocketListener::BytesSent(), bytesRead, 0);
            }

            if(actual == 0) {
                readDone = true;
            } else {
                readPos += actual;
            }
        }

        if(readDone && readPos == 0) {
            if(!threadFlushProxyOutput()) return;
            fire(BufferedSocketListener::TransmitDone());
            return;
        }

        readBuf.swap(writeBuf);
        readBuf.resize(bufSize);
        writeBuf.resize(readPos);
        readPos = 0;

        size_t writePos = 0, writeSize = 0;
        int written = 0;

        while(writePos < writeBuf.size()) {
            if(disconnecting)
                return;

            int w = sock->wait(0, Socket::WAIT_READ);
            if(w & Socket::WAIT_READ) {
                threadRead();
            }

            if(written == -1) {
                // workaround for OpenSSL (crashes when previous write failed and now retrying with different writeSize)
                try {
                    written = sock->write(&writeBuf[writePos], writeSize);
                } catch(const Exception&) {
                    // ...
                }
            } else {
                writeSize = min(sockSize / 2, writeBuf.size() - writePos);
                written = ctx().getThrottleManager()->write(sock.get(), &writeBuf[writePos], writeSize);
            }

            if(written > 0) {
                writePos += written;

                fire(BufferedSocketListener::BytesSent(), 0, written);

            } else if(written == -1) {
                if(!readDone && readPos < readBuf.size()) {
                    // Read a little since we're blocking anyway...
                    size_t bytesRead = min(readBuf.size() - readPos, readBuf.size() / 2);
                    size_t actual = file->read(&readBuf[readPos], bytesRead);

                    if(bytesRead > 0) {
                        fire(BufferedSocketListener::BytesSent(), bytesRead, 0);
                    }

                    if(actual == 0) {
                        readDone = true;
                    } else {
                        readPos += actual;
                    }
                } else {
                    while(!disconnecting) {
                        int w = sock->wait(Socket::WAIT_FOREVER, Socket::WAIT_WRITE | Socket::WAIT_READ);
                        if(w & Socket::WAIT_READ) {
                            threadRead();
                        }
                        if(w & Socket::WAIT_WRITE) {
                            break;
                        }
                    }
                }
            }
        }
    }
}

void BufferedSocket::write(const char* aBuf, size_t aLen) {
    if(aLen == 0 || !sock.get())
        return;
    Lock l(cs);
    const size_t previousSize = writeBuf.size();
    if(aLen > writeBuf.max_size() - previousSize)
        throw std::length_error("Socket output buffer capacity exceeded");
    writeBuf.insert(writeBuf.end(), aBuf, aBuf+aLen);
    if(previousSize == 0) {
        try {
            addTask(SEND_DATA, 0);
        } catch(...) {
            writeBuf.resize(previousSize);
            throw;
        }
    }
}

int BufferedSocket::writeChunkSize(size_t remaining) {
    return static_cast<int>(std::min(remaining, size_t(std::numeric_limits<int>::max())));
}

void BufferedSocket::threadSendData() {
    if(state != RUNNING)
        return;

    {
        Lock l(cs);
        if(writeBuf.empty())
            return;

        writeBuf.swap(sendBuf);
    }

    size_t left = sendBuf.size();
    size_t done = 0;
    while(left > 0) {
        if(disconnecting) {
            return;
        }

        int w = sock->wait(Socket::WAIT_FOREVER, Socket::WAIT_READ | Socket::WAIT_WRITE);

        if(w & Socket::WAIT_READ) {
            threadRead();
        }

        if(w & Socket::WAIT_WRITE) {
            int n = sock->write(&sendBuf[done], writeChunkSize(left));
            if(n > 0) {
                left -= n;
                done += n;
            }
        }
    }
    if(!threadFlushProxyOutput()) return;
    sendBuf.clear();
}

bool BufferedSocket::threadFlushProxyOutput() {
    // Plaintext acceptance can leave a final encrypted record queued locally.
    // Drain it before completing the task or processing a graceful disconnect.
    while(!disconnecting && state == RUNNING) {
        if(sock->flushProxyOutput()) return true;
        const int ready = sock->wait(Socket::WAIT_FOREVER, Socket::WAIT_READ | Socket::WAIT_WRITE);
        if(ready & Socket::WAIT_READ) threadRead();
    }
    return false;
}

bool BufferedSocket::checkEvents() {
    while(state == RUNNING ? taskSem.wait(0) : taskSem.wait()) {
        pair<Tasks, unique_ptr<TaskData> > p;
        {
            Lock l(cs);
            dcassert(!tasks.empty());
            p = std::move(tasks.front());
            tasks.pop_front();
        }

        if(p.first == SHUTDOWN) {
            return false;
        } else if(p.first == UPDATED) {
            fire(BufferedSocketListener::Updated());
            continue;
        }

        if(state == STARTING) {
            if(p.first == CONNECT) {
                ConnectInfo* ci = static_cast<ConnectInfo*>(p.second.get());
                threadConnect(ci->addr, ci->port, ci->localPort, ci->natRole, ci->proxy);
            } else if(p.first == ACCEPTED) {
                threadAccept();
            } else {
                dcdebug("%d unexpected in STARTING state\n", p.first);
            }
        } else if(state == RUNNING) {
            if(p.first == SEND_DATA) {
                threadSendData();
            } else if(p.first == SEND_FILE) {
                threadSendFile(static_cast<SendFileInfo*>(p.second.get())->stream); break;
            } else if(p.first == DISCONNECT) {
                fail(_("Disconnected"));
            } else {
                dcdebug("%d unexpected in RUNNING state\n", p.first);
            }
        }
    }
    return true;
}

void BufferedSocket::checkSocket() {
    {
        Lock lock(cs);
        // A file send may have consumed a signal while later tasks remain queued.
        if(!tasks.empty()) return;
    }
    int waitFor = sock->wait(Socket::WAIT_FOREVER, Socket::WAIT_READ | Socket::WAIT_WAKE);

    if(waitFor & Socket::WAIT_READ) {
        threadRead();
    }
}

/**
 * Main task dispatcher for the buffered socket abstraction.
 */
int BufferedSocket::run() {
    dcdebug("BufferedSocket::run() start %p\n", (void*)this);
    while(true) {
        try {
            if(!checkEvents()) {
                break;
            }
            if(state == RUNNING) {
                checkSocket();
            }
        } catch(const Exception& e) {
            fail(e.getError());
        } catch(const std::exception& e) {
            // Catch std::bad_alloc and other std::exception subclasses that
            // are not derived from dcpp::Exception.  Without this, an
            // uncaught exception would call std::terminate() and crash the
            // entire process.
            dcdebug("BufferedSocket::run() std::exception: %s\n", e.what());
            fail(string("std::exception in socket thread: ") + e.what());
        }
    }
    dcdebug("BufferedSocket::run() end %p\n", (void*)this);
    detach();  // prevent jthread destructor from self-joining
    delete this;
    return 0;
}

void BufferedSocket::fail(const string& aError) {
    if(sock.get()) {
        sock->disconnect();
    }

    if(state == RUNNING) {
        state = FAILED;
        fire(BufferedSocketListener::Failed(), aError);
    }
}

void BufferedSocket::shutdown() {
    Lock l(cs);
    disconnecting = true;
    addTask(SHUTDOWN, 0);
}

void BufferedSocket::addTask(Tasks task, TaskData* data) {
    dcassert(task == DISCONNECT || task == SHUTDOWN || sock.get());
    unique_ptr<TaskData> owner(data);
    if(task == UPDATED && !tasks.empty() && tasks.back().first == UPDATED)
        return;
    tasks.emplace_back(task, std::move(owner));
    taskSem.signal();
    taskWake->signal();
}

} // namespace dcpp
