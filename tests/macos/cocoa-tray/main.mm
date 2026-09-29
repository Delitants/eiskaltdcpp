#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <mach-o/dyld.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QPixmap>
#include <cstdio>

namespace {
NSEvent *injectedEvent = nil;
NSEvent *currentEvent(id, SEL) { return injectedEvent; }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "Usage: cocoa_tray_regression /absolute/Qt/plugin/root\n");
        return 2;
    }
    const QString pluginRoot = QString::fromLocal8Bit(argv[1]);
    const QString expectedPlugin = QFileInfo(pluginRoot + "/platforms/libqcocoa.dylib").canonicalFilePath();
    if (expectedPlugin.isEmpty())
        return 2;
    qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
    qputenv("QT_QPA_PLATFORM", "cocoa");
    QCoreApplication::setLibraryPaths({pluginRoot});
    QApplication app(argc, argv);
    int matchingPlugins = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const QFileInfo loaded(QString::fromUtf8(_dyld_get_image_name(i)));
        if (loaded.fileName() == "libqcocoa.dylib") {
            if (loaded.canonicalFilePath() != expectedPlugin) {
                std::fprintf(stderr, "FAIL unexpected Cocoa plugin: %s\n", qPrintable(loaded.filePath()));
                return 2;
            }
            ++matchingPlugins;
        }
    }
    QFile plugin(expectedPlugin);
    if (matchingPlugins != 1 || !plugin.open(QIODevice::ReadOnly))
        return 2;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&plugin))
        return 2;
    std::printf("Plugin: %s\nSHA256: %s\n", qPrintable(expectedPlugin), hash.result().toHex().constData());
    QMenu menu;
    menu.addAction(QStringLiteral("Regression fixture only"));
    QSystemTrayIcon tray;
    QPixmap icon(16, 16);
    icon.fill(Qt::blue);
    tray.setIcon(QIcon(icon));
    tray.setContextMenu(&menu);
    tray.show();
    app.processEvents();
    NSMenu *nativeMenu = menu.toNSMenu();
    if (!nativeMenu || !QSystemTrayIcon::isSystemTrayAvailable())
        return 2;

    int activations = 0;
    QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown;
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app,
        [&](QSystemTrayIcon::ActivationReason value) { ++activations; reason = value; });

    // Limit event injection to the synchronous notification used by Qt's
    // real Cocoa tray delegate; no application profile or network is involved.
    Method method = class_getInstanceMethod([NSApplication class], @selector(currentEvent));
    IMP original = method_setImplementation(method, reinterpret_cast<IMP>(currentEvent));
    int failures = 0;
    auto check = [&](const char *name, NSEvent *event, QSystemTrayIcon::ActivationReason expected) {
        injectedEvent = event;
        activations = 0;
        @try {
            [[NSNotificationCenter defaultCenter]
                postNotificationName:NSMenuDidBeginTrackingNotification object:nativeMenu];
            if (activations != 1 || reason != expected) {
                std::fprintf(stderr, "FAIL %s: activations=%d reason=%d expected=%d\n",
                             name, activations, int(reason), int(expected));
                ++failures;
            } else {
                std::printf("PASS %s\n", name);
            }
        } @catch (NSException *exception) {
            std::fprintf(stderr, "FAIL %s: %s: %s\n", name,
                         exception.name.UTF8String, exception.reason.UTF8String);
            ++failures;
        }
    };
    auto other = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    auto mouse = [](NSEventType type, NSInteger count) {
        return [NSEvent mouseEventWithType:type location:NSZeroPoint modifierFlags:0 timestamp:0
            windowNumber:0 context:nil eventNumber:0 clickCount:count pressure:1];
    };
    check("non-mouse menu activation", other, QSystemTrayIcon::Unknown);
    check("missing current event", nil, QSystemTrayIcon::Unknown);
    check("left click", mouse(NSEventTypeLeftMouseDown, 1), QSystemTrayIcon::Trigger);
    check("double click", mouse(NSEventTypeLeftMouseDown, 2), QSystemTrayIcon::DoubleClick);
    check("right click", mouse(NSEventTypeRightMouseDown, 1), QSystemTrayIcon::Context);
    method_setImplementation(method, original);
    injectedEvent = nil;
    tray.hide();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
