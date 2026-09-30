#include "UpdateFeedVerifier.h"
#include "UpdatePolicy.h"
#include "UpdateUrlPolicy.h"
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <openssl/evp.h>
#include <limits>
#include <memory>

namespace updates {
namespace {
const QString sparkle = QStringLiteral("http://www.andymatuschak.org/xml-namespaces/sparkle");
const QString schema = QStringLiteral("https://eiskaltdcpp.org/xml-namespaces/updates/1");
constexpr qint64 feedLimit = 1024 * 1024;
constexpr qint64 archiveLimit = 512LL * 1024 * 1024;

QByteArray decode(const QByteArray& encoded, int size) {
    if(encoded.size() != ((size + 2) / 3) * 4) return {};
    const auto raw = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    return raw.size() == size && raw.toBase64() == encoded ? raw : QByteArray{};
}
bool number(const QString& text, quint64 maximum, quint64& value) {
    static const QRegularExpression decimal(QStringLiteral("\\A[1-9][0-9]{0,18}\\z"));
    if(!decimal.match(text).hasMatch()) return false;
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok && value <= maximum;
}
bool authenticate(const QByteArray& bytes, const QByteArray& keyBase64, QByteArray& payload) {
    const QByteArray marker = "<!-- sparkle-signatures:\n";
    const auto offset = bytes.indexOf(marker);
    if(offset <= 0 || bytes.indexOf(marker, offset + marker.size()) >= 0) return false;
    const auto trailer = bytes.mid(offset + marker.size());
    const auto lines = trailer.split('\n');
    // Accept the pinned sign_update format only, not its permissive duplicate
    // key/unsigned suffix variants. Sparkle independently verifies at handoff.
    if((lines.size() != 3 && lines.size() != 4) || lines[2] != "-->" ||
        (lines.size() == 4 && !lines[3].isEmpty()) ||
        !lines[0].startsWith("edSignature: ") || !lines[1].startsWith("length: ")) return false;
    quint64 length = 0;
    if(!number(QString::fromLatin1(lines[1].mid(8)), feedLimit, length) || length != quint64(offset)) return false;
    const auto key = decode(keyBase64, 32);
    const auto signature = decode(lines[0].mid(13), 64);
    if(key.isEmpty() || signature.isEmpty()) return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
            reinterpret_cast<const unsigned char*>(key.constData()), key.size()), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if(!publicKey || !ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, publicKey.get()) != 1 ||
        EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(signature.constData()), signature.size(),
            reinterpret_cast<const unsigned char*>(bytes.constData()), offset) != 1) return false;
    payload = bytes.left(offset);
    return true;
}

