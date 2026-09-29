#include "PendingOpenEvents.h"
#include <QApplication>
#include <QFile>
#include <QFileOpenEvent>
#include <QJsonDocument>
#include <QJsonObject>

class Receiver final : public QApplication
{
public:
    Receiver(int &argc, char **argv) : QApplication(argc, argv)
    {
        setQuitOnLastWindowClosed(false);
        if (arguments().size() != 2)
            qFatal("Expected one report path");
        report_.setFileName(arguments().at(1));
        if (!report_.open(QIODevice::WriteOnly | QIODevice::Append))
            qFatal("Cannot open report");
        record("started");
        queue_.setHandler([this](const QString &source) {
            record("delivered", source);
            if (++delivered_ == 2) {
                // Observe late duplicates before self-exiting, without killing any app.
                QTimer::singleShot(700, this, [this] {
                    record("finished");
                    exit(delivered_ == 2 && received_ == 2 ? 0 : 3);
                });
            }
        });
        QTimer::singleShot(15000, this, [this] { record("timeout"); exit(2); });
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() != QEvent::FileOpen)
            return QApplication::event(event);
        auto *open = static_cast<QFileOpenEvent *>(event);
        const auto source = PendingOpenEvents::nativeSource(open->file(), open->url());
        const bool accepted = queue_.enqueue(source);
        record("received", source, accepted, open->file(), open->url().toString(QUrl::FullyEncoded));
        if (++received_ == 1) {
            // Start only after native delivery: cold events always encounter an unready queue.
            QTimer::singleShot(800, this, [this] {
                ready_ = true;
                record("ready");
                queue_.setReady(true);
            });
        }
        event->accept();
        return true;
    }

private:
    void record(const char *kind, const QString &source = {}, bool accepted = true,
                const QString &file = {}, const QString &url = {})
    {
        const QJsonObject row{{"event", kind}, {"source", source}, {"ready", ready_},
                              {"pid", qint64(applicationPid())}, {"accepted", accepted},
                              {"file", file}, {"url", url}};
        const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        if (report_.write(bytes) != bytes.size() || !report_.flush())
            qFatal("Cannot append report");
    }
    QFile report_;
    PendingOpenEvents queue_;
    bool ready_ = false;
    int received_ = 0;
    int delivered_ = 0;
};

int main(int argc, char **argv)
{
    Receiver app(argc, argv);
    return app.exec();
}
