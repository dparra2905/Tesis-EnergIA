// LectorMetricasSistema.cpp - ver LectorMetricasSistema.h.
#include "LectorMetricasSistema.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

bool numero(const QVariantMap& fila, const char* columna, double* valor)
{
    const QString texto = fila.value(columna).toString();
    if (texto.isEmpty()) return false;
    bool ok = false;
    *valor = texto.toDouble(&ok);
    return ok;
}

} // namespace

void LectorMetricasSistema::iniciar(const QString& dirCorrida)
{
    reiniciar();
    dir_ = dirCorrida;
}

void LectorMetricasSistema::reiniciar()
{
    dir_.clear();
    posicion_ = 0;
    columnas_.clear();
    ramTotal_ = 0;
}

void LectorMetricasSistema::leerInfo()
{
    QFile f(QDir(dir_).filePath(ARCHIVO_INFO));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject info = QJsonDocument::fromJson(f.readAll()).object();
    ramTotal_ = info["ram_total_bytes"].toInteger();
}

QVariantList LectorMetricasSistema::actualizar()
{
    QVariantList nuevas;
    if (dir_.isEmpty()) return nuevas;
    if (ramTotal_ <= 0) leerInfo();

    QFile f(QDir(dir_).filePath(ARCHIVO_CSV));
    if (!f.open(QIODevice::ReadOnly)) return nuevas;
    if (!f.seek(posicion_)) return nuevas;
    const QByteArray bloque = f.readAll();
    // Solo lineas completas: la ultima puede estar a medio escribir
    const qsizetype ultimoSalto = bloque.lastIndexOf('\n');
    if (ultimoSalto < 0) return nuevas;
    posicion_ += ultimoSalto + 1;

    for (const QByteArray& crudo : bloque.left(ultimoSalto).split('\n')) {
        const QString linea = QString::fromUtf8(crudo).trimmed();
        if (linea.isEmpty()) continue;
        const QStringList campos = linea.split(',');
        if (columnas_.isEmpty()) { columnas_ = campos; continue; }  // encabezado
        QVariantMap fila;
        for (int i = 0; i < columnas_.size() && i < campos.size(); ++i) fila[columnas_[i]] = campos[i];
        nuevas.append(derivar(fila, static_cast<double>(ramTotal_)));
    }
    return nuevas;
}

QVariantMap LectorMetricasSistema::derivar(const QVariantMap& fila, double ramTotalBytes)
{
    QVariantMap r;
    r["timestamp"] = fila.value("timestamp");
    double ocupado = 0, total = 0, orq = 0, intervalo = 0, ram = 0, ws = 0;
    const bool hayTotal = numero(fila, "cpu_sis_total_s", &total) && total > 0;
    if (hayTotal && numero(fila, "cpu_sis_ocupado_s", &ocupado)) r["cpu_sistema_pct"] = 100.0 * ocupado / total;
    if (numero(fila, "orq_cpu_s", &orq)) {
        if (hayTotal) r["cpu_proceso_pct"] = 100.0 * orq / total;
        if (numero(fila, "intervalo_ms", &intervalo) && intervalo > 0) r["cpu_proceso_nucleos"] = orq / (intervalo / 1000.0);
    }
    if (ramTotalBytes > 0 && numero(fila, "ram_sis_usada_bytes", &ram)) r["ram_sistema_pct"] = 100.0 * ram / ramTotalBytes;
    if (numero(fila, "orq_ws_bytes", &ws)) r["ram_proceso_mb"] = ws / (1024.0 * 1024.0);
    return r;
}
