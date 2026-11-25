#include "MainWindow.h"

#include <qcustomplot.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <QDialog>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QSettings>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Client-side logging: append-only log file with a big banner on each launch.
// ---------------------------------------------------------------------------
static QString logFilePath()
{
    // Log next to the application executable.
    return QCoreApplication::applicationDirPath() + "/gt_client.log";
}

static void appendToLog(const QString &text)
{
    QFile f(logFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << text << "\n";
    f.flush();
}

static QString timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
}

static void writeLogBanner()
{
    QString banner;
    banner += QStringLiteral("\n");
    banner += QStringLiteral("==============================================================================\n");
    banner += QStringLiteral("  GAMMA TREND CLIENT — NEW LAUNCH\n");
    banner += QStringLiteral("  %1\n").arg(timestamp());
    banner += QStringLiteral("==============================================================================\n");
    banner += QStringLiteral("\n");
    appendToLog(banner);
}

static void logDebug(const QString &message)
{
    appendToLog(QStringLiteral("[%1] DEBUG: %2").arg(timestamp(), message));
}

static void logError(const QString &message)
{
    appendToLog(QStringLiteral("[%1] ERROR: %2").arg(timestamp(), message));
}

static QString fmtSecs(double s)
{
    return QDateTime::fromSecsSinceEpoch(qint64(s))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// centered moving average via prefix sums; winPts<=1 passes through
static QVector<double> movingAvg(const QVector<double> &v, int winPts)
{
    const int n = v.size();
    if (winPts <= 1 || n == 0) return v;
    QVector<double> prefix(n + 1, 0.0);
    for (int i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + v[i];
    const int h = winPts / 2;
    QVector<double> out(n);
    for (int i = 0; i < n; ++i) {
        const int lo = qMax(0, i - h);
        const int hi = qMin(n, i + h + 1);
        out[i] = (prefix[hi] - prefix[lo]) / double(hi - lo);
    }
    return out;
}

// bin means over uniform bins of binSec starting at startSec
static void binMean(const QVector<double> &key, const QVector<double> &v,
                    double startSec, double endSec, double binSec,
                    QVector<double> &outKey, QVector<double> &outVal)
{
    const qint64 nBins = qint64((endSec - startSec) / binSec) + 1;
    outKey.clear(); outVal.clear();
    outKey.reserve(int(nBins)); outVal.reserve(int(nBins));
    const double interval = key.size() > 1 ? key[1] - key[0] : 1.0;
    double sum = 0.0; int cnt = 0; double binStart = startSec;
    for (int i = 0; i < key.size(); ++i) {
        while (key[i] >= binStart + binSec && cnt > 0) {
            outKey.append(binStart + binSec * 0.5);
            outVal.append(sum / cnt);
            sum = 0.0; cnt = 0; binStart += binSec;
        }
        sum += v[i]; ++cnt;
        Q_UNUSED(interval);
    }
    if (cnt > 0) { outKey.append(binStart + binSec * 0.5); outVal.append(sum / cnt); }
}

// bin sums over uniform bins of binSec for chart (isotope) data.
// Handles gaps (empty bins skipped) and overlaps (multiple points at same time all summed).
static void binChartData(const QVector<double> &key, const QVector<double> &val,
                         double binSec,
                         QVector<double> &outKey, QVector<double> &outVal)
{
    outKey.clear(); outVal.clear();
    if (key.isEmpty()) return;

    double binStart = key.first();
    double binSum = 0.0;
    int binCount = 0;

    for (int i = 0; i < key.size(); ++i) {
        while (key[i] >= binStart + binSec && binCount > 0) {
            outKey.append(binStart + binSec * 0.5);
            outVal.append(binSum);
            binSum = 0.0; binCount = 0; binStart += binSec;
        }
        binSum += val[i]; ++binCount;
    }
    if (binCount > 0) { outKey.append(binStart + binSec * 0.5); outVal.append(binSum); }
}

// ---------------------------------------------------------------------------
// HTTP GET helper. Uses cpp-httplib; auto-selects SSLClient for https:// URLs.
// ---------------------------------------------------------------------------
std::string MainWindow::httpGet(const std::string &path, int timeoutSec)
{
    // Always request JSON output mode explicitly (server requires ?mode=).
    std::string fullPath = path;
    // Strip leading slash if present — m_baseUrl already includes the path prefix (e.g. /api)
    if (!fullPath.empty() && fullPath[0] == '/') {
        fullPath.erase(0, 1);
    }
    // Build the full request path with mode=json query parameter
    if (fullPath.find('?') == std::string::npos) {
        fullPath += "?mode=json";
    } else {
        fullPath += "&mode=json";
    }

    // Construct the base URL (scheme + host + port, no path) from m_baseUrl
    // m_baseUrl may include a path prefix (e.g. https://rms.h3dgamma.com/api).
    // Preserve the path prefix so requests go to the correct endpoint (e.g. /api/charts).
    std::string baseUrl = m_baseUrl;
    size_t pathStart = baseUrl.find('/', 8); // find first '/' after scheme://
    std::string pathPrefix;
    if (pathStart != std::string::npos) {
        pathPrefix = m_baseUrl.substr(pathStart);
        baseUrl = baseUrl.substr(0, pathStart);
    }

    // Full URL for logging only
    std::string fullUrl = m_baseUrl;
    if (!fullUrl.empty() && fullUrl.back() != '/') {
        fullUrl += "/";
    }
    fullUrl += fullPath;
    // Prepend the path prefix (e.g. /api) so the request reaches the correct endpoint
    std::string requestPath = pathPrefix + "/" + fullPath;
    logDebug(QStringLiteral("httpGet: %1").arg(QString::fromStdString(fullUrl)));

    httplib::Result res;
    {
        httplib::Client cli(baseUrl);
        cli.set_connection_timeout(timeoutSec);
        cli.set_read_timeout(timeoutSec);
        res = cli.Get(requestPath.c_str());
    }

    if (!res) {
        auto err = res.error();
        logError(QStringLiteral("httpGet: request failed (error=%1 (%2)) for %3")
            .arg(httplib::to_string(err))
            .arg(static_cast<int>(err))
            .arg(QString::fromStdString(fullUrl)));
        logError(QStringLiteral("httpGet: baseUrl=%1 pathPrefix=%2 requestPath=%3")
            .arg(QString::fromStdString(baseUrl))
            .arg(QString::fromStdString(pathPrefix))
            .arg(QString::fromStdString(requestPath)));
        return "";
    }

    if (res->status != 200) {
        std::string bodyPreview = res->body;
        if (bodyPreview.size() > 200) bodyPreview.resize(200);
        logError(QStringLiteral("httpGet: HTTP %1 for %2 body=%3")
            .arg(res->status)
            .arg(QString::fromStdString(fullUrl))
            .arg(QString::fromStdString(bodyPreview)));
        return "";
    }

    return res->body;
}

// -------
// HTTP PUT helper. Sends license text as request body.
// -------
std::string MainWindow::httpPut(const std::string &path, const std::string &body, int timeoutSec)
{
    std::string fullPath = path;
    if (!fullPath.empty() && fullPath[0] == '/') {
        fullPath.erase(0, 1);
    }
    if (fullPath.find('?') == std::string::npos) {
        fullPath += "?mode=html";
    } else {
        fullPath += "&mode=html";
    }

    std::string baseUrl = m_baseUrl;
    size_t pathStart = baseUrl.find('/', 8);
    std::string pathPrefix;
    if (pathStart != std::string::npos) {
        pathPrefix = m_baseUrl.substr(pathStart);
        baseUrl = baseUrl.substr(0, pathStart);
    }

    std::string fullUrl = m_baseUrl;
    if (!fullUrl.empty() && fullUrl.back() != '/') {
        fullUrl += "/";
    }
    fullUrl += fullPath;
    logDebug(QStringLiteral("httpPut: %1").arg(QString::fromStdString(fullUrl)));

    httplib::Result res;
    {
        httplib::Client cli(baseUrl);
        cli.set_connection_timeout(timeoutSec);
        cli.set_read_timeout(timeoutSec);
        // Prepend the path prefix (e.g. /api) so the request reaches the correct endpoint
        std::string requestPath = pathPrefix + "/" + fullPath;
        res = cli.Put(requestPath.c_str(), body, "text/plain");
    }

    if (!res) {
        logError(QStringLiteral("httpPut: request failed (%1) for %2")
            .arg(httplib::to_string(res.error()))
            .arg(QString::fromStdString(fullUrl)));
        return "";
    }

    if (res->status != 200) {
        logError(QStringLiteral("httpPut: HTTP %1 for %2")
            .arg(res->status)
            .arg(QString::fromStdString(fullUrl)));
        return "";
    }

    return res->body;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    // Load persisted server URL from QSettings (falls back to default).
    QSettings settings("H3D", "GammaTrendClient");
    m_baseUrl = settings.value("serverUrl", QStringLiteral("https://rms.h3dgamma.com/api")).toString().toStdString();

    buildUi();
    writeLogBanner();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Gamma Trend Client"));

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *rootLay = new QVBoxLayout(central);

    // ---- control bar -----------------------------------------------------
    QGridLayout *ctrl = new QGridLayout;
    QPushButton *genBtn   = new QPushButton(QStringLiteral("Generate 2y @ 1min"));
    QPushButton *btn24h   = new QPushButton(QStringLiteral("Last 24h"));
    QPushButton *btn7d    = new QPushButton(QStringLiteral("Last 7d"));
    QPushButton *btn30d   = new QPushButton(QStringLiteral("Last 30d"));
    QPushButton *btnAll   = new QPushButton(QStringLiteral("Full range"));
    QPushButton *expBtn   = new QPushButton(QStringLiteral("Export PNG"));
    ctrl->addWidget(genBtn, 0, 0); ctrl->addWidget(btn24h, 0, 1);
    ctrl->addWidget(btn7d, 0, 2);  ctrl->addWidget(btn30d, 0, 3);
    ctrl->addWidget(btnAll, 0, 4); ctrl->addWidget(expBtn, 0, 5);
    m_testBtn = new QPushButton(QStringLiteral("Test Connection"));
    ctrl->addWidget(m_testBtn, 0, 6);

    m_showDetectorsBtn = new QPushButton(QStringLiteral("Show Detectors"));
    ctrl->addWidget(m_showDetectorsBtn, 0, 7);

    m_showLocationsBtn = new QPushButton(QStringLiteral("Show Locations"));
    ctrl->addWidget(m_showLocationsBtn, 0, 8);

    m_showChartsBtn = new QPushButton(QStringLiteral("Show Charts"));
    ctrl->addWidget(m_showChartsBtn, 0, 9);

    m_loadChartDataBtn = new QPushButton(QStringLiteral("Load Chart Data"));
    ctrl->addWidget(m_loadChartDataBtn, 0, 10);

    m_createChartBtn = new QPushButton(QStringLiteral("Create Chart"));
    ctrl->addWidget(m_createChartBtn, 0, 11);

    m_deleteChartBtn = new QPushButton(QStringLiteral("Delete Chart"));
    ctrl->addWidget(m_deleteChartBtn, 0, 12);

    m_showLicensesBtn = new QPushButton(QStringLiteral("Show Licenses"));
    ctrl->addWidget(m_showLicensesBtn, 0, 13);

    m_createLicenseBtn = new QPushButton(QStringLiteral("Create License"));
    ctrl->addWidget(m_createLicenseBtn, 0, 14);

    m_deleteLicenseBtn = new QPushButton(QStringLiteral("Delete License"));
    ctrl->addWidget(m_deleteLicenseBtn, 0, 15);

    ctrl->addWidget(new QLabel(QStringLiteral("Interval bin:")), 1, 0);
    m_binCombo = new QComboBox;
    m_binCombo->addItems({QStringLiteral("1 min (raw)"), QStringLiteral("5 min"),
                          QStringLiteral("15 min"), QStringLiteral("1 hour"),
                          QStringLiteral("6 hours"), QStringLiteral("1 day")});
    m_binCombo->setCurrentIndex(1);
    ctrl->addWidget(m_binCombo, 1, 1);

    m_smoothLabel = new QLabel(QStringLiteral("Smoothing: off"));
    ctrl->addWidget(m_smoothLabel, 2, 0, 1, 2);
    m_smoothSlider = new QSlider(Qt::Horizontal);
    m_smoothSlider->setRange(0, 240);
    m_smoothSlider->setSingleStep(5);
    ctrl->addWidget(m_smoothSlider, 2, 2, 1, 4);

    m_urlEdit = new QLineEdit(QString::fromStdString(m_baseUrl));
    m_urlEdit->setPlaceholderText(QStringLiteral("https://rms.h3dgamma.com/api"));
    m_urlEdit->setMinimumWidth(300);
    ctrl->addWidget(new QLabel(QStringLiteral("Server URL:")), 4, 0);
    ctrl->addWidget(m_urlEdit, 4, 1, 1, 6);
    m_saveUrlBtn = new QPushButton(QStringLiteral("Save URL"));
    ctrl->addWidget(m_saveUrlBtn, 4, 7);

    m_cbRequests = new QCheckBox(QStringLiteral("Requests")); m_cbRequests->setChecked(true);
    m_cbErrors   = new QCheckBox(QStringLiteral("Errors"));   m_cbErrors->setChecked(true);
    m_cbGpu      = new QCheckBox(QStringLiteral("GPU temp")); m_cbGpu->setChecked(true);
    m_cbCpu      = new QCheckBox(QStringLiteral("CPU temp")); m_cbCpu->setChecked(true);
    m_cbIncludeMda = new QCheckBox(QStringLiteral("Include MDA")); m_cbIncludeMda->setChecked(false);
    QHBoxLayout *cbs = new QHBoxLayout;
    cbs->addWidget(m_cbRequests); cbs->addWidget(m_cbErrors);
    cbs->addSpacing(16); cbs->addWidget(m_cbGpu); cbs->addWidget(m_cbCpu);
    cbs->addSpacing(16); cbs->addWidget(m_cbIncludeMda);
    cbs->addStretch();
    ctrl->addLayout(cbs, 3, 0, 1, 6);
    rootLay->addLayout(ctrl);

    // ---- main plot -------------------------------------------------------
    m_plot = new QCustomPlot;
    m_plot->setMinimumHeight(340);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->yAxis->setLabel(QStringLiteral("Counts per interval"));
    m_plot->yAxis2->setVisible(true);
    m_plot->yAxis2->setLabel(QStringLiteral("Temperature °C"));
    m_plot->setInteraction(QCP::iRangeZoom, true);

    QCPAxisTickerDateTime *tick = new QCPAxisTickerDateTime;
    tick->setDateTimeFormat(QStringLiteral("yyyy-MM-dd\nHH:mm"));
    tick->setTickCount(6);
    m_plot->xAxis->setTicker(QSharedPointer<QCPAxisTickerDateTime>(tick));
    m_plot->xAxis->setLabel(QStringLiteral("Date / time"));

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(255, 255, 255, 200)));
    rootLay->addWidget(m_plot, 6);

    // ---- navigator (overview + range selector) ----------------------------
    m_navigator = new QCustomPlot;
    m_navigator->setMinimumHeight(72);
    {
        QCPAxisTickerDateTime *nt = new QCPAxisTickerDateTime;
        nt->setDateTimeFormat(QStringLiteral("yyyy"));
        nt->setTickCount(10);
        m_navigator->xAxis->setTicker(QSharedPointer<QCPAxisTickerDateTime>(nt));
        m_navigator->yAxis->setVisible(false);
        m_navigator->yAxis->setRange(0, 1);
        m_navigator->yAxis2->setVisible(false);
        m_navigator->setInteractions(QCP::Interactions());
        m_navigator->addGraph();
        m_navigator->graph()->setPen(QPen(QColor(80, 120, 180, 160)));
        m_navigator->graph()->setBrush(QBrush(QColor(120, 160, 220, 90)));
    }
    m_navRect = new QCPItemRect(m_navigator);
    m_navRect->setPen(QPen(QColor(40, 40, 40, 160)));
    m_navRect->setBrush(QBrush(QColor(0, 160, 230, 50)));
    m_navRect->setSelectable(QCP::stNone);
    m_navEdgeL = new QCPItemLine(m_navigator);
    m_navEdgeR = new QCPItemLine(m_navigator);
    QPen ep(QColor(200, 30, 30), 2);
    m_navEdgeL->setPen(ep); m_navEdgeR->setPen(ep);
    m_navigator->installEventFilter(this);
    rootLay->addWidget(m_navigator, 1);

    // ---- daily rollup plot -----------------------------------------------
    QLabel *dailyTitle = new QLabel(QStringLiteral("<b>Daily rollup</b> — bars: avg counts/day · line: avg temp (°C, right axis)"));
    rootLay->addWidget(dailyTitle);
    m_daily = new QCustomPlot;
    m_daily->setMinimumHeight(190);
    m_daily->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    {
        QCPAxisTickerDateTime *dt = new QCPAxisTickerDateTime;
        dt->setDateTimeFormat(QStringLiteral("yyyy-MM"));
        dt->setTickCount(8);
        m_daily->xAxis->setTicker(QSharedPointer<QCPAxisTickerDateTime>(dt));
        m_daily->yAxis->setLabel(QStringLiteral("Avg counts / day"));
        m_daily->yAxis2->setVisible(true);
        m_daily->yAxis2->setLabel(QStringLiteral("°C"));
        m_daily->legend->setVisible(true);
    }
    rootLay->addWidget(m_daily, 3);

    statusBar()->showMessage(QStringLiteral("No data loaded."));

    // ---- connections ------------------------------------------------------
    connect(genBtn, &QPushButton::clicked, this, &MainWindow::regenerate);
    connect(expBtn, &QPushButton::clicked, this, &MainWindow::exportPng);
    connect(m_testBtn, &QPushButton::clicked, this, &MainWindow::testConnection);
    connect(m_showDetectorsBtn, &QPushButton::clicked, this, &MainWindow::showDetectors);
    connect(m_showLocationsBtn, &QPushButton::clicked, this, &MainWindow::showLocations);
    connect(m_showChartsBtn, &QPushButton::clicked, this, &MainWindow::showCharts);
    connect(m_loadChartDataBtn, &QPushButton::clicked, this, &MainWindow::loadChartData);
    connect(m_createChartBtn, &QPushButton::clicked, this, &MainWindow::createChart);
    connect(m_deleteChartBtn, &QPushButton::clicked, this, &MainWindow::deleteChart);
    connect(m_showLicensesBtn, &QPushButton::clicked, this, &MainWindow::showLicenses);
    connect(m_createLicenseBtn, &QPushButton::clicked, this, &MainWindow::createLicense);
    connect(m_deleteLicenseBtn, &QPushButton::clicked, this, &MainWindow::deleteLicense);
    connect(m_saveUrlBtn, &QPushButton::clicked, this, &MainWindow::saveUrl);
    connect(btn24h, &QPushButton::clicked, this, [this]{ showLast(86400.0 * 1);   });
    connect(btn7d,  &QPushButton::clicked, this, [this]{ showLast(86400.0 * 7);   });
    connect(btn30d, &QPushButton::clicked, this, [this]{ showLast(86400.0 * 30);  });
    connect(btnAll, &QPushButton::clicked, this, [this]{ showLast(m_raw.endSec - m_raw.startSec + 1); });
    connect(m_binCombo, &QComboBox::currentIndexChanged, this, &MainWindow::binChanged);
    connect(m_smoothSlider, &QSlider::valueChanged, this, &MainWindow::smoothingChanged);
    for (QCheckBox *cb : {m_cbRequests, m_cbErrors, m_cbGpu, m_cbCpu})
        connect(cb, &QCheckBox::toggled, this, [this]{ replotMain(); });
    connect(m_cbIncludeMda, &QCheckBox::toggled, this, [this]{ replotChartData(); });

    // keep navigator rectangle + Y autoscale in sync with main x range
    connect(m_plot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this]{ updateNavRect(); });
}

