// ResumenCorrida.cpp - ver ResumenCorrida.h.
#include "ResumenCorrida.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <limits>

namespace analisis {

namespace {

constexpr qint64 INTERVALO_MS = 1000;  // rejilla de 1 s (D-007)

struct Tabla {
    QStringList columnas;
    QList<QStringList> filas;
    QString valor(int fila, const char* columna) const
    {
        const int c = columnas.indexOf(QString::fromLatin1(columna));
        return (c >= 0 && c < filas[fila].size()) ? filas[fila][c] : QString();
    }
};

bool leerCsv(const QString& ruta, Tabla* t)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    t->columnas = in.readLine().trimmed().split(',');
    while (!in.atEnd()) {
        const QString linea = in.readLine().trimmed();
        if (!linea.isEmpty()) t->filas.append(linea.split(','));
    }
    return true;
}

qint64 ms(const QString& iso)
{
    const QDateTime t = QDateTime::fromString(iso, Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : -1;
}

bool numero(const QString& texto, double* v)
{
    if (texto.isEmpty()) return false;
    bool ok = false;
    *v = texto.toDouble(&ok);
    return ok;
}

qint64 ramTotal(const QString& dir)
{
    QFile f(QDir(dir).filePath("sistema_info.json"));
    if (!f.open(QIODevice::ReadOnly)) return 0;
    return QJsonDocument::fromJson(f.readAll()).object()["ram_total_bytes"].toInteger();
}

// Valores derivados de una fila de sistema_metrics.csv (ver ResumenCorrida.h)
QMap<QString, double> filaSistema(const Tabla& t, int i, double ramTotalBytes)
{
    QMap<QString, double> r;
    double total = 0, ocupado = 0, orq = 0, ram = 0, ws = 0;
    const bool hayTotal = numero(t.valor(i, "cpu_sis_total_s"), &total) && total > 0;
    if (hayTotal && numero(t.valor(i, "cpu_sis_ocupado_s"), &ocupado)) r["cpu_sistema_pct"] = 100.0 * ocupado / total;
    if (hayTotal && numero(t.valor(i, "orq_cpu_s"), &orq)) r["cpu_proceso_pct"] = 100.0 * orq / total;
    if (numero(t.valor(i, "orq_ws_bytes"), &ws)) r["ram_proceso_mb"] = ws / (1024.0 * 1024.0);
    if (ramTotalBytes > 0 && numero(t.valor(i, "ram_sis_usada_bytes"), &ram)) r["ram_sistema_pct"] = 100.0 * ram / ramTotalBytes;
    return r;
}

QMap<QString, double> filaEnergia(const Tabla& t, int i)
{
    QMap<QString, double> r;
    double p = 0, v = 0, a = 0;
    if (numero(t.valor(i, "power_w"), &p) && numero(t.valor(i, "voltage_v"), &v) && numero(t.valor(i, "current_a"), &a)) {
        r["potencia_w"] = p;
        r["voltaje_v"] = v;
        r["corriente_a"] = a;
    }
    return r;
}

struct Estadistica {
    int n = 0;
    double minimo = std::numeric_limits<double>::max();
    double maximo = std::numeric_limits<double>::lowest();
    double suma = 0;
    void agregar(double v) { ++n; minimo = std::min(minimo, v); maximo = std::max(maximo, v); suma += v; }
    QVariantMap aMapa() const
    {
        QVariantMap m;
        m["n"] = n;
        if (n > 0) { m["min"] = minimo; m["max"] = maximo; m["media"] = suma / n; }
        return m;
    }
};

QString texto(double v, int decimales) { return QString::number(v, 'f', decimales); }

} // namespace

QVariantMap resumenSistema(const QString& dirCorrida, qint64 desdeMs, qint64 hastaMs)
{
    Tabla t;
    QVariantMap r;
    if (!leerCsv(QDir(dirCorrida).filePath("sistema_metrics.csv"), &t)) return r;
    const double ram = static_cast<double>(ramTotal(dirCorrida));
    QMap<QString, Estadistica> est;
    for (int i = 0; i < t.filas.size(); ++i) {
        const qint64 fin = ms(t.valor(i, "timestamp"));
        if (fin < 0 || fin - INTERVALO_MS < desdeMs || fin > hastaMs) continue;  // intervalo completo en la ventana
        const auto valores = filaSistema(t, i, ram);
        for (auto it = valores.begin(); it != valores.end(); ++it) est[it.key()].agregar(it.value());
    }
    for (const char* s : {"cpu_proceso_pct", "cpu_sistema_pct", "ram_proceso_mb", "ram_sistema_pct"})
        r[s] = est.value(s).aMapa();
    return r;
}

QVariantMap resumenEnergia(const QString& dirCorrida, qint64 desdeMs, qint64 hastaMs)
{
    Tabla t;
    QVariantMap r;
    if (!leerCsv(QDir(dirCorrida).filePath("energia_atorch.csv"), &t)) return r;
    QMap<QString, Estadistica> est;
    int filas = 0, conTrama = 0;
    for (int i = 0; i < t.filas.size(); ++i) {
        const qint64 ts = ms(t.valor(i, "timestamp"));
        if (ts < desdeMs || ts > hastaMs) continue;
        ++filas;
        const auto valores = filaEnergia(t, i);
        if (valores.isEmpty()) continue;
        ++conTrama;
        for (auto it = valores.begin(); it != valores.end(); ++it) est[it.key()].agregar(it.value());
    }
    for (const char* s : {"potencia_w", "voltaje_v", "corriente_a"}) r[s] = est.value(s).aMapa();
    r["filas"] = filas;
    r["filas_con_trama"] = conTrama;
    return r;
}

QVariantMap seriesSistema(const QString& dirCorrida, qint64 tStartMs)
{
    Tabla t;
    QVariantMap r;
    if (!leerCsv(QDir(dirCorrida).filePath("sistema_metrics.csv"), &t)) return r;
    const double ram = static_cast<double>(ramTotal(dirCorrida));
    QVariantList ts;
    QMap<QString, QVariantList> series;
    const QStringList nombres = {"cpu_proceso_pct", "cpu_sistema_pct", "ram_proceso_mb", "ram_sistema_pct"};
    for (int i = 0; i < t.filas.size(); ++i) {
        const qint64 fin = ms(t.valor(i, "timestamp"));
        if (fin < 0) continue;
        ts.append(static_cast<double>(fin - tStartMs) / 1000.0);
        const auto valores = filaSistema(t, i, ram);
        for (const QString& n : nombres) series[n].append(valores.contains(n) ? QVariant(valores[n]) : QVariant());
    }
    r["t_s"] = ts;
    for (const QString& n : nombres) r[n] = series[n];
    return r;
}

QVariantMap seriesEnergia(const QString& dirCorrida, qint64 tStartMs)
{
    Tabla t;
    QVariantMap r;
    if (!leerCsv(QDir(dirCorrida).filePath("energia_atorch.csv"), &t)) return r;
    QVariantList ts;
    QMap<QString, QVariantList> series;
    const QStringList nombres = {"potencia_w", "voltaje_v", "corriente_a"};
    for (int i = 0; i < t.filas.size(); ++i) {
        const qint64 m = ms(t.valor(i, "timestamp"));
        if (m < 0) continue;
        ts.append(static_cast<double>(m - tStartMs) / 1000.0);
        const auto valores = filaEnergia(t, i);
        for (const QString& n : nombres) series[n].append(valores.contains(n) ? QVariant(valores[n]) : QVariant());
    }
    r["t_s"] = ts;
    for (const QString& n : nombres) r[n] = series[n];
    return r;
}

const QList<ColumnaRejilla> COLUMNAS_REJILLA = {
    {"cpu_proceso_pct", 3}, {"cpu_sistema_pct", 3}, {"ram_proceso_mb", 3}, {"ram_sistema_pct", 3},
    {"potencia_w", 1},      {"voltaje_v", 1},       {"corriente_a", 3},
};

// Pasos de progreso.csv: [timestamp_inicio, timestamp_fin] y step_index
struct Paso { qint64 inicioMs = 0, finMs = 0; int indice = 0; };

QList<Paso> leerPasos(const QString& dirCorrida, const char* archivo)
{
    Tabla t;
    QList<Paso> r;
    if (!leerCsv(QDir(dirCorrida).filePath(archivo), &t)) return r;
    for (int i = 0; i < t.filas.size(); ++i) {
        bool ok = false;
        Paso p;
        p.inicioMs = ms(t.valor(i, "timestamp_inicio"));
        p.finMs = ms(t.valor(i, "timestamp_fin"));
        p.indice = t.valor(i, "step_index").toInt(&ok);
        if (ok && p.inicioMs >= 0 && p.finMs >= p.inicioMs) r.append(p);
    }
    return r;
}

bool rejilla(const QString& dirCorrida, const fases::Marcas& marcas, QList<FilaRejilla>* filas, QString* error)
{
    const qint64 tStartMs = marcas.tStartMs, tAntesMs = marcas.tAntesMs, tDespuesMs = marcas.tDespuesMs;
    Tabla sis, ene;
    const bool haySis = leerCsv(QDir(dirCorrida).filePath("sistema_metrics.csv"), &sis);
    const bool hayEne = leerCsv(QDir(dirCorrida).filePath("energia_atorch.csv"), &ene);
    if (!haySis && !hayEne) {
        *error = "La corrida no tiene archivos de los medidores";
        return false;
    }
    const double ram = static_cast<double>(ramTotal(dirCorrida));

    // Union por timestamp de la rejilla (mismo formato en ambos medidores)
    QMap<qint64, FilaRejilla> union_;
    for (int i = 0; i < sis.filas.size(); ++i) {
        const qint64 m = ms(sis.valor(i, "timestamp"));
        if (m < 0) continue;
        FilaRejilla& f = union_[m];
        f.timestamp = sis.valor(i, "timestamp");
        const auto v = filaSistema(sis, i, ram);
        for (auto it = v.begin(); it != v.end(); ++it) f.valores[it.key()] = it.value();
    }
    for (int i = 0; i < ene.filas.size(); ++i) {
        const qint64 m = ms(ene.valor(i, "timestamp"));
        if (m < 0) continue;
        FilaRejilla& f = union_[m];
        f.timestamp = ene.valor(i, "timestamp");
        const auto v = filaEnergia(ene, i);
        for (auto it = v.begin(); it != v.end(); ++it) f.valores[it.key()] = it.value();
        if (!v.isEmpty()) f.timestampTrama = ene.valor(i, "timestamp_trama");
    }
    const QList<Paso> pasos = leerPasos(dirCorrida, "progreso.csv");
    // Inferencia (D-014): las filas posteriores a t_inf_antes usan los grupos de pasadas
    const QList<Paso> grupos = marcas.tInfAntesMs > 0 ? leerPasos(dirCorrida, "progreso_inferencia.csv") : QList<Paso>();
    filas->clear();
    for (auto it = union_.begin(); it != union_.end(); ++it) {
        FilaRejilla f = it.value();
        f.ms = it.key();
        f.tRelS = static_cast<double>(f.ms - tStartMs) / 1000.0;
        f.enEntrenamiento = tAntesMs > 0 && tDespuesMs > 0 && f.ms >= tAntesMs && f.ms <= tDespuesMs;
        f.fase = fases::faseFila(marcas, f.ms);
        const bool deInferencia = marcas.tInfAntesMs > 0 && f.ms > marcas.tInfAntesMs;
        for (const Paso& p : deInferencia ? grupos : pasos) {
            if (p.inicioMs > f.ms || p.finMs <= f.ms - INTERVALO_MS) continue;  // no toca (t - 1 s, t]
            f.pasoDesde = f.pasoDesde < 0 ? p.indice : std::min(f.pasoDesde, p.indice);
            f.pasoHasta = std::max(f.pasoHasta, p.indice);
        }
        filas->append(f);
    }
    return true;
}

bool escribirRejillaCsv(const QList<FilaRejilla>& filas, const QString& destino, QString* error)
{
    QSaveFile f(destino);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = "No se pudo escribir " + QDir::toNativeSeparators(destino) + ": " + f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "timestamp,t_rel_s,en_entrenamiento,fase,paso_desde,paso_hasta";
    for (const auto& c : COLUMNAS_REJILLA) out << ',' << c.nombre;
    out << ",timestamp_trama\n";
    for (const FilaRejilla& r : filas) {
        out << r.timestamp << ',' << texto(r.tRelS, 3) << ',' << (r.enEntrenamiento ? 1 : 0) << ',' << r.fase << ',';
        if (r.pasoDesde >= 0) out << r.pasoDesde;
        out << ',';
        if (r.pasoHasta >= 0) out << r.pasoHasta;
        for (const auto& c : COLUMNAS_REJILLA) {
            out << ',';
            const auto v = r.valores.constFind(QString::fromLatin1(c.nombre));
            if (v != r.valores.constEnd()) out << texto(v.value(), c.decimales);
        }
        out << ',' << r.timestampTrama << '\n';
    }
    out.flush();
    if (!f.commit()) {
        *error = "No se pudo escribir " + QDir::toNativeSeparators(destino) + ": " + f.errorString();
        return false;
    }
    return true;
}

QVariantMap resumenRejilla(const QList<FilaRejilla>& filas, qint64 desdeMs, qint64 hastaMs)
{
    static const QStringList SISTEMA = {"cpu_proceso_pct", "cpu_sistema_pct", "ram_proceso_mb", "ram_sistema_pct"};
    static const QStringList ENERGIA = {"potencia_w", "voltaje_v", "corriente_a"};
    QMap<QString, Estadistica> est;
    int filasEnergia = 0, conTrama = 0;
    for (const FilaRejilla& f : filas) {
        // Monitor de software: el intervalo de ~1 s que termina en f.ms debe caer completo
        if (f.ms - INTERVALO_MS >= desdeMs && f.ms <= hastaMs)
            for (const QString& s : SISTEMA)
                if (f.valores.contains(s)) est["s:" + s].agregar(f.valores.value(s));
        // Monitor fisico: el punto de la rejilla dentro de la ventana
        if (f.ms >= desdeMs && f.ms <= hastaMs) {
            ++filasEnergia;
            if (f.valores.contains("potencia_w")) ++conTrama;
            for (const QString& s : ENERGIA)
                if (f.valores.contains(s)) est["e:" + s].agregar(f.valores.value(s));
        }
    }
    QVariantMap sistema, energia;
    for (const QString& s : SISTEMA) sistema[s] = est.value("s:" + s).aMapa();
    for (const QString& s : ENERGIA) energia[s] = est.value("e:" + s).aMapa();
    energia["filas"] = filasEnergia;
    energia["filas_con_trama"] = conTrama;
    return {{"sistema", sistema}, {"energia", energia}};
}

QVariantMap seriesRejilla(const QList<FilaRejilla>& filas)
{
    static const QStringList SISTEMA = {"cpu_proceso_pct", "cpu_sistema_pct", "ram_proceso_mb", "ram_sistema_pct"};
    static const QStringList ENERGIA = {"potencia_w", "voltaje_v", "corriente_a"};
    auto familia = [&filas](const QStringList& nombres) {
        QVariantList ts;
        QMap<QString, QVariantList> series;
        for (const FilaRejilla& f : filas) {
            bool alguna = false;
            for (const QString& n : nombres) alguna = alguna || f.valores.contains(n);
            if (!alguna) continue;
            ts.append(f.tRelS);
            for (const QString& n : nombres) series[n].append(f.valores.contains(n) ? QVariant(f.valores.value(n)) : QVariant());
        }
        QVariantMap r{{"t_s", ts}};
        for (const QString& n : nombres) r[n] = series.value(n);
        return r;
    };
    return {{"series_sistema", familia(SISTEMA)}, {"series_energia", familia(ENERGIA)}};
}

bool exportarRejilla(const QString& dirCorrida, const fases::Marcas& marcas, const QString& destino, int* filas,
                     QString* error)
{
    QList<FilaRejilla> r;
    if (!rejilla(dirCorrida, marcas, &r, error)) return false;
    if (!escribirRejillaCsv(r, destino, error)) return false;
    if (filas) *filas = static_cast<int>(r.size());
    return true;
}

} // namespace analisis
