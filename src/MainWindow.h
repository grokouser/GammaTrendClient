#pragma once
#include <QMainWindow>
#include "Telemetry.h"
#include "cpp-httplib/httplib.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QSettings>
#include <QMap>
#include <QVector>
#include <QString>

class QCustomPlot;
class QCPGraph;
class QCPBars;
class QCPItemRect;
class QCPItemLine;
class QCheckBox;
class QSlider;
class QComboBox;
class QPushButton;
class QLabel;
class QDialog;
class QListWidget;
class QLineEdit;

// A single chart data point from the server.
// isotopeId is treated as a string (not a number) per spec.
struct ChartPoint {
    double time;        // unix seconds, already converted to local-time epoch
    QString isotopeId;  // string ID, compared as text
    double netArea;     // NetArea value
    double mda;         // Minimum Detectable Activity
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void load(const TelemetryData &d);
    QPixmap renderSnapshot(int w, int h);   // offscreen grab for headless verification
    QString statsText() const;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void regenerate();
    void binChanged(int idx);
    void smoothingChanged(int minutes);
    void showLast(double seconds);
    void exportPng();
    void testConnection();
    void showDetectors();
    void showLocations();
    void showCharts();
    void loadChartData();
    void createChart();
    void deleteChart();
    void showLicenses();
    void createLicense();
    void deleteLicense();
    void saveUrl();

private:
    void buildUi();
    void replotMain();
    void replotChartData();       // re-plot isotope trend lines with current bin/MDA settings
    void buildDailyPlot();
    void updateNavRect();
    void setSelectRange(double a, double b);

    // HTTP client helper (port 22438, or https://api.h3drms.com)
    // Returns the response body, or empty on failure.
    std::string httpGet(const std::string &path, int timeoutSec = 10);
    std::string httpPut(const std::string &path, const std::string &body, int timeoutSec = 10);

    // data
    TelemetryData m_raw;
    int m_binSec = 300;          // current display bin (seconds)
    int m_smoothMin = 0;         // moving-average window (minutes, 0 = off)

    // parsed chart data (isotope trend lines)
    QVector<ChartPoint> m_chartPoints;

    // base URL for the server HTTP API (e.g. https://rms.h3dgamma.com/api or http://localhost:22438/api)
    std::string m_baseUrl = "https://rms.h3dgamma.com/api";

    // widgets
    QCustomPlot *m_plot = nullptr;      // main telemetry plot
    QCustomPlot *m_navigator = nullptr; // long-range overview + range selector
    QCustomPlot *m_daily = nullptr;     // daily rollup plot
    QComboBox   *m_binCombo = nullptr;
    QSlider     *m_smoothSlider = nullptr;
    QLabel      *m_smoothLabel = nullptr;
    QCheckBox   *m_cbRequests = nullptr;
    QCheckBox   *m_cbErrors = nullptr;
    QCheckBox   *m_cbGpu = nullptr;
    QCheckBox   *m_cbCpu = nullptr;
    QCheckBox   *m_cbIncludeMda = nullptr;   // Include MDA in chart values
    QPushButton *m_testBtn = nullptr;
    QPushButton *m_showDetectorsBtn = nullptr;
    QPushButton *m_showLocationsBtn = nullptr;
    QPushButton *m_showChartsBtn = nullptr;
    QPushButton *m_loadChartDataBtn = nullptr;
    QPushButton *m_createChartBtn = nullptr;
    QPushButton *m_deleteChartBtn = nullptr;
    QPushButton *m_showLicensesBtn = nullptr;
    QPushButton *m_createLicenseBtn = nullptr;
    QPushButton *m_deleteLicenseBtn = nullptr;
    QLineEdit   *m_urlEdit = nullptr;
    QPushButton *m_saveUrlBtn = nullptr;

    // navigator selection items
    QCPItemRect  *m_navRect = nullptr;
    QCPItemLine  *m_navEdgeL = nullptr;
    QCPItemLine  *m_navEdgeR = nullptr;
    enum NavMode { NavNone, NavMove, NavLeft, NavRight, NavNew };
    NavMode m_navMode = NavNone;
    double  m_navAnchor = 0.0;
    double  m_navGrabOffset = 0.0;
    bool    m_updatingNav = false;
};
