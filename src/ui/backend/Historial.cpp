// Historial.cpp - ver Historial.h.
#include "Historial.h"

#include "Auditoria.h"
#include "ExportacionModelo.h"
#include "ResumenCorrida.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

QJsonObject leerJson(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

qint64 ms(const QJsonValue& v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

} // namespace

Historial::Historial(const QString& dirSalida, QObject* parent) : QObject(parent), dirSalida_(dirSalida) {}

Historial::~Historial()
{
    if (hiloExportacion_) hiloExportacion_->wait();
}

QVariantMap Historial::infoExportacion(const QString& dirCorrida) const
{
    return exportacion::info(dirCorrida).toVariantMap();
}

void Historial::exportarModelo(const QString& dirCorrida, const QString& destino)
{
    if (hiloExportacion_) {
        emit modeloExportado(dirCorrida, destino, "Ya hay una exportación en curso", {});
        return;
    }
    const QString huellas = rutaHuellas_;
    hiloExportacion_ = QThread::create([this, dirCorrida, destino, huellas] {
        QJsonObject modelo;
        QString error;
        exportacion::exportar(dirCorrida, destino, huellas, &modelo, &error);
        const QVariantList avisos = modelo["avisos"].toArray().toVariantList();
        QMetaObject::invokeMethod(this, [this, dirCorrida, destino, error, avisos] {
            hiloExportacion_->wait();
            delete hiloExportacion_;
            hiloExportacion_ = nullptr;
            emit exportacionModeloCambiada();
            emit modeloExportado(dirCorrida, destino, error, avisos);
        }, Qt::QueuedConnection);
    });
    hiloExportacion_->start();
    emit exportacionModeloCambiada();
}

QVariantMap Historial::leerResumen(const QString& dirCorrida)
{
    const QJsonObject m = leerJson(QDir(dirCorrida).filePath("metadata.json"));
    if (m.isEmpty()) return {};
    QVariantMap r = resumenDesdeJson(m, leerJson(QDir(dirCorrida).filePath("resultados.json")), QDir::toNativeSeparators(dirCorrida));
    // Auditoria (D-012) y nota de condiciones, si existen
    const QJsonObject a = leerJson(QDir(dirCorrida).filePath(auditoria::ARCHIVO));
    r["veredicto"] = a["veredicto"].toString();
    r["auditoria"] = a.toVariantMap();
    r["nota"] = leerNota(dirCorrida);
    return r;
}

QVariantMap Historial::resumenDesdeJson(const QJsonObject& m, const QJsonObject& res, const QString& dir)
{
    QVariantMap r;
    if (m.isEmpty()) return r;
    const QJsonObject modelo = m["modelo"].toObject();
    const QJsonObject tiempos = m["tiempos"].toObject();
    const QJsonObject datos = m["datos"].toObject();

    r["dir"] = dir;
    r["id"] = m["id_corrida"].toString();
    r["experimento"] = m["experimento"].toString();
    r["corrida"] = m["corrida"].toInt();
    r["estado"] = m["estado"].toString();
    r["error"] = m["error"].toObject()["mensaje"].toString();
    r["modelo_nombre"] = modelo["nombre"].toString();
    r["modelo_clave"] = modelo["clave"].toString();
    r["modelo_interno"] = modelo["interno"].toBool();
    r["unidad_paso"] = modelo["unidad_paso"].toString();
    // Hilos que uso la corrida (D2). Las corridas anteriores a hilos_efectivos se calculan con la
    // misma regla: el hiperparametro 'hilos' si es > 0; si no, los automaticos (modelo.hilos)
    const QJsonValue hilosHiper = m["hiperparametros"].toObject()["hilos"];
    r["hilos_efectivos"] = modelo["hilos_efectivos"].isDouble() ? modelo["hilos_efectivos"].toVariant()
                         : hilosHiper.isDouble() && hilosHiper.toInt() > 0 ? QVariant(hilosHiper.toInt())
                         : modelo["hilos"].isDouble() ? modelo["hilos"].toVariant() : QVariant();
    r["datos_origen"] = datos["origen"].toString();
    r["datos_ruta"] = datos["ruta"].toString();
    r["n_train"] = datos["n_train"].toVariant();
    r["n_val"] = datos["n_val"].toVariant();
    r["hiperparametros"] = m["hiperparametros"].toObject().toVariantMap();
    r["pasos_registrados"] = m["entrenamiento"].toObject()["pasos_registrados"].toInt();
    r["evaluacion"] = m["evaluacion"].toObject()["metricas"].toObject().toVariantMap();
    for (const char* t : {"t_creacion", "t_start", "t_antes", "t_despues", "t_stop", "t_fin", "t_inf_antes", "t_inf_despues"})
        r[t] = tiempos[t].toString();
    // Inferencia (D-014): metadata.inferencia ({activada: false} o sin la clave en las corridas sin
    // inferencia). tiempo_por_prediccion_us sale de aqui; la energia por prediccion, de resultados.json
    r["inferencia"] = m["inferencia"].isObject() ? m["inferencia"].toObject().toVariantMap() : QVariantMap{{"activada", false}};
    r["t_creacion_ms"] = ms(tiempos["t_creacion"]);
    r["duracion_entrenamiento_s"] = tiempos["duracion_entrenamiento_ms"].isDouble()
        ? QVariant(tiempos["duracion_entrenamiento_ms"].toDouble() / 1000.0) : QVariant();
    QStringList medidores;
    for (const QJsonValue& v : m["medidores"].toArray()) medidores << v.toObject()["nombre"].toString();
    r["medidores"] = medidores;

    // Energia del experimento (dato derivado, D-008), si se calculo
    const QJsonObject ventanas = res["energia_experimento"].toObject()["ventanas"].toObject();
    if (!ventanas.isEmpty()) {
        r["energia_experimento"] = ventanas.toVariantMap();
        r["metodo_energia"] = res["energia_experimento"].toObject()["metodo"].toString();
        r["version_metodo_energia"] = res["energia_experimento"].toObject()["version_metodo"].toInt();
    }
    return r;
}

void Historial::establecerDirSalida(const QString& dir)
{
    const QString normalizada = QDir::cleanPath(dir);
    if (normalizada == dirSalida_) return;
    dirSalida_ = normalizada;
    recargar();
}

void Historial::recargar()
{
    QVariantList lista;
    const QDir raiz(QDir(dirSalida_).filePath("corridas"));
    for (const QString& exp : raiz.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir dirExp(raiz.filePath(exp));
        for (const QString& c : dirExp.entryList({"corrida_*"}, QDir::Dirs)) {
            const QVariantMap r = leerResumen(dirExp.filePath(c));
            if (!r.isEmpty()) lista.append(r);
        }
    }
    std::sort(lista.begin(), lista.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("t_creacion_ms").toLongLong() > b.toMap().value("t_creacion_ms").toLongLong();
    });
    corridas_ = lista;
    emit cambiado();
}

