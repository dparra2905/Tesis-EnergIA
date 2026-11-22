// Reloj.cpp - ver Reloj.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "Reloj.h"

#include <chrono>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace reloj {

qint64 ahoraMs()
{
#ifdef Q_OS_WIN
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<qint64>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
#endif
}

// Milisegundos desde epoch -> "2026-09-22T00:52:45.334Z" (UTC)
QByteArray isoUtc(qint64 ms)
{
    qint64 dias = ms / 86400000;
    qint64 resto = ms % 86400000;
    if (resto < 0) { resto += 86400000; --dias; }

    // Conversion de dias desde 1970-01-01 a fecha civil (algoritmo de H. Hinnant)
    const qint64 z = dias + 719468;
    const qint64 era = (z >= 0 ? z : z - 146096) / 146097;
    const qint64 doe = z - era * 146097;
    const qint64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const qint64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const qint64 mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));

    const int h = static_cast<int>(resto / 3600000);
    const int mi = static_cast<int>((resto / 60000) % 60);
    const int s = static_cast<int>((resto / 1000) % 60);
    const int mil = static_cast<int>(resto % 1000);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, m, d, h, mi, s, mil);
    return QByteArray(buf);
}

int msHasta(qint64 objetivoMs)
{
    const qint64 ms = objetivoMs - ahoraMs();
    return ms > 0 ? static_cast<int>(ms) : 0;
}

} // namespace reloj
