#pragma once

#include <QIcon>
#include <QCache>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QTemporaryDir>

class FileExtensionIcons {
public:
    QIcon iconForFile(const QString &remoteName, const QIcon &fallback) {
        const QString name = remoteName.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
        const QString extension = QFileInfo(name).suffix().toLower();
        static const QRegularExpression safeExtension(QStringLiteral("\\A[a-z0-9]{1,16}\\z"));
        if (!safeExtension.match(extension).hasMatch())
            return fallback;
        if (const QIcon *cached = cache.object(extension))
            return cached->isNull() ? fallback : *cached;

        QIcon resolved;
        QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/eiskalt-file-icon-XXXXXX"));
        if (temporary.isValid()) {
            QFile file(temporary.filePath(QStringLiteral("type.") + extension));
            if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                file.close();
                // Only this synthetic empty file is inspected, never the remote path.
                const QIcon native = provider.icon(QFileInfo(file));
                const QImage generic = provider.icon(QFileIconProvider::File).pixmap(16, 16).toImage();
                if (!native.isNull() && native.pixmap(16, 16).toImage() != generic) {
                    // Materialize before removing the file: native icon engines can be lazy.
                    for (int size : {16, 24, 32, 48, 64})
                        resolved.addPixmap(native.pixmap(size, size));
                }
            }
        }
        cache.insert(extension, new QIcon(resolved));
        return resolved.isNull() ? fallback : resolved;
    }

private:
    QCache<QString, QIcon> cache{128};
    QFileIconProvider provider;
};