void MainWindow::binChanged(int idx)
{
    static const int secs[] = {60, 300, 900, 3600, 21600, 86400};
    m_binSec = secs[qBound(0, idx, 5)];
    replotMain();
    if (!m_chartPoints.isEmpty())
        replotChartData();
}

void MainWindow::smoothingChanged(int minutes)
{
    m_smoothMin = minutes;
    m_smoothLabel->setText(minutes <= 0
        ? QStringLiteral("Smoothing: off")
        : QStringLiteral("Smoothing: %1 min moving average").arg(minutes));
    replotMain();
}

void MainWindow::regenerate()
{
    QElapsedTimer et; et.start();
    const int n = 730 * 1440;                       // 2 years @ 1 min
    const double endSec = QDateTime::currentSecsSinceEpoch();
    TelemetryData d = generateTelemetry(endSec - n * 60.0, n, 60.0,
                                        quint64(QDateTime::currentMSecsSinceEpoch()));
    load(d);
    statusBar()->showMessage(QStringLiteral("Generated %L1 points in %2 ms")
        .arg(d.count()).arg(et.elapsed()));
}

void MainWindow::load(const TelemetryData &d)
{
    m_raw = d;
    m_chartPoints.clear();          // chart data cleared when telemetry loaded
    if (m_raw.count() == 0) return;

    // navigator overview: decimate to ~4000 pts
    const int stride = qMax(1, m_raw.count() / 4000);
    QVector<double> dk, dv;
    dk.reserve(m_raw.count() / stride + 1); dv.reserve(dk.capacity());
    double sum = 0; int cnt = 0;
    for (int i = 0; i < m_raw.count(); ++i) {
        sum += m_raw.requests[i]; ++cnt;
        if ((i + 1) % stride == 0) {
            dk.append(m_raw.key[i - cnt + stride]);
            dv.append(sum / cnt);
            sum = 0; cnt = 0;
        }
    }
    m_navigator->graph()->setData(dk, dv);
    m_navigator->xAxis->setRange(m_raw.startSec, m_raw.endSec);
    m_navigator->yAxis->rescale(true);
    m_navigator->replot();

    // main graphs (style set once, data refreshed by replotMain)
    m_plot->clearGraphs();
    m_plot->addGraph(); m_plot->graph(0)->setName(QStringLiteral("Requests"));
    m_plot->graph(0)->setPen(QPen(QColor(0, 100, 200), 1.2));
    m_plot->addGraph(); m_plot->graph(1)->setName(QStringLiteral("Errors"));
    m_plot->graph(1)->setPen(QPen(QColor(200, 30, 30), 1.2));
    m_plot->addGraph(m_plot->xAxis, m_plot->yAxis2);
    m_plot->graph(2)->setName(QStringLiteral("GPU temp"));
    m_plot->graph(2)->setPen(QPen(QColor(150, 0, 90), 1.2, Qt::DashLine));
    m_plot->addGraph(m_plot->xAxis, m_plot->yAxis2);
    m_plot->graph(3)->setName(QStringLiteral("CPU temp"));
    m_plot->graph(3)->setPen(QPen(QColor(230, 130, 0), 1.2, Qt::DashDotLine));
    for (int g = 0; g < 4; ++g) m_plot->graph(g)->setAdaptiveSampling(true);

    showLast(86400.0);   // default view: last 24 h
    replotMain();
    buildDailyPlot();
    statusBar()->showMessage(
        QStringLiteral("Loaded %L1 points (%2 \xE2\x86\x92 %3)")
            .arg(m_raw.count()).arg(fmtSecs(m_raw.startSec), fmtSecs(m_raw.endSec)));
}

