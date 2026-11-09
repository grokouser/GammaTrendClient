#pragma once
#include <QVector>

struct TelemetryData {
    QVector<double> key;            // seconds since epoch (QCP time axis convention)
    QVector<double> requests;       // requests per interval  (primary Y)
    QVector<double> errors;         // errors per interval    (primary Y)
    QVector<double> gpuTemp;        // deg C                  (secondary Y)
    QVector<double> cpuTemp;        // deg C                  (secondary Y)
    double startSec = 0.0;
    double endSec   = 0.0;
    double intervalSec = 60.0;
    int count() const { return key.size(); }
};

// Synthetic but realistic 24h-cycle + yearly-seasonality telemetry.
TelemetryData generateTelemetry(double startSec, int nPoints, double intervalSec, quint64 seed);
