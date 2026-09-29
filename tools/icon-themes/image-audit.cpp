#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

static QImage loadImage(const QString &path, int size, int dpr)
{
    if (size == 0) {
        QImage image(path);
        if (image.isNull()) throw std::runtime_error("Cannot load legacy PNG");
        return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) throw std::runtime_error("Invalid SVG");
    QImage image(size * dpr, size * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return image;
}

static QByteArray visiblePixels(const QImage &image)
{
    QByteArray result;
    result.reserve(image.width() * image.height() * 4);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            result.append(char(qRed(pixel)));
            result.append(char(qGreen(pixel)));
            result.append(char(qBlue(pixel)));
            result.append(char(qAlpha(pixel)));
        }
    }
    return result;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QFile input;
    input.open(stdin, QIODevice::ReadOnly);
    const auto requests = QJsonDocument::fromJson(input.readAll()).array();
    QJsonArray results;
    try {
        for (const auto &request : requests) {
            const auto item = request.toObject();
            const int size = item["size"].toInt();
            const int dpr = item["dpr"].toInt(1);
            const auto first = loadImage(item["first"].toString(), size, dpr);
            const auto second = loadImage(item["second"].toString(), size, dpr);
            if (first.size() != second.size()) throw std::runtime_error("Image sizes differ");
            int alphaChanged = 0;
            double alphaError = 0;
            for (int y = 0; y < first.height(); ++y) {
                for (int x = 0; x < first.width(); ++x) {
                    const int difference = std::abs(qAlpha(first.pixel(x, y)) - qAlpha(second.pixel(x, y)));
                    if (difference > 32) ++alphaChanged;
                    alphaError += difference;
                }
            }
            const auto a = visiblePixels(first);
            const auto b = visiblePixels(second);
            const double pixels = first.width() * first.height();
            results.append(QJsonObject{
                {"id", item["id"]}, {"width", first.width()}, {"height", first.height()},
                {"identicalPixels", a == b}, {"alphaChangedFraction", alphaChanged / pixels},
                {"alphaMeanAbsoluteError", alphaError / (255 * pixels)},
                {"firstPixelSha256", QString::fromLatin1(QCryptographicHash::hash(a, QCryptographicHash::Sha256).toHex())},
                {"secondPixelSha256", QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex())}
            });
        }
    } catch (const std::exception &error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
    QTextStream(stdout) << QJsonDocument(results).toJson(QJsonDocument::Compact) << '\n';
}