void MainWindow::replotMain()
{
    if (m_raw.count() == 0 || !m_plot->graphCount()) return;

    const int winPts = m_smoothMin > 0
        ? qMax(1, int(m_smoothMin * 60.0 / m_raw.intervalSec)) : 0;
    QVector<double> req = movingAvg(m_raw.requests, winPts);
    QVector<double> err = movingAvg(m_raw.errors, winPts);
    QVector<double> gpu = movingAvg(m_raw.gpuTemp, winPts);
    QVector<double> cpu = movingAvg(m_raw.cpuTemp, winPts);

    QVector<double> bk, bv;
    binMean(m_raw.key, req, m_raw.startSec, m_raw.endSec, m_binSec, bk, bv);
    m_plot->graph(0)->setData(bk, bv, /*alreadySorted=*/true);
    QVector<double> ek, ev; binMean(m_raw.key, err, m_raw.startSec, m_raw.endSec, m_binSec, ek, ev);
    m_plot->graph(1)->setData(ek, ev, true);
    QVector<double> gk, gv; binMean(m_raw.key, gpu, m_raw.startSec, m_raw.endSec, m_binSec, gk, gv);
    m_plot->graph(2)->setData(gk, gv, true);
    QVector<double> ck, cv; binMean(m_raw.key, cpu, m_raw.startSec, m_raw.endSec, m_binSec, ck, cv);
    m_plot->graph(3)->setData(ck, cv, true);

    m_plot->graph(0)->setVisible(m_cbRequests ? m_cbRequests->isChecked() : true);
    m_plot->graph(1)->setVisible(m_cbErrors   ? m_cbErrors->isChecked()   : true);
    m_plot->graph(2)->setVisible(m_cbGpu      ? m_cbGpu->isChecked()      : true);
    m_plot->graph(3)->setVisible(m_cbCpu      ? m_cbCpu->isChecked()      : true);

    // autoscale Y axes to visible window (sampled so panning stays cheap)
    m_plot->yAxis->rescale(true);
    m_plot->yAxis2->rescale(true);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::replotChartData()
{
    if (m_chartPoints.isEmpty()) return;

    m_plot->clearGraphs();

    // Build unique isotope ID list (treated as strings, not numbers)
    QStringList isotopeIds;
    QMap<QString, QVector<ChartPoint>> pointsByIsotope;
    for (const ChartPoint &cp : m_chartPoints) {
        if (!isotopeIds.contains(cp.isotopeId))
            isotopeIds << cp.isotopeId;
        pointsByIsotope[cp.isotopeId].append(cp);
    }
    std::sort(isotopeIds.begin(), isotopeIds.end());

    // Color palette for multiple isotope lines
    QVector<QColor> colors = {
        QColor(0, 100, 200), QColor(200, 30, 30), QColor(0, 150, 0),
        QColor(150, 0, 90), QColor(230, 130, 0), QColor(100, 100, 100),
        QColor(0, 150, 150), QColor(150, 100, 0), QColor(120, 60, 180),
        QColor(60, 120, 180)
    };

    bool includeMda = m_cbIncludeMda ? m_cbIncludeMda->isChecked() : false;

    double overallMinTime = 1e18, overallMaxTime = -1e18;

    int graphIdx = 0;
    for (const QString &isoId : isotopeIds) {
        QVector<ChartPoint> pts = pointsByIsotope[isoId];

        // Sort by time (handles any ordering from server)
        std::sort(pts.begin(), pts.end(), [](const ChartPoint &a, const ChartPoint &b) {
            return a.time < b.time;
        });

        // Extract key/val vectors for binning
        QVector<double> key, val;
        key.reserve(pts.size());
        val.reserve(pts.size());
        for (const ChartPoint &cp : pts) {
            key.append(cp.time);
            val.append(includeMda ? cp.netArea + cp.mda : cp.netArea);
        }

        // Bin by the selected interval (summing values in each bin)
        QVector<double> binX, binY;
        binChartData(key, val, static_cast<double>(m_binSec), binX, binY);

        if (binX.isEmpty()) continue;

        // Create a trend line for this isotope
        m_plot->addGraph();
        m_plot->graph(graphIdx)->setName(QStringLiteral("Isotope %1").arg(isoId));
        m_plot->graph(graphIdx)->setPen(QPen(colors[graphIdx % colors.size()], 1.2));
        m_plot->graph(graphIdx)->setData(binX, binY, true);
        m_plot->graph(graphIdx)->setAdaptiveSampling(true);

        overallMinTime = qMin(overallMinTime, binX.first());
        overallMaxTime = qMax(overallMaxTime, binX.last());

        graphIdx++;
    }

    // Configure axes for isotope chart display
    QCPAxisTickerDateTime *tick = new QCPAxisTickerDateTime;
    tick->setDateTimeFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    tick->setTickCount(8);
    m_plot->xAxis->setTicker(QSharedPointer<QCPAxisTickerDateTime>(tick));
    m_plot->xAxis->setLabel(QStringLiteral("Date / time (local)"));
    m_plot->yAxis->setLabel(includeMda ? QStringLiteral("Net Area + MDA") : QStringLiteral("Net Area"));
    m_plot->legend->setVisible(true);

    if (graphIdx > 0) {
        m_plot->xAxis->setRange(overallMinTime, overallMaxTime);
        m_plot->yAxis->rescale(true);
    }
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void MainWindow::buildDailyPlot()
{
    if (m_raw.count() == 0) return;
    m_daily->clearPlottables();

    QVector<double> dKey, dReq, dTemp;
    binMean(m_raw.key, m_raw.requests, m_raw.startSec, m_raw.endSec, 86400.0, dKey, dReq);
    QVector<double> tk, tv;
    binMean(m_raw.key, m_raw.gpuTemp, m_raw.startSec, m_raw.endSec, 86400.0, tk, tv);

    QCPBars *bars = new QCPBars(m_daily->xAxis, m_daily->yAxis);
    bars->setName(QStringLiteral("Avg counts/day"));
    bars->setPen(QPen(QColor(0, 100, 200, 60)));
    bars->setBrush(QBrush(QColor(70, 140, 210, 150)));
    bars->setWidth(0.7 * 86400.0);
    bars->setData(dKey, dReq, true);

    QCPGraph *tg = m_daily->addGraph(m_daily->xAxis, m_daily->yAxis2);
    tg->setName(QStringLiteral("Avg GPU °C"));
    tg->setPen(QPen(QColor(150, 0, 90), 1.4));
    tg->setData(tk, tv, true);

    m_daily->xAxis->setRange(m_raw.startSec, m_raw.endSec);
    m_daily->yAxis->rescale(true);
    m_daily->yAxis2->rescale(true);
    m_daily->replot();
}

void MainWindow::showLast(double seconds)
{
    if (m_raw.count() == 0) return;
    const double b = qMax(m_raw.startSec, m_raw.endSec - seconds);
    m_plot->xAxis->setRange(b, m_raw.endSec);
    updateNavRect();
    m_plot->replot();
}

void MainWindow::updateNavRect()
{
    if (!m_navRect || m_raw.count() == 0 || m_updatingNav) return;
    m_updatingNav = true;
    const double a = m_plot->xAxis->range().lower;
    const double b = m_plot->xAxis->range().upper;
    m_navRect->topLeft->setCoords(a, 1.0);
    m_navRect->bottomRight->setCoords(b, 0.0);
    m_navEdgeL->start->setCoords(a, 1.0);    m_navEdgeL->end->setCoords(a, 0.0);
    m_navEdgeR->start->setCoords(b, 1.0);    m_navEdgeR->end->setCoords(b, 0.0);
    m_navigator->replot(QCustomPlot::rpQueuedRefresh);
    m_updatingNav = false;
}

void MainWindow::setSelectRange(double a, double b)
{
    a = qBound(m_raw.startSec, a, m_raw.endSec);
    b = qBound(m_raw.startSec, b, m_raw.endSec);
    if (b - a < 300.0) { const double m = 0.5 * (a + b); a = m - 150.0; b = m + 150.0; }
    m_plot->xAxis->setRange(a, b);
    m_plot->yAxis->rescale(true);
    m_plot->yAxis2->rescale(true);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != m_navigator || m_raw.count() == 0)
        return QMainWindow::eventFilter(obj, ev);

    QMouseEvent *me = static_cast<QMouseEvent *>(ev);
    switch (ev->type()) {
    case QEvent::MouseButtonPress: {
        if (me->button() != Qt::LeftButton) break;
        const int x = int(me->position().x());
        const double t = m_navigator->xAxis->pixelToCoord(x);
        const double l = m_navRect->topLeft->coords().x();
        const double r = m_navRect->bottomRight->coords().x();
        const double lp = m_navigator->xAxis->coordToPixel(l);
        const double rp = m_navigator->xAxis->coordToPixel(r);
        if (std::abs(x - lp) <= 6.0)      m_navMode = NavLeft;
        else if (std::abs(x - rp) <= 6.0) m_navMode = NavRight;
        else if (t > l && t < r)        { m_navMode = NavMove; m_navGrabOffset = t - l; }
        else { m_navMode = NavNew; m_navAnchor = t; setSelectRange(t, t); }
        return true;
    }
    case QEvent::MouseMove: {
        if (m_navMode == NavNone) break;
        const double t = m_navigator->xAxis->pixelToCoord(me->position().x());
        const double l = m_navRect->topLeft->coords().x();
        const double r = m_navRect->bottomRight->coords().x();
        switch (m_navMode) {
        case NavLeft:  setSelectRange(qMin(t, r - 300.0), r); break;
        case NavRight: setSelectRange(l, qMax(t, l + 300.0)); break;
        case NavMove: { const double span = r - l; setSelectRange(t - m_navGrabOffset, t - m_navGrabOffset + span); break; }
        case NavNew:   setSelectRange(m_navAnchor, t); break;
        default: break;
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (m_navMode != NavNone) { m_navMode = NavNone; return true; }
        break;
    default: break;
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::showLicenses()
{
    logDebug(QStringLiteral("showLicenses: GET /licenses"));
    std::string body = httpGet("licenses", 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("showLicenses: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Licenses failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON response: %1").arg(QString::fromStdString(body));
        logError(QStringLiteral("showLicenses: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Licenses failed"), err);
        return;
    }

    QJsonArray licenses = doc.object().value(QStringLiteral("licenses")).toArray();
    QStringList items;
    for (const QJsonValue &v : licenses) {
        QJsonObject obj = v.toObject();
        qint64 id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
        QString content = obj.value(QStringLiteral("content")).toString();
        QString preview = content;
        if (preview.length() > 80) preview = preview.left(80) + QStringLiteral("...");
        items << QStringLiteral("ID %1: %2").arg(id).arg(preview);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Licenses"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);
    QListWidget *list = new QListWidget(&dialog);
    if (items.isEmpty())
        list->addItem(QStringLiteral("(no licenses in database)"));
    else
        list->addItems(items);
    lay->addWidget(list);
    QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), &dialog);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::createLicense()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create License"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);

    lay->addWidget(new QLabel(QStringLiteral("Paste the license text below:"), &dialog));
    QTextEdit *textEdit = new QTextEdit(&dialog);
    textEdit->setMinimumSize(600, 400);
    lay->addWidget(textEdit);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    lay->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        logDebug(QStringLiteral("createLicense: user cancelled"));
        return;
    }

    QString content = textEdit->toPlainText().trimmed();
    if (content.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Create License"),
            QStringLiteral("License text cannot be empty."));
        return;
    }

    logDebug(QStringLiteral("createLicense: PUT /license/create"));
    std::string resp = httpPut("license/create", content.toStdString(), 15);
    if (resp.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("createLicense: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Create License failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(resp).toUtf8());
    QString msg;
    if (!doc.isNull() && doc.isObject()) {
        QString error = doc.object().value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            msg = QStringLiteral("Server error: %1").arg(error);
        } else {
            qint64 licId = doc.object().value(QStringLiteral("license_id")).toVariant().toLongLong();
            msg = QStringLiteral("License created successfully (ID %1).").arg(licId);
        }
    } else {
        msg = QStringLiteral("License created.\n%1").arg(QString::fromStdString(resp));
    }

    QMessageBox::information(this, QStringLiteral("Create License"), msg);
}

void MainWindow::deleteLicense()
{
    bool ok = false;
    qint64 licenseId = QInputDialog::getInt(this, QStringLiteral("Delete License"),
        QStringLiteral("License ID:"), 1, 1, 1000000, 1, &ok);
    if (!ok) return;

    QString path = QStringLiteral("license/delete?id=%1").arg(licenseId);
    logDebug(QStringLiteral("deleteLicense: DELETE %1").arg(path));

    std::string baseUrl = m_baseUrl;
    size_t pathStart = baseUrl.find('/', 8);
    if (pathStart != std::string::npos) {
        baseUrl = baseUrl.substr(0, pathStart);
    }
    std::string fullPath = path.toStdString() + "&mode=html";

    httplib::Result res;
    {
        httplib::Client cli(baseUrl);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(10);
        res = cli.Delete(fullPath.c_str());
    }

    if (!res) {
        QString err = QStringLiteral("Request failed (%1).")
            .arg(httplib::to_string(res.error()));
        logError(QStringLiteral("deleteLicense: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Delete License failed"), err);
        return;
    }

    if (res->status != 200) {
        QString err = QStringLiteral("HTTP %1 from server.").arg(res->status);
        logError(QStringLiteral("deleteLicense: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Delete License failed"), err);
        return;
    }

    QString body = QString::fromStdString(res->body);
    QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
    QString msg;
    if (!doc.isNull() && doc.isObject()) {
        QString error = doc.object().value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            msg = QStringLiteral("Server error: %1").arg(error);
        } else {
            msg = QStringLiteral("License deleted successfully.\n%1").arg(body);
        }
    } else {
        msg = QStringLiteral("License deleted.\n%1").arg(body);
    }

    QMessageBox::information(this, QStringLiteral("Delete License"), msg);
}

void MainWindow::testConnection()
{
    logDebug(QStringLiteral("testConnection: GET http://localhost:22438/detectors"));
    std::string body = httpGet("detectors", 5);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.\nCheck that the server is running on port 22438.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("testConnection: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Connection test failed"), err);
        return;
    }
    logDebug(QStringLiteral("testConnection: response=%1").arg(QString::fromStdString(body).left(200)));
    QMessageBox::information(this, QStringLiteral("Connection test success!"),
        QStringLiteral("Successfully reached H3DPolarisServer HTTP API on port 22438."));
}

