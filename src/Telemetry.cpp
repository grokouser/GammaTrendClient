#include "Telemetry.h"
#include <QRandomGenerator>
#include <QtMath>

TelemetryData generateTelemetry(double startSec, int nPoints, double intervalSec, quint64 seed)
{
    TelemetryData d;
    d.startSec = startSec;
    d.intervalSec = intervalSec;
    d.key.reserve(nPoints);
    d.requests.reserve(nPoints);
    d.errors.reserve(nPoints);
    d.gpuTemp.reserve(nPoints);
    d.cpuTemp.reserve(nPoints);

    QRandomGenerator rng(seed);
    const double daySec = 86400.0;
    const double yearSec = 365.25 * daySec;

    for (int i = 0; i < nPoints; ++i) {
        const double t = startSec + i * intervalSec;
        d.key.append(t);

        const double dayPhase  = std::fmod(t, daySec) / daySec;          // 0..1
        const double yearPhase = std::fmod(t, yearSec) / yearSec;        // 0..1
        const double weekday = std::fmod(std::floor(t / daySec), 7.0);
        const double weekendFactor = (weekday >= 5.0) ? 0.45 : 1.0;

        // diurnal workload: quiet at ~04:00, peaks ~14:00
        const double diurnal = 0.5 - 0.5 * std::cos(2.0 * M_PI * (dayPhase - 0.08));
        // slow seasonal demand drift
        const double seasonal = 1.0 + 0.25 * std::sin(2.0 * M_PI * yearPhase);

        const double base = (1500.0 + 9500.0 * diurnal) * weekendFactor * seasonal;
        const double noise = rng.bounded(300);
        double req = qMax(20.0, base + noise);

        // rare incident bursts: ~0.3% of minutes get an error storm
        double err = rng.bounded(4);
        if (rng.generateDouble() < 0.003)
            err += 200 + rng.bounded(700);
        // errors loosely track traffic too
        err += req * 0.002;

        // temps: diurnal lag behind workload + yearly ambient + noise; incidents spike GPU
        const double ambient = 18.0 + 6.0 * std::sin(2.0 * M_PI * (yearPhase - 0.22));
        const double loadN   = diurnal * weekendFactor;
        double gpu = ambient + 14.0 + 28.0 * loadN + rng.bounded(25) / 10.0;
        double cpu = ambient + 8.0  + 16.0 * loadN + rng.bounded(20) / 10.0;
        if (err > 500.0) gpu += 12.0;

        d.requests.append(req);
        d.errors.append(err);
        d.gpuTemp.append(gpu);
        d.cpuTemp.append(cpu);
    }
    d.endSec = startSec + (nPoints - 1) * intervalSec;
    return d;
}