struct Element {
    QString name, ns, text;
    QXmlStreamAttributes attributes;
    QVector<Element> children;
};
// A small bounded tree makes schema validation independent of XML prefix
// spelling. Reject DTDs before entity expansion and never install a resolver.
class Reader {
public:
    Reader(const QByteArray& bytes, const std::function<bool()>& stop) : xml(bytes), cancelled(stop) {}
    bool read(Element& root) {
        bool found = false;
        while(!xml.atEnd()) {
            if(!next()) return false;
            if(xml.isStartElement()) {
                if(found || !element(root, 1)) return false;
                found = true;
            } else if(xml.isCharacters() && !xml.isWhitespace()) return false;
        }
        return found && !xml.hasError();
    }
private:
    bool next() {
        if(cancelled && cancelled()) return false;
        xml.readNext();
        return !xml.hasError() && !xml.isDTD() && !xml.isEntityReference() && !xml.isProcessingInstruction();
    }
    bool element(Element& out, int depth) {
        if(depth > 5 || ++nodes > 4096 || xml.attributes().size() > 8) return false;
        out.name = xml.name().toString(); out.ns = xml.namespaceUri().toString();
        out.attributes = xml.attributes();
        while(!xml.atEnd()) {
            if(!next()) return false;
            if(xml.isEndElement()) return true;
            if(xml.isStartElement()) {
                Element child;
                if(!element(child, depth + 1)) return false;
                out.children.append(std::move(child));
            } else if(xml.isCharacters()) {
                if(out.text.size() + xml.text().size() > 16384) return false;
                out.text += xml.text();
            }
        }
        return false;
    }
    QXmlStreamReader xml;
    const std::function<bool()>& cancelled;
    int nodes = 0;
};
bool plain(const Element& e, int maximum, bool empty = false) {
    if(!e.attributes.isEmpty() || !e.children.isEmpty() || e.text.size() > maximum ||
        e.text.toUtf8().size() > maximum || (!empty && e.text.trimmed().isEmpty())) return false;
    for(const auto c : e.text) {
        if(c.category() == QChar::Other_Control && c != '\n' && c != '\r' && c != '\t') return false;
    }
    return true;
}
bool container(const Element& e) { return e.attributes.isEmpty() && e.text.trimmed().isEmpty(); }
bool enclosure(const Element& e, SignedUpdate& r) {
    if(!e.children.isEmpty() || !e.text.trimmed().isEmpty() || e.attributes.size() != 6) return false;
    QSet<QString> attributes;
    for(const auto& a : e.attributes) attributes.insert(a.namespaceUri().toString() + '\n' + a.name().toString());
    const QSet<QString> expected{"\nurl", "\nlength", "\ntype", sparkle + "\nos", sparkle + "\nedSignature", schema + "\nsha256"};
    if(attributes != expected || e.attributes.value("type") != "application/octet-stream" ||
        e.attributes.value(sparkle, "os") != "macos") return false;
    const auto urlText = e.attributes.value("url").toString();
    const auto rawUrl = urlText.toUtf8();
    r.archiveUrl = QUrl::fromEncoded(rawUrl, QUrl::StrictMode);
    if(r.archiveUrl.toEncoded() != rawUrl || !allowedUrl(r.archiveUrl, ResourceKind::Archive) ||
        r.archiveUrl.hasQuery() || !r.archiveUrl.path().endsWith(".zip")) return false;
    quint64 size = 0;
    if(!number(e.attributes.value("length").toString(), archiveLimit, size)) return false;
    r.offer.size = qint64(size);
    r.archiveSignatureBase64 = e.attributes.value(sparkle, "edSignature").toLatin1();
    if(decode(r.archiveSignatureBase64, 64).isEmpty()) return false;
    const auto hash = e.attributes.value(schema, "sha256").toLatin1();
    r.offer.sha256 = QByteArray::fromHex(hash);
    return hash.size() == 64 && r.offer.sha256.size() == 32 && r.offer.sha256.toHex() == hash;
}
bool release(const Element& item, SignedUpdate& r) {
    if(!container(item)) return false;
    QSet<QString> seen;
    for(const auto& e : item.children) {
        const auto id = e.ns + '\n' + e.name;
        if(seen.contains(id)) return false;
        seen.insert(id);
        if(e.ns.isEmpty() && e.name == "enclosure") {
            if(!enclosure(e, r)) return false;
            continue;
        }
        const bool notes = e.ns.isEmpty() && e.name == "description";
        if(!plain(e, notes ? 16384 : 256, notes) || (!notes && e.text != e.text.trimmed())) return false;
        if(e.ns.isEmpty()) {
            if(notes) r.offer.notes = e.text;
            else if(e.name != "title" && e.name != "pubDate") return false;
        } else if(e.ns == sparkle) {
            if(e.name == "version") {
                if(!number(e.text, std::numeric_limits<qint64>::max(), r.offer.build)) return false;
            } else if(e.name == "shortVersionString") {
                static const QRegularExpression version(QStringLiteral("\\A[0-9A-Za-z][0-9A-Za-z.+-]{0,127}\\z"));
                if(!version.match(e.text).hasMatch()) return false;
                r.offer.version = e.text;
            } else if(e.name == "minimumSystemVersion") {
                static const QRegularExpression os(QStringLiteral("\\A[1-9][0-9]{0,3}(?:\\.(?:0|[1-9][0-9]{0,3})){0,2}\\z"));
                if(!os.match(e.text).hasMatch()) return false;
                r.offer.minimumOs = QVersionNumber::fromString(e.text);
            } else if(e.name == "channel") {
                // Sparkle's stable/default channel is represented by omission.
                if(e.text != "preview") return false;
                r.offer.channel = Channel::Preview;
            } else return false;
        } else if(e.ns == schema) {
            if(e.name == "bundleIdentifier") {
                static const QRegularExpression bundle(QStringLiteral("\\A[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)+\\z"));
                if(!bundle.match(e.text).hasMatch()) return false;
                r.offer.bundleId = e.text;
            } else if(e.name == "cpu") {
                if(e.text != "arm64" && e.text != "x86_64") return false;
                r.offer.cpu = e.text;
            } else return false;
        } else return false;
    }
    const QSet<QString> required{"\nenclosure", sparkle + "\nversion", sparkle + "\nshortVersionString",
        sparkle + "\nminimumSystemVersion", schema + "\nbundleIdentifier", schema + "\ncpu"};
    return seen.contains(required);
}
}

std::optional<VerifiedUpdateFeed> verifyUpdateFeed(const QByteArray& bytes, const QByteArray& pinnedPublicKey,
    const std::function<bool()>& cancelled) {
    const auto stopped = [&] { return cancelled && cancelled(); };
    if(stopped() || bytes.isEmpty() || bytes.size() > feedLimit) return {};
    QByteArray payload;
    if(!authenticate(bytes, pinnedPublicKey, payload) || stopped()) return {};
    Element root;
    if(!Reader(payload, cancelled).read(root) || root.name != "rss" || !root.ns.isEmpty() ||
        root.attributes.size() != 1 || root.attributes[0].name() != "version" ||
        !root.attributes[0].namespaceUri().isEmpty() || root.attributes[0].value() != "2.0" ||
        !root.text.trimmed().isEmpty() || root.children.size() != 1) return {};
    const auto& channel = root.children.front();
    if(channel.name != "channel" || !channel.ns.isEmpty() || !container(channel)) return {};
    VerifiedUpdateFeed result{bytes, {}};
    QSet<QString> identities;
    bool title = false;
    for(const auto& element : channel.children) {
        if(stopped() || !element.ns.isEmpty()) return {};
        if(element.name == "title") {
            if(title || !plain(element, 256)) return {};
            title = true;
        } else if(element.name == "item") {
            SignedUpdate update;
            if(result.releases.size() >= 128 || !release(element, update)) return {};
            const auto id = update.offer.bundleId + '\n' + update.offer.cpu + '\n' + QString::number(update.offer.build);
            if(identities.contains(id)) return {};
            identities.insert(id);
            result.releases.append(std::move(update));
        } else return {};
    }
    if(stopped()) return {};
    return result;
}

std::optional<SignedUpdate> selectUpdate(const VerifiedUpdateFeed& feed, const UpdateIdentity& installed, Channel channel) {
    UpdatePreferences preferences;
    preferences.channel = channel;
    std::optional<SignedUpdate> selected;
    for(const auto& release : feed.releases) {
        if(UpdatePolicy::accepts(installed, preferences, release.offer) &&
            (!selected || release.offer.build > selected->offer.build)) selected = release;
    }
    return selected;
}
}