void MainWindow::showDetectors()
{
    logDebug(QStringLiteral("showDetectors: GET /detectors"));
    std::string body = httpGet("detectors", 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("showDetectors: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON response: %1").arg(QString::fromStdString(body));
        logError(QStringLiteral("showDetectors: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"), err);
        return;
    }

    QJsonArray detectors = doc.object().value(QStringLiteral("detectors")).toArray();
    QStringList names;
    for (const QJsonValue &v : detectors)
        names << v.toObject().value(QStringLiteral("name")).toString();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Detectors"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);
    QListWidget *list = new QListWidget(&dialog);
    if (names.isEmpty())
        list->addItem(QStringLiteral("(no detectors in database)"));
    else
        list->addItems(names);
    lay->addWidget(list);
    QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), &dialog);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::showLocations()
{
    logDebug(QStringLiteral("showLocations: GET /locations"));
    std::string body = httpGet("locations", 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("showLocations: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON response: %1").arg(QString::fromStdString(body));
        logError(QStringLiteral("showLocations: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"), err);
        return;
    }

    QJsonArray locations = doc.object().value(QStringLiteral("locations")).toArray();
    QStringList names;
    for (const QJsonValue &v : locations)
        names << v.toObject().value(QStringLiteral("name")).toString();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Locations"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);
    QListWidget *list = new QListWidget(&dialog);
    if (names.isEmpty())
        list->addItem(QStringLiteral("(no locations in database)"));
    else
        list->addItems(names);
    lay->addWidget(list);
    QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), &dialog);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::showCharts()
{
    logDebug(QStringLiteral("showCharts: GET /charts"));
    std::string body = httpGet("charts", 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("showCharts: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON response: %1").arg(QString::fromStdString(body));
        logError(QStringLiteral("showCharts: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"), err);
        return;
    }

    QJsonArray charts = doc.object().value(QStringLiteral("charts")).toArray();
    QStringList names;
    for (const QJsonValue &v : charts)
        names << v.toObject().value(QStringLiteral("name")).toString();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Charts"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);
    QListWidget *list = new QListWidget(&dialog);
    if (names.isEmpty())
        list->addItem(QStringLiteral("(no charts in database)"));
    else
        list->addItems(names);
    lay->addWidget(list);
    QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), &dialog);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::loadChartData()
{
    // Step 1: get the JSON list of charts.
    logDebug(QStringLiteral("loadChartData: GET /charts"));
    std::string body = httpGet("charts", 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON response: %1").arg(QString::fromStdString(body));
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data failed"), err);
        return;
    }

    QJsonArray charts = doc.object().value(QStringLiteral("charts")).toArray();
    logDebug(QStringLiteral("loadChartData: received %1 charts from server").arg(charts.size()));
    if (charts.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Load Chart Data"),
            QStringLiteral("No charts found in the database."));
        return;
    }

    // Step 2: show a dialog with all available charts and command selection.
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Load Chart Data"));
    QVBoxLayout *lay = new QVBoxLayout(&dialog);
    lay->addWidget(new QLabel(QStringLiteral("Select a chart and the command to run:"), &dialog));

    QListWidget *list = new QListWidget(&dialog);
    QHash<QString, qint64> chartIdByName;
    for (const QJsonValue &v : charts) {
        QJsonObject obj = v.toObject();
        QString name = obj.value(QStringLiteral("name")).toString();
        qint64 id = obj.value(QStringLiteral("id")).toVariant().toLongLong();
        list->addItem(name);
        chartIdByName.insert(name, id);
    }
    lay->addWidget(list);

    QComboBox *cmdCombo = new QComboBox(&dialog);
    cmdCombo->addItems({QStringLiteral("chart-data"), QStringLiteral("chart-tail"), QStringLiteral("chart-all")});
    lay->addWidget(new QLabel(QStringLiteral("Command:"), &dialog));
    lay->addWidget(cmdCombo);

    QPushButton *okBtn = new QPushButton(QStringLiteral("Load"), &dialog);
    QPushButton *cancelBtn = new QPushButton(QStringLiteral("Cancel"), &dialog);
    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->addStretch();
    btnLay->addWidget(okBtn);
    btnLay->addWidget(cancelBtn);
    lay->addLayout(btnLay);
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        logDebug(QStringLiteral("loadChartData: user cancelled chart selection"));
        return;
    }

    QListWidgetItem *selected = list->currentItem();
    if (!selected) {
        QString err = QStringLiteral("No chart selected.");
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data"), err);
        return;
    }
    qint64 chartId = chartIdByName.value(selected->text(), -1);
    if (chartId <= 0) {
        QString err = QStringLiteral("Could not determine chart ID.");
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data failed"), err);
        return;
    }
    QString command = cmdCombo->currentText();
    logDebug(QStringLiteral("loadChartData: selected chart id=%1 name=%2 command=%3")
        .arg(chartId).arg(selected->text()).arg(command));

    if (command == QStringLiteral("chart-all")) {
        loadChartAll(chartId);
        return;
    }

    // Step 2.5: ask the user for the max rows to limit the chart data output.
    bool ok = false;
    int maxRows = QInputDialog::getInt(this, QStringLiteral("Max Rows"),
        QStringLiteral("Enter maximum number of rows to fetch (e.g. 100, or -1 for all):"),
        100, -1000000, 1000000, 1, &ok);
    if (!ok) {
        logDebug(QStringLiteral("loadChartData: user cancelled max rows dialog"));
        return;
    }
    logDebug(QStringLiteral("loadChartData: user requested maxrows=%1").arg(maxRows));

    // Step 3: clear the demo data and any previous chart data from the GUI.
    m_raw = TelemetryData();
    m_chartPoints.clear();
    m_plot->clearGraphs();
    m_navigator->clearGraphs();
    m_daily->clearPlottables();
    m_plot->replot();
    m_navigator->replot();
    m_daily->replot();

    // Step 4: GET /chart-data or /chart-tail
    QString path;
    if (command == QStringLiteral("chart-tail")) {
        path = QStringLiteral("chart-tail?id=%1&maxrows=%2").arg(chartId).arg(maxRows);
    } else {
        path = QStringLiteral("chart-data?id=%1&maxrows=%2").arg(chartId).arg(maxRows);
    }
    logDebug(QStringLiteral("loadChartData: GET %1").arg(path));
    std::string dataResp = httpGet(path.toStdString(), 30);
    if (dataResp.empty()) {
        QString err = QStringLiteral("No chart data returned from %1.")
            .arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data failed"), err);
        return;
    }

    logDebug(QStringLiteral("loadChartData: raw response (first 200 chars): %1")
        .arg(QString::fromStdString(dataResp).left(200)));

    QJsonDocument dataDoc = QJsonDocument::fromJson(QString::fromStdString(dataResp).toUtf8());
    if (dataDoc.isNull() || !dataDoc.isObject()) {
        QString err = QStringLiteral("Invalid JSON chart data: %1").arg(QString::fromStdString(dataResp));
        logError(QStringLiteral("loadChartData: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Load Chart Data failed"), err);
        return;
    }

    QJsonObject dataObj = dataDoc.object();
    QJsonArray points = dataObj.value(QStringLiteral("points")).toArray();
    logDebug(QStringLiteral("loadChartData: received %1 points for chart %2").arg(points.size()).arg(chartId));

    // Step 5: parse points into isotope-id-grouped data.
    // Each point has: isotope_id (string), point_time (unix seconds), net_area (double), mda (double).
    m_chartPoints.clear();
    m_chartPoints.reserve(points.size());

    for (const QJsonValue &v : points) {
        QJsonObject pt = v.toObject();

        QString isotopeId = pt.value(QStringLiteral("isotope_id")).toString();
        if (isotopeId.isEmpty()) {
            // Fallback: try numeric id converted to string
            isotopeId = QString::number(pt.value(QStringLiteral("isotope_id")).toVariant().toLongLong());
        }

        qint64 unixSec = pt.value(QStringLiteral("point_time")).toVariant().toLongLong();

        // Convert unix timestamp to local-time epoch seconds so QCustomPlot's
        // date-time ticker shows local wall-clock time.
        QDateTime utc = QDateTime::fromSecsSinceEpoch(unixSec, Qt::UTC);
        QDateTime local = utc.toLocalTime();
        double localSecs = local.toSecsSinceEpoch();

        double netArea = pt.value(QStringLiteral("net_area")).toDouble();
        double mda = pt.value(QStringLiteral("mda")).toDouble();

        m_chartPoints.append({localSecs, isotopeId, netArea, mda});
    }

    // count unique isotopes for the debug log
    QMap<QString, bool> seenIds;
    for (const ChartPoint &cp : m_chartPoints)
        seenIds[cp.isotopeId] = true;
    logDebug(QStringLiteral("loadChartData: parsed %1 points across %2 unique isotope IDs")
        .arg(m_chartPoints.size()).arg(seenIds.size()));

    // Step 6: plot the data — one trend line per unique isotope ID.
    replotChartData();

    // Count unique isotopes for the status message
    QMap<QString, bool> seen;
    int uniqueIsotopes = 0;
    for (const ChartPoint &cp : m_chartPoints) {
        if (!seen.contains(cp.isotopeId)) {
            seen[cp.isotopeId] = true;
            uniqueIsotopes++;
        }
    }

    statusBar()->showMessage(
        QStringLiteral("Loaded chart %1: %2 points, %3 isotope(s)")
        .arg(chartId).arg(points.size()).arg(uniqueIsotopes));
    logDebug(QStringLiteral("loadChartData: successfully plotted chart %1 with %2 points across %3 isotope(s)")
        .arg(chartId).arg(points.size()).arg(uniqueIsotopes));
}

