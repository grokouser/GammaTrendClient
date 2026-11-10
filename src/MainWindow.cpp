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
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

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

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    buildUi();
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

    m_cbRequests = new QCheckBox(QStringLiteral("Requests")); m_cbRequests->setChecked(true);
    m_cbErrors   = new QCheckBox(QStringLiteral("Errors"));   m_cbErrors->setChecked(true);
    m_cbGpu      = new QCheckBox(QStringLiteral("GPU temp")); m_cbGpu->setChecked(true);
    m_cbCpu      = new QCheckBox(QStringLiteral("CPU temp")); m_cbCpu->setChecked(true);
    QHBoxLayout *cbs = new QHBoxLayout;
    cbs->addWidget(m_cbRequests); cbs->addWidget(m_cbErrors);
    cbs->addSpacing(16); cbs->addWidget(m_cbGpu); cbs->addWidget(m_cbCpu);
    cbs->addStretch();
    ctrl->addLayout(cbs, 3, 0, 1, 6);
    rootLay->addLayout(ctrl);

    // ---- main plot -------------------------------------------------------
    m_plot = new QCustomPlot;
    m_plot->setMinimumHeight(340);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->yAxis->setLabel(QStringLiteral("Counts per interval"));
    m_plot->yAxis2->setVisible(true);
    m_plot->yAxis2->setLabel(QStringLiteral("Temperature \xC2\xB0""C"));
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
    QLabel *dailyTitle = new QLabel(QStringLiteral("<b>Daily rollup</b> — bars: avg counts/day · line: avg temp (\xC2\xB0""C, right axis)"));
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
        m_daily->yAxis2->setLabel(QStringLiteral("\xC2\xB0""C"));
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
    connect(btn24h, &QPushButton::clicked, this, [this]{ showLast(86400.0 * 1);   });
    connect(btn7d,  &QPushButton::clicked, this, [this]{ showLast(86400.0 * 7);   });
    connect(btn30d, &QPushButton::clicked, this, [this]{ showLast(86400.0 * 30);  });
    connect(btnAll, &QPushButton::clicked, this, [this]{ showLast(m_raw.endSec - m_raw.startSec + 1); });
    connect(m_binCombo, &QComboBox::currentIndexChanged, this, &MainWindow::binChanged);
    connect(m_smoothSlider, &QSlider::valueChanged, this, &MainWindow::smoothingChanged);
    for (QCheckBox *cb : {m_cbRequests, m_cbErrors, m_cbGpu, m_cbCpu})
        connect(cb, &QCheckBox::toggled, this, [this]{ replotMain(); });

    // keep navigator rectangle + Y autoscale in sync with main x range
    connect(m_plot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged),
            this, [this]{ updateNavRect(); });
}

void MainWindow::binChanged(int idx)
{
    static const int secs[] = {60, 300, 900, 3600, 21600, 86400};
    m_binSec = secs[qBound(0, idx, 5)];
    replotMain();
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
    tg->setName(QStringLiteral("Avg GPU \xC2\xB0""C"));
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

void MainWindow::testConnection()
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("localhost"), 22437);
    if (!socket.waitForConnected(3000)) {
        QMessageBox::warning(this, QStringLiteral("Connection test failed"),
            QStringLiteral("Could not connect to localhost:22437.\n%1").arg(socket.errorString()));
        return;
    }
    socket.write(QStringLiteral("test\n").toUtf8());
    if (!socket.waitForReadyRead(3000)) {
        QMessageBox::warning(this, QStringLiteral("Connection test failed"),
            QStringLiteral("Connected but no response from localhost:22437."));
        return;
    }
    QByteArray response = socket.readAll();
    socket.disconnectFromHost();
    if (response.contains("Hello World")) {
        QMessageBox::information(this, QStringLiteral("Connection test success!"),
            QStringLiteral("Successfully connected to H3DPolarisServer on port 22437."));
    } else {
        QMessageBox::warning(this, QStringLiteral("Connection test failed"),
            QStringLiteral("Unexpected response: %1").arg(QString::fromUtf8(response)));
    }
}

