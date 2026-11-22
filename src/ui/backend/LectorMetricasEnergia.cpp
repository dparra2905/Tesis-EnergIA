// LectorMetricasEnergia.cpp - ver LectorMetricasEnergia.h.
#include "LectorMetricasEnergia.h"

#include <QDir>
#include <QFile>

void LectorMetricasEnergia::iniciar(const QString& dirCorrida)
{
    reiniciar();
    dir_ = dirCorrida;
}

void LectorMetricasEnergia::reiniciar()
{
    dir_.clear();
    posicion_ = 0;
    columnas_.clear();
}

QVariantList LectorMetricasEnergia::actualizar()
{
    QVariantList nuevas;
    if (dir_.isEmpty()) return nuevas;
    QFile f(QDir(dir_).filePath(ARCHIVO_CSV));
    if (!f.open(QIODevice::ReadOnly) || !f.seek(posicion_)) return nuevas;
    const QByteArray bloque = f.readAll();
    const qsizetype ultimoSalto = bloque.lastIndexOf('\n');
    if (ultimoSalto < 0) return nuevas;  // solo lineas completas
    posicion_ += ultimoSalto + 1;

    for (const QByteArray& crudo : bloque.left(ultimoSalto).split('\n')) {
        const QString linea = QString::fromUtf8(crudo).trimmed();
        if (linea.isEmpty()) continue;
        const QStringList campos = linea.split(',');
        if (columnas_.isEmpty()) { columnas_ = campos; continue; }
        QVariantMap fila;
        for (int i = 0; i < columnas_.size() && i < campos.size(); ++i) fila[columnas_[i]] = campos[i];
        nuevas.append(derivar(fila));
    }
    return nuevas;
}

QVariantMap LectorMetricasEnergia::derivar(const QVariantMap& fila)
{
    QVariantMap r;
    r["timestamp"] = fila.value("timestamp");
    r["conectado"] = fila.value("conectado").toString() == "1";
    bool okP = false, okV = false, okI = false;
    const double p = fila.value("power_w").toString().toDouble(&okP);
    const double v = fila.value("voltage_v").toString().toDouble(&okV);
    const double i = fila.value("current_a").toString().toDouble(&okI);
    if (okP && okV && okI) {  // fila con trama vigente
        r["timestamp_trama"] = fila.value("timestamp_trama");
        r["potencia_w"] = p;
        r["voltaje_v"] = v;
        r["corriente_a"] = i;
    }
    return r;
}