void MainWindow::loadChartAll(qint64 chartId)
{
    // Step 1: Request chart-all (instant JSON status response)
    QString path = QStringLiteral("chart-all?id=%1&mode=json").arg(chartId);
    logDebug(QStringLiteral("loadChartAll: GET %1").arg(path));;
    std::string resp = httpGet(path.toStdString(), 10);
    if (resp.empty()) {
        QString err = QStringLiteral("No response from chart-all for chart %1.").arg(chartId);
        logError(QStringLiteral("loadChartAll: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Chart All failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(resp).toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QString err = QStringLiteral("Invalid JSON from chart-all: %1").arg(QString::fromStdString(resp));
        logError(QStringLiteral("loadChartAll: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Chart All failed"), err);
        return;
    }

    QJsonObject obj = doc.object();
    QString statusUrl = obj.value(QStringLiteral("status_url")).toString();
    QString fileName  = obj.value(QStringLiteral("file_name")).toString();
    QString downloadUrl = obj.value(QStringLiteral("download_url")).toString();

    if (statusUrl.isEmpty() || fileName.isEmpty() || downloadUrl.isEmpty()) {
        QString err = QStringLiteral("Incomplete status response from chart-all.");
        logError(QStringLiteral("loadChartAll: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Chart All failed"), err);
        return;
    }

    // Step 2: Show progress dialog with two progress bars
    QDialog progressDialog(this);
    progressDialog.setWindowTitle(QStringLiteral("Chart All — Creating File"));
    progressDialog.setModal(true);
    QVBoxLayout *pLay = new QVBoxLayout(&progressDialog);

    QLabel *statusLabel = new QLabel(QStringLiteral("Requesting file creation..."), &progressDialog);
    pLay->addWidget(statusLabel);

    QProgressBar *createProgress = new QProgressBar(&progressDialog);
    createProgress->setRange(0, 100);
    createProgress->setValue(0);
    pLay->addWidget(createProgress);

    QProgressBar *downloadProgress = new QProgressBar(&progressDialog);
    downloadProgress->setRange(0, 100);
    downloadProgress->setValue(0);
    downloadProgress->hide();
    pLay->addWidget(downloadProgress);

    QPushButton *closeBtn = new QPushButton(QStringLiteral("Close"), &progressDialog);
    closeBtn->setEnabled(false);
    pLay->addWidget(closeBtn, 0, Qt::AlignRight);

    progressDialog.show();

    // Step 3: Poll the status URL until the file is ready
    bool ready = false;
    qint64 finalSize = 0;
    int pollCount = 0;
    const int maxPolls = 120;

    while (!ready && pollCount < maxPolls) {
        QCoreApplication::processEvents();
        if (!progressDialog.isVisible()) {
            return;
        }

        std::string statusResp = httpGet(statusUrl.toStdString(), 5);
        if (!statusResp.empty()) {
            QJsonDocument statusDoc = QJsonDocument::fromJson(QString::fromStdString(statusResp).toUtf8());
            if (!statusDoc.isNull() && statusDoc.isObject()) {
                QJsonObject statusObj = statusDoc.object();
                ready = statusObj.value(QStringLiteral("ready")).toBool();
                qint64 currentSize = statusObj.value(QStringLiteral("current_size")).toVariant().toLongLong();
                finalSize = statusObj.value(QStringLiteral("final_size")).toVariant().toLongLong();

                if (finalSize > 0) {
                    int pct = int(100.0 * currentSize / finalSize);
                    createProgress->setValue(pct);
                    statusLabel->setText(QStringLiteral("Creating file... %1% (%2 / %3 bytes)")
                        .arg(pct).arg(currentSize).arg(finalSize));
                } else {
                    statusLabel->setText(QStringLiteral("Creating file..."));
                }
            }
        }

        if (!ready) {
            QThread::msleep(3000);
            pollCount++;
        }
    }

    if (!ready) {
        statusLabel->setText(QStringLiteral("File creation timed out."));
        closeBtn->setEnabled(true);
        progressDialog.exec();
        return;
    }

    // Step 4: Download the file
    createProgress->hide();
    downloadProgress->show();
    statusLabel->setText(QStringLiteral("Downloading file..."));

    QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Chart All File"), fileName,
        QStringLiteral("JSON files (*.json)"));
    if (savePath.isEmpty()) {
        closeBtn->setEnabled(true);
        progressDialog.exec();
        return;
    }

    downloadFile(downloadUrl, savePath, downloadProgress, statusLabel);

    statusLabel->setText(QStringLiteral("Download complete! Saved to %1.").arg(savePath));
    closeBtn->setEnabled(true);
    progressDialog.exec();

    // Step 5: Parse and plot the downloaded data
    QFile file(savePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Chart All"),
            QStringLiteral("Could not open downloaded file for parsing."));
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument dataDoc = QJsonDocument::fromJson(fileData);
    if (dataDoc.isNull() || !dataDoc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Chart All"),
            QStringLiteral("Downloaded file is not valid JSON."));
        return;
    }

    QJsonObject dataObj = dataDoc.object();
    QJsonArray points = dataObj.value(QStringLiteral("points")).toArray();

    m_raw = TelemetryData();
    m_chartPoints.clear();
    m_chartPoints.reserve(points.size());
    m_plot->clearGraphs();
    m_navigator->clearGraphs();
    m_daily->clearPlottables();
    m_plot->replot();
    m_navigator->replot();
    m_daily->replot();

    for (const QJsonValue &v : points) {
        QJsonObject pt = v.toObject();

        QString isotopeId = pt.value(QStringLiteral("isotope_id")).toString();
        if (isotopeId.isEmpty()) {
            isotopeId = QString::number(pt.value(QStringLiteral("isotope_id")).toVariant().toLongLong());
        }

        qint64 unixSec = pt.value(QStringLiteral("point_time")).toVariant().toLongLong();

        QDateTime utc = QDateTime::fromSecsSinceEpoch(unixSec, Qt::UTC);
        QDateTime local = utc.toLocalTime();
        double localSecs = local.toSecsSinceEpoch();

        double netArea = pt.value(QStringLiteral("net_area")).toDouble();
        double mda = pt.value(QStringLiteral("mda")).toDouble();

        m_chartPoints.append({localSecs, isotopeId, netArea, mda});
    }

    replotChartData();

    statusBar()->showMessage(
        QStringLiteral("Chart All: loaded %1 points from chart %2").arg(points.size()).arg(chartId));
    logDebug(QStringLiteral("loadChartAll: successfully plotted %1 points from chart %2")
        .arg(points.size()).arg(chartId));
}