void MainWindow::showDetectors()
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("localhost"), 22437);
    if (!socket.waitForConnected(3000)) {
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"),
            QStringLiteral("Could not connect to localhost:22437.\n%1").arg(socket.errorString()));
        return;
    }

    // The console interface defaults to text mode; switch it to JSON for this
    // connection, then request the detector list.
    socket.write(QStringLiteral("mode json\n").toUtf8());
    QByteArray ack = readResponse(socket, 3000);
    if (ack.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"),
            QStringLiteral("Connected but no response from localhost:22437."));
        return;
    }

    socket.write(QStringLiteral("list detectors\n").toUtf8());
    QByteArray response = readResponse(socket, 3000);
    socket.disconnectFromHost();

    if (response.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"),
            QStringLiteral("Connected but no detector list from localhost:22437."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Show Detectors failed"),
            QStringLiteral("Invalid JSON response: %1").arg(QString::fromUtf8(response)));
        return;
    }

    QJsonArray detectors = doc.object().value(QStringLiteral("detectors")).toArray();
    QStringList names;
    for (const QJsonValue &v : detectors)
        names << v.toObject().value(QStringLiteral("name")).toString();

    // Popup window with the list of detectors (names only, for now).
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
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("localhost"), 22437);
    if (!socket.waitForConnected(3000)) {
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"),
            QStringLiteral("Could not connect to localhost:22437.\n%1").arg(socket.errorString()));
        return;
    }

    // The console interface defaults to text mode; switch it to JSON for this
    // connection, then request the location list.
    socket.write(QStringLiteral("mode json\n").toUtf8());
    QByteArray ack = readResponse(socket, 3000);
    if (ack.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"),
            QStringLiteral("Connected but no response from localhost:22437."));
        return;
    }

    socket.write(QStringLiteral("list locations\n").toUtf8());
    QByteArray response = readResponse(socket, 3000);
    socket.disconnectFromHost();

    if (response.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"),
            QStringLiteral("Connected but no location list from localhost:22437."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Show Locations failed"),
            QStringLiteral("Invalid JSON response: %1").arg(QString::fromUtf8(response)));
        return;
    }

    QJsonArray locations = doc.object().value(QStringLiteral("locations")).toArray();
    QStringList names;
    for (const QJsonValue &v : locations)
        names << v.toObject().value(QStringLiteral("name")).toString();

    // Popup window with the list of locations (names only, for now).
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
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("localhost"), 22437);
    if (!socket.waitForConnected(3000)) {
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"),
            QStringLiteral("Could not connect to localhost:22437.\n%1").arg(socket.errorString()));
        return;
    }

    // The console interface defaults to text mode; switch it to JSON for this
    // connection, then request the chart list.
    socket.write(QStringLiteral("mode json\n").toUtf8());
    QByteArray ack = readResponse(socket, 3000);
    if (ack.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"),
            QStringLiteral("Connected but no response from localhost:22437."));
        return;
    }

    socket.write(QStringLiteral("list charts\n").toUtf8());
    QByteArray response = readResponse(socket, 3000);
    socket.disconnectFromHost();

    if (response.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"),
            QStringLiteral("Connected but no chart list from localhost:22437."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("Show Charts failed"),
            QStringLiteral("Invalid JSON response: %1").arg(QString::fromUtf8(response)));
        return;
    }

    QJsonArray charts = doc.object().value(QStringLiteral("charts")).toArray();
    QStringList names;
    for (const QJsonValue &v : charts)
        names << v.toObject().value(QStringLiteral("name")).toString();

    // Popup window with the list of charts (names only, for now).
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

QByteArray MainWindow::readResponse(QTcpSocket &socket, int timeoutMs)
{
    QByteArray data;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        int remaining = timeoutMs - int(timer.elapsed());
        if (remaining <= 0) break;
        if (socket.waitForReadyRead(remaining < 1000 ? remaining : 1000)) {
            data += socket.readAll();
            if (data.endsWith('\n'))
                break;
        } else {
            break;
        }
    }
    return data;
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
