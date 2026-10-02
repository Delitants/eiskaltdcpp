#include "dcpp/stdinc.h"
#include <memory>
#include <ranges>
#define private public
#include "dcpp/BufferedSocket.h"
#include "dcpp/DownloadManager.h"
#include "dcpp/QueueManager.h"
#include "dcpp/ClientManager.h"
#include "dcpp/ConnectionManager.h"
#include "dcpp/UserConnection.h"
#undef private
#include "dcpp/Download.h"
#include "dcpp/File.h"
#include "dcpp/Streams.h"
#include "dcpp/HashManager.h"
#include "dcpp/Semaphore.h"

// Isolated test access, matching the core shutdown resource-drain fixtures.
void installQuitFixtureTransport(dcpp::BufferedSocket* socket, std::unique_ptr<dcpp::Socket> transport) {
    socket->setSocket(std::move(transport));
}

bool startQuitFixtureDownload(dcpp::DCContext& core, dcpp::Socket& peer,
                              const std::string& part, const std::string& target) {
    using namespace dcpp;
    Socket listener; listener.create(Socket::TYPE_TCP, AF_INET);
    listener.bind("0", "127.0.0.1"); listener.listen();
    peer.create(Socket::TYPE_TCP, AF_INET);
    peer.connect("127.0.0.1", listener.getLocalPort());
    if (!peer.waitConnected(3000)) return false;
    auto* source = core.getConnectionManager()->getConnection(false, false);
    source->setFlag(UserConnection::FLAG_DOWNLOAD | UserConnection::FLAG_SUPPORTS_ADCGET |
                    UserConnection::FLAG_SUPPORTS_TTHF | UserConnection::FLAG_SUPPORTS_MINISLOTS);
    source->setUser(UserPtr(new User(CID::generate()))); source->setHubUrl("adc://fixture");
    source->socket = BufferedSocket::getSocket('\n', core);
    struct Accepted : BufferedSocketListener {
        Semaphore ready;
        void on(Updated) override { ready.signal(); }
    } accepted;
    source->socket->addListener(&accepted);
    source->socket->accept(listener, false, false);
    // The ordered Updated task acknowledges ACCEPTED without GUI event pumping.
    source->socket->updated();
    const bool running = accepted.ready.wait(3000);
    source->socket->removeListener(&accepted);
    if (!running) {
        source->on(BufferedSocketListener::Failed(), "fixture accept timeout");
        return false;
    }
    source->socket->addListener(source);
    const std::string bytes(192 * 1024, '\0'); TigerTree tree(64 * 1024);
    tree.update(bytes.data(), bytes.size()); tree.finalize(); core.getHashManager()->addTree(tree);
    File(part, File::WRITE, File::CREATE | File::TRUNCATE).write(bytes);
    auto* item = core.getQueueManager()->fileQueue.add(target, bytes.size(), 0, QueueItem::NORMAL,
                                                     part, 1700000000, tree.getRoot());
    item->addSegment(Segment(0, 64 * 1024));
    core.getQueueManager()->addSource(item, source->getHintedUser(), 0, false);
    core.getDownloadManager()->addConnection(source);
    auto* download = source->getDownload();
    if (!download) return false;
    core.getDownloadManager()->startData(source, download->getStartPos(), download->getSize(), false);
    core.getDownloadManager()->on(UserConnectionListener::Data(), source,
                                  reinterpret_cast<const uint8_t*>(bytes.data()), 64 * 1024);
    return core.getDownloadManager()->getDownloadCount() == 1;
}