void MainWindow::downloadFile(const QString &urlPath, const QString &savePath,
                              QProgressBar *progressBar, QLabel *statusLabel)
{
    std::string baseUrl = m_baseUrl;
    size_t pathStart = baseUrl.find('/', 8);
    std::string pathPrefix;
    if (pathStart != std::string::npos) {
        pathPrefix = m_baseUrl.substr(pathStart);
        baseUrl = baseUrl.substr(0, pathStart);
    }

    std::string requestPath = pathPrefix + "/" + urlPath.toStdString();
    logDebug(QStringLiteral("downloadFile: GET %1 -> %2")
        .arg(QString::fromStdString(requestPath), savePath));

    httplib::Client cli(baseUrl);
    cli.set_connection_timeout(30);
    cli.set_read_timeout(300);

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QString err = QStringLiteral("Could not open file for writing: %1").arg(savePath);
        logError(QStringLiteral("downloadFile: %1").arg(err));
        return;
    }

    qint64 downloadedBytes = 0;

    auto res = cli.Get(requestPath.c_str(),
        [&](const char *data, uint64_t len) {
            if (file.isOpen()) {
                file.write(data, static_cast<qint64>(len));
                downloadedBytes += static_cast<qint64>(len);
                statusLabel->setText(QStringLiteral("Downloading... %1 bytes").arg(downloadedBytes));
                progressBar->setValue(0);
                QCoreApplication::processEvents();
            }
            return true;
        });

    file.close();

    if (!res || res->status != 200) {
        QString err = QStringLiteral("Download failed.");
        if (res) err = QStringLiteral("HTTP %1 from server.").arg(res->status);
        logError(QStringLiteral("downloadFile: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Download failed"), err);
        return;
    }

    progressBar->setValue(100);
    statusLabel->setText(QStringLiteral("Download complete."));
}