QVariantMap Historial::detalle(const QString& dirCorrida) const
{
    QVariantMap r = leerResumen(dirCorrida);
    if (r.isEmpty()) return r;
    // Metricas del modelo (D-015): vacio si la corrida no tiene metricas_modelo.json (corrida vieja o
    // sin entrenamiento completo). Solo en el detalle: la lista no las necesita.
    r["metricas_modelo"] = leerJson(QDir(dirCorrida).filePath("metricas_modelo.json")).toVariantMap();
    const qint64 tStart = QDateTime::fromString(r["t_start"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
    const qint64 tAntes = QDateTime::fromString(r["t_antes"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
    const qint64 tDespues = QDateTime::fromString(r["t_despues"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
    const qint64 tStop = QDateTime::fromString(r["t_stop"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();

    QVariantMap ventanas;
    if (tAntes > 0 && tDespues > tAntes) {
        QVariantMap v;
        v["sistema"] = analisis::resumenSistema(dirCorrida, tAntes, tDespues);
        v["energia"] = analisis::resumenEnergia(dirCorrida, tAntes, tDespues);
        ventanas["entrenamiento"] = v;
    }
    if (tStart > 0 && tStop > tStart) {
        QVariantMap v;
        v["sistema"] = analisis::resumenSistema(dirCorrida, tStart, tStop);
        v["energia"] = analisis::resumenEnergia(dirCorrida, tStart, tStop);
        ventanas["medicion"] = v;
    }
    // Inferencia (D-014): estadisticas en [t_inf_antes, t_inf_despues], aparte del entrenamiento
    const qint64 tInfAntes = QDateTime::fromString(r["t_inf_antes"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
    const qint64 tInfDespues = QDateTime::fromString(r["t_inf_despues"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
    if (tInfAntes > 0 && tInfDespues > tInfAntes) {
        QVariantMap v;
        v["sistema"] = analisis::resumenSistema(dirCorrida, tInfAntes, tInfDespues);
        v["energia"] = analisis::resumenEnergia(dirCorrida, tInfAntes, tInfDespues);
        ventanas["inferencia"] = v;
    }
    r["ventanas"] = ventanas;
    if (tStart > 0) {
        r["series_sistema"] = analisis::seriesSistema(dirCorrida, tStart);
        r["series_energia"] = analisis::seriesEnergia(dirCorrida, tStart);
        r["t_antes_rel_s"] = tAntes > 0 ? QVariant((tAntes - tStart) / 1000.0) : QVariant();
        r["t_despues_rel_s"] = tDespues > 0 ? QVariant((tDespues - tStart) / 1000.0) : QVariant();
        r["t_inf_antes_rel_s"] = tInfAntes > 0 ? QVariant((tInfAntes - tStart) / 1000.0) : QVariant();
        r["t_inf_despues_rel_s"] = tInfDespues > 0 ? QVariant((tInfDespues - tStart) / 1000.0) : QVariant();
    }
    return r;
}

QString Historial::exportarCsv(const QString& dirCorrida, const QString& destino) const
{
    const QVariantMap r = leerResumen(dirCorrida);
    if (r.isEmpty()) return "La carpeta no contiene una corrida (falta metadata.json)";
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    if (!f.open(QIODevice::ReadOnly)) return "No se pudo leer metadata.json";
    const fases::Marcas marcas = fases::desdeMetadata(QJsonDocument::fromJson(f.readAll()).object());
    QString error;
    int filas = 0;
    if (!analisis::exportarRejilla(dirCorrida, marcas, destino, &filas, &error))
        return error;
    return {};
}

QString Historial::leerNota(const QString& dirCorrida)
{
    QFile f(QDir(dirCorrida).filePath(ARCHIVO_NOTA));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

QString Historial::nota(const QString& dirCorrida) const { return leerNota(dirCorrida); }

QString Historial::guardarNota(const QString& dirCorrida, const QString& texto)
{
    const QString ruta = QDir(dirCorrida).filePath(ARCHIVO_NOTA);
    if (!QFile::exists(QDir(dirCorrida).filePath("metadata.json"))) return "La carpeta no contiene una corrida (falta metadata.json)";
    if (texto.trimmed().isEmpty()) {
        if (QFile::exists(ruta) && !QFile::remove(ruta)) return "No se pudo borrar " + QDir::toNativeSeparators(ruta);
    } else {
        QSaveFile f(ruta);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(texto.toUtf8()) < 0 || !f.commit())
            return "No se pudo escribir " + QDir::toNativeSeparators(ruta) + ": " + f.errorString();
    }
    recargar();
    return {};
}

QString Historial::rutaLocal(const QUrl& url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
