#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPixmap>
#include <cstdio>

#include "MainWindow.h"
#include "Telemetry.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("gt_api_test"));

    const QStringList args = app.arguments();
    const bool selftest = args.contains(QStringLiteral("--selftest"));

    QString renderPath;
    if (args.contains(QStringLiteral("--render"))) {
        const int i = args.indexOf(QStringLiteral("--render"));
        if (i + 1 < args.size()) renderPath = args[i + 1];
        else renderPath = QStringLiteral("gt_api_test_snapshot.png");
    }
    int wpx = 1600, hpx = 900;
    if (args.contains(QStringLiteral("--size"))) {
        const int i = args.indexOf(QStringLiteral("--size"));
        if (i + 1 < args.size())
            sscanf(args[i + 1].toLatin1().constData(), "%dx%d", &wpx, &hpx);
    }

    MainWindow win;
    QElapsedTimer et; et.start();
    const int n = 730 * 1440;                       // ~2 years @ 1 min
    const double endSec = QDateTime::currentSecsSinceEpoch();
    TelemetryData d = generateTelemetry(endSec - n * 60.0, n, 60.0, 0xC0FFEEULL);
    const qint64 genMs = et.elapsed();
    win.load(d);

    if (selftest || !renderPath.isEmpty()) {
        QPixmap pm = win.renderSnapshot(wpx, hpx);
        if (pm.save(renderPath, "PNG"))
            std::printf("snapshot saved: %s (%dx%d) gen_points=%d gen_ms=%lld stats=%s\n",
                        renderPath.toLocal8Bit().constData(), wpx, hpx,
                        d.count(), (long long)genMs,
                        win.statsText().toLocal8Bit().constData());
        else {
            std::fprintf(stderr, "snapshot save FAILED: %s\n", renderPath.toLocal8Bit().constData());
            return 2;
        }
        if (selftest) return 0;
    }

    win.resize(1500, 950);
    win.show();
    return app.exec();
}