QPixmap MainWindow::renderSnapshot(int w, int h)
{
    showFullScreen();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    m_plot->replot();
    m_daily->replot();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QPixmap pm;
    if (QScreen *scr = QGuiApplication::primaryScreen())
        pm = scr->grabWindow(0);            // grabs the offscreen virtual screen
    if (pm.isNull() || pm.width() < 100)
        pm = grab();                        // fallback
    return pm;
}

QString MainWindow::statsText() const
{
    return QStringLiteral("points=%L1 visible=[%2 .. %3]")
        .arg(m_raw.count())
        .arg(fmtSecs(m_plot->xAxis->range().lower), fmtSecs(m_plot->xAxis->range().upper));
}

void MainWindow::exportPng()
{
    const QString fn = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export plot PNG"), QStringLiteral("gt_telemetry.png"),
        QStringLiteral("PNG image (*.png)"));
    if (fn.isEmpty()) return;
    if (m_plot->savePng(fn, 0, 0, 2.0))
        statusBar()->showMessage(QStringLiteral("Saved %1").arg(fn));
    else
        QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("Could not write %1").arg(fn));
}

void MainWindow::createChart()
{
    // Ask the user for chart type and parameters.
    QStringList types;
    types << QStringLiteral("Detector") << QStringLiteral("Location");
    bool ok = false;
    QString type = QInputDialog::getItem(this, QStringLiteral("Create Chart"),
        QStringLiteral("Chart type:"), types, 0, false, &ok);
    if (!ok) return;

    QString name = QInputDialog::getText(this, QStringLiteral("Create Chart"),
        QStringLiteral("Chart name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    int id = QInputDialog::getInt(this, QStringLiteral("Create Chart"),
        QStringLiteral("%1 ID:").arg(type), 1, 1, 1000000, 1, &ok);
    if (!ok) return;

    int libraryId = QInputDialog::getInt(this, QStringLiteral("Create Chart"),
        QStringLiteral("Library ID:"), 0, 0, 1000000, 1, &ok);
    if (!ok) return;

    QString path;
    if (type == QStringLiteral("Detector")) {
        path = QStringLiteral("chart/create/detector?name=%1&detectorID=%2&libraryID=%3")
            .arg(name).arg(id).arg(libraryId);
    } else {
        path = QStringLiteral("chart/create/location?name=%1&locationID=%2&libraryID=%3")
            .arg(name).arg(id).arg(libraryId);
    }

    logDebug(QStringLiteral("createChart: GET %1").arg(path));
    std::string body = httpGet(path.toStdString(), 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.").arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("createChart: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Create Chart failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    QString msg;
    if (!doc.isNull() && doc.isObject()) {
        QString error = doc.object().value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            msg = QStringLiteral("Server error: %1").arg(error);
        } else {
            msg = QStringLiteral("Chart created successfully.\n%1").arg(QString::fromStdString(body));
        }
    } else {
        msg = QStringLiteral("Chart created.\n%1").arg(QString::fromStdString(body));
    }

    QMessageBox::information(this, QStringLiteral("Create Chart"), msg);

    // Refresh the chart list so the new chart shows up.
    showCharts();
}

void MainWindow::deleteChart()
{
    bool ok = false;
    qint64 chartId = QInputDialog::getInt(this, QStringLiteral("Delete Chart"),
        QStringLiteral("Chart ID:"), 1, 1, 1000000, 1, &ok);
    if (!ok) return;

    QString path = QStringLiteral("chart/delete?id=%1").arg(chartId);
    logDebug(QStringLiteral("deleteChart: GET %1").arg(path));
    std::string body = httpGet(path.toStdString(), 10);
    if (body.empty()) {
        QString err = QStringLiteral("Could not reach the HTTP API at %1.").arg(QString::fromStdString(m_baseUrl));
        logError(QStringLiteral("deleteChart: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("Delete Chart failed"), err);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(body).toUtf8());
    QString msg;
    if (!doc.isNull() && doc.isObject()) {
        QString error = doc.object().value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            msg = QStringLiteral("Server error: %1").arg(error);
        } else {
            msg = QStringLiteral("Chart deleted successfully.\n%1").arg(QString::fromStdString(body));
        }
    } else {
        msg = QStringLiteral("Chart deleted.\n%1").arg(QString::fromStdString(body));
    }

    QMessageBox::information(this, QStringLiteral("Delete Chart"), msg);

    // Refresh the chart list.
    showCharts();
}

void MainWindow::saveUrl()
{
    QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Save URL"), QStringLiteral("URL cannot be empty."));
        return;
    }
    QSettings settings("H3D", "GammaTrendClient");
    settings.setValue("serverUrl", url);
    m_baseUrl = url.toStdString();
    logDebug(QStringLiteral("saveUrl: server URL set to %1").arg(url));
    QMessageBox::information(this, QStringLiteral("URL Saved"),
        QStringLiteral("Server URL saved: %1\nThis will be used on next launch.").arg(url));
}
