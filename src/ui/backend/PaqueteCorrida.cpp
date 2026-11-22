// PaqueteCorrida.cpp - ver PaqueteCorrida.h.
#include "PaqueteCorrida.h"

#include "Auditoria.h"
#include "Historial.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace PaqueteCorrida {

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

// Marca temporal de la base (p. ej. "2026-10-02T13:35:34+00:00") al formato de los medidores
// ("2026-10-02T13:35:34.000Z"), para que el CSV exportado desde la base sea igual al local
QString isoMedidor(const QString& texto)
{
    const QDateTime t = QDateTime::fromString(texto, Qt::ISODateWithMs);
    return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : texto;
}

QJsonValue numeroONulo(const QVariant& v)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    return (v.isValid() && !v.isNull() && ok) ? QJsonValue(d) : QJsonValue(QJsonValue::Null);
}

// Texto de un hiperparametro tal como esta en metadata.json (la tabla guarda texto)
QString textoValor(const QJsonValue& v)
{
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)) && qAbs(d) < 1e15) return QString::number(static_cast<qint64>(d));
        return QString::number(d, 'g', 17);
    }
    if (v.isNull() || v.isUndefined()) return {};
    const QJsonDocument d = v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject());
    return QString::fromUtf8(d.toJson(QJsonDocument::Compact));
}

// progreso.csv / progreso_inferencia.csv: timestamp_inicio, timestamp_fin, step_index, step_total,
// metrica_k_nombre/valor (k = 1..4). Cada paso y metrica lleva su tarea (migracion 008, D-014).
void leerProgreso(const QString& ruta, const QString& tarea, QJsonArray* pasos, QJsonArray* metricas)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QStringList columnas = in.readLine().trimmed().split(',');
    auto col = [&columnas](const QStringList& fila, const QString& nombre) {
        const int c = columnas.indexOf(nombre);
        return (c >= 0 && c < fila.size()) ? fila[c] : QString();
    };
    while (!in.atEnd()) {
        const QString linea = in.readLine().trimmed();
        if (linea.isEmpty()) continue;
        const QStringList fila = linea.split(',');
        bool ok = false;
        const int indice = col(fila, "step_index").toInt(&ok);
        if (!ok) continue;
        QJsonObject p{{"tarea", tarea}, {"step_index", indice}};
        bool okTotal = false;
        const int total = col(fila, "step_total").toInt(&okTotal);
        p["step_total"] = okTotal ? QJsonValue(total) : QJsonValue(QJsonValue::Null);
        const QString ini = col(fila, "timestamp_inicio"), fin = col(fila, "timestamp_fin");
        p["t_inicio"] = ini.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(ini);
        p["t_fin"] = fin.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(fin);
        pasos->append(p);
        for (int k = 1; k <= 4; ++k) {
            const QString nombre = col(fila, QString("metrica_%1_nombre").arg(k));
            if (nombre.isEmpty()) continue;
            bool okValor = false;
            const double valor = col(fila, QString("metrica_%1_valor").arg(k)).toDouble(&okValor);
            metricas->append(QJsonObject{{"tarea", tarea}, {"step_index", indice}, {"nombre", nombre},
                                         {"valor", okValor ? QJsonValue(valor) : QJsonValue(QJsonValue::Null)}});
        }
    }
}

} // namespace

QJsonObject hardwareLocal(const QString& equipo, const QString& dirCorrida)
{
    const QJsonObject info = leerJson(QDir(dirCorrida).filePath("sistema_info.json"));
    QJsonObject h{{"nombre", equipo}, {"sistema_operativo", QSysInfo::prettyProductName()},
                  {"arquitectura", QSysInfo::currentCpuArchitecture()}};
    h["nucleos_logicos"] = info["nucleos_logicos"].isDouble() ? info["nucleos_logicos"] : QJsonValue(QThread::idealThreadCount());
    QJsonValue ram = info["ram_total_bytes"].isDouble() ? info["ram_total_bytes"] : QJsonValue(QJsonValue::Null);
    QJsonValue cpu = QJsonValue::Null;
#ifdef Q_OS_WIN
    const QSettings reg("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
    const QString nombreCpu = reg.value("ProcessorNameString").toString().trimmed();
    if (!nombreCpu.isEmpty()) cpu = nombreCpu;
    if (ram.isNull()) {
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof(m);
        if (GlobalMemoryStatusEx(&m)) ram = static_cast<qint64>(m.ullTotalPhys);
    }
#endif
    h["cpu"] = cpu;
    h["ram_total_bytes"] = ram;
    return h;
}

QJsonObject datosMetadata(const QString& dirCorrida)
{
    return leerJson(QDir(dirCorrida).filePath("metadata.json"))["datos"].toObject();
}

QString rutaDataset(const QString& dirCorrida)
{
    const QJsonObject datos = leerJson(QDir(dirCorrida).filePath("metadata.json"))["datos"].toObject();
    return datos["origen"].toString() == "csv" ? datos["ruta"].toString() : QString();
}

QString huellaArchivo(const QString& ruta, QString* error)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = "No se pudo leer " + QDir::toNativeSeparators(ruta) + ": " + f.errorString();
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        if (error) *error = "No se pudo leer " + QDir::toNativeSeparators(ruta) + ": " + f.errorString();
        return {};
    }
    return QString::fromLatin1(h.result().toHex());
}

QJsonObject construir(const QString& dirCorrida, const QString& equipo, const QString& sha256Dataset, QString* error)
{
    error->clear();
    const QDir dir(dirCorrida);
    const QJsonObject m = leerJson(dir.filePath("metadata.json"));
    if (m.isEmpty()) {
        *error = "La carpeta no contiene una corrida (falta metadata.json)";
        return {};
    }
    const QString estado = m["estado"].toString();
    if (estado != "completada" && estado != "cancelada" && estado != "error") {
        *error = QString("La corrida no terminó (estado '%1')").arg(estado);
        return {};
    }
    const QJsonObject modelo = m["modelo"].toObject();
    const QJsonObject tiempos = m["tiempos"].toObject();
    if (!tiempos["t_creacion"].isString()) {
        *error = "metadata.json no tiene tiempos.t_creacion (identifica la corrida en la base)";
        return {};
    }
    const QJsonObject datos = m["datos"].toObject();
    const QJsonObject res = leerJson(dir.filePath("resultados.json"));
    const QJsonObject ventanas = res["energia_experimento"].toObject()["ventanas"].toObject();
    const QJsonObject ventEnt = ventanas["entrenamiento"].toObject();
    const QJsonObject ventMed = ventanas["medicion"].toObject();

    QJsonObject p;
    // Nombre original del experimento (con tildes). En las corridas anteriores a
    // experimento_carpeta, metadata.experimento ya era el nombre seguro y se sube igual que antes,
    // asi que su identidad en la base (experimento + equipo + t_creacion) no cambia.
    p["experimento"] = QJsonObject{{"nombre", m["experimento"].toString()}, {"descripcion", QJsonValue::Null}};

    if (!sha256Dataset.isEmpty()) {
        const qint64 nCols = datos["n_cols"].toInteger(-1);
        p["dataset"] = QJsonObject{
            {"sha256", sha256Dataset},
            {"nombre", QFileInfo(datos["ruta"].toString()).fileName()},
            {"filas", datos["n_filas_total"]},
            // n_cols son las columnas de atributos; el archivo tiene ademas la columna objetivo
            {"columnas", nCols >= 0 ? QJsonValue(nCols + 1) : QJsonValue(QJsonValue::Null)},
            {"columna_objetivo", datos["columna_objetivo"]},
            {"n_clases", datos["n_clases"]},
        };
    }

    // Monitor fisico: ¿estaba listo en t_start? (null si no participo)
    QJsonValue listo = QJsonValue::Null;
    for (const QJsonValue& v : m["medidores"].toArray())
        if (v.toObject()["nombre"].toString() == "energia") listo = v.toObject()["listo_en_t_start"];

    QJsonObject c;
    c["equipo"] = equipo;
    c["numero"] = m["corrida"].toInt();
    // Identidad de la corrida en la base (con experimento y equipo): el numero se puede repetir
    c["t_creacion"] = tiempos["t_creacion"];
    c["visibilidad"] = "privada";
    c["estado"] = estado;
    c["status_code"] = m["entrenamiento"].toObject()["status_dll"];
    c["modelo_dll"] = modelo["dll"].toString(modelo["clave"].toString());
    c["contrato_version"] = modelo["version_contrato"];
    c["pct_entrenamiento"] = datos["porcentaje_train"].isDouble()
        ? QJsonValue(datos["porcentaje_train"].toDouble() / 100.0) : QJsonValue(QJsonValue::Null);
    c["semilla"] = datos["semilla"];
    for (const char* t : {"t_start", "t_antes", "t_despues", "t_stop"})
        c[t] = tiempos[t].isString() ? tiempos[t] : QJsonValue(QJsonValue::Null);
    c["listo_en_t_start"] = listo;
    c["accuracy_val"] = m["evaluacion"].toObject()["metricas"].toObject()["val_accuracy"];
    c["duracion_entrenamiento_s"] = tiempos["duracion_entrenamiento_ms"].isDouble()
        ? QJsonValue(tiempos["duracion_entrenamiento_ms"].toDouble() / 1000.0) : QJsonValue(QJsonValue::Null);

    // Resumen de consumo (SUGERIDO en el esquema; definicion en D-011):
    //   energia_entrenamiento_wh / energia_total_wh: energia del experimento (D-008) en las
    //     ventanas 'entrenamiento' (t_antes..t_despues) y 'medicion' (t_start..t_stop);
    //   energia_base_pre_wh / energia_base_post_wh: energia en bruto de las ventanas base_pre y
    //     base_post de resultados.json (null si la ventana no existe: corrida cancelada antes,
    //     abortada, o sin fase posterior); no se aplica correccion por linea base (D-008);
    //   potencia_media_w: la de la ventana de entrenamiento de resultados.json;
    //   potencia_max_w y cpu_media_pct: maximo de potencia_w y media de cpu_proceso_pct en el
    //     entrenamiento, con el mismo criterio que la pagina de Resultados.
    c["energia_entrenamiento_wh"] = ventEnt["energia_wh"].isDouble() ? ventEnt["energia_wh"] : QJsonValue(QJsonValue::Null);
    c["energia_total_wh"] = ventMed["energia_wh"].isDouble() ? ventMed["energia_wh"] : QJsonValue(QJsonValue::Null);
    const QJsonObject ventPre = ventanas["base_pre"].toObject(), ventPost = ventanas["base_post"].toObject();
    c["energia_base_pre_wh"] = ventPre["energia_wh"].isDouble() ? ventPre["energia_wh"] : QJsonValue(QJsonValue::Null);
    c["energia_base_post_wh"] = ventPost["energia_wh"].isDouble() ? ventPost["energia_wh"] : QJsonValue(QJsonValue::Null);
    c["potencia_media_w"] = ventEnt["potencia_media_w"].isDouble() ? ventEnt["potencia_media_w"] : QJsonValue(QJsonValue::Null);
    const qint64 tStart = ms(tiempos["t_start"]), tAntes = ms(tiempos["t_antes"]), tDespues = ms(tiempos["t_despues"]);
    c["potencia_max_w"] = QJsonValue::Null;
    c["cpu_media_pct"] = QJsonValue::Null;
    if (tAntes > 0 && tDespues > tAntes) {
        const QVariantMap ene = analisis::resumenEnergia(dirCorrida, tAntes, tDespues);
        const QVariantMap sis = analisis::resumenSistema(dirCorrida, tAntes, tDespues);
        c["potencia_max_w"] = numeroONulo(ene.value("potencia_w").toMap().value("max"));
        c["cpu_media_pct"] = numeroONulo(sis.value("cpu_proceso_pct").toMap().value("media"));
    }
    c["metadata"] = m;
    // Copia de resultados.json (cobertura, metodo y version del calculo de energia, D-008)
    c["resultados"] = res.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(res);
    // Auditoria (D-012): auditoria.json integro y su veredicto; nota de condiciones (nota.txt).
    // null si la corrida no tiene auditoria o nota.
    const QJsonObject aud = leerJson(dir.filePath(auditoria::ARCHIVO));
    c["auditoria"] = aud.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(aud);
    c["veredicto"] = aud["veredicto"].isString() ? aud["veredicto"] : QJsonValue(QJsonValue::Null);
    const QString nota = Historial::leerNota(dirCorrida);
    c["nota"] = nota.trimmed().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(nota);
    // Inferencia (D-014, migracion 008). tiempo_por_prediccion_us sale de metadata.inferencia (se calcula
    // una sola vez, en el orquestador); energia_por_prediccion_j y las energias, de resultados.json
    // (null sin el monitor fisico de energia). Una corrida anterior a la inferencia se sube con
    // inferencia_activada = false.
    const QJsonObject inf = m["inferencia"].toObject();
    const bool conInferencia = inf["activada"].toBool(false);
    auto deInferencia = [&](const char* clave) { return conInferencia && !inf[clave].isUndefined() ? inf[clave] : QJsonValue(QJsonValue::Null); };
    c["inferencia_activada"] = conInferencia;
    c["inferencia_lote"] = deInferencia("lote");
    c["inferencia_duracion_min_s"] = deInferencia("duracion_min_s");
    for (const char* t : {"t_inf_antes", "t_inf_despues"})
        c[t] = conInferencia && tiempos[t].isString() ? tiempos[t] : QJsonValue(QJsonValue::Null);
    c["inferencia_pasadas"] = deInferencia("pasadas");
    c["n_predicciones"] = deInferencia("n_predicciones");
    c["tiempo_por_prediccion_us"] = deInferencia("tiempo_por_prediccion_us");
    const QJsonObject ventInf = ventanas["inferencia"].toObject(), ventMedia = ventanas["base_media"].toObject();
    c["energia_inferencia_wh"] = ventInf["energia_wh"].isDouble() ? ventInf["energia_wh"] : QJsonValue(QJsonValue::Null);
    c["energia_base_media_wh"] = ventMedia["energia_wh"].isDouble() ? ventMedia["energia_wh"] : QJsonValue(QJsonValue::Null);
    c["energia_por_prediccion_j"] =
        ventInf["energia_por_prediccion_j"].isDouble() ? ventInf["energia_por_prediccion_j"] : QJsonValue(QJsonValue::Null);
    // Metricas del modelo (D-015, migracion 009): copia integra de metricas_modelo.json y su resumen en
    // columnas. null si la corrida no tiene el archivo, y el conjunto en que predecir fallo queda en
    // null (la copia lleva el motivo). La exactitud de validacion sigue en accuracy_val (evaluar).
    const QJsonObject mm = leerJson(dir.filePath("metricas_modelo.json"));
    const QJsonObject mVal = mm["validacion"].toObject(), mEnt = mm["entrenamiento"].toObject();
    auto numero = [](const QJsonValue& v) { return v.isDouble() ? v : QJsonValue(QJsonValue::Null); };
    c["metricas_modelo"] = mm.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(mm);
    c["f1_macro_val"] = numero(mVal["macro"].toObject()["f1"]);
    c["f1_ponderado_val"] = numero(mVal["ponderado"].toObject()["f1"]);
    c["precision_macro_val"] = numero(mVal["macro"].toObject()["precision"]);
    c["recall_macro_val"] = numero(mVal["macro"].toObject()["recall"]);
    c["accuracy_entrenamiento"] = numero(mEnt["exactitud"]);
    c["f1_macro_entrenamiento"] = numero(mEnt["macro"].toObject()["f1"]);
    p["corrida"] = c;
    p["hardware"] = hardwareLocal(equipo, dirCorrida);

    QJsonArray hiper;
    const QJsonObject h = m["hiperparametros"].toObject();
    for (auto it = h.begin(); it != h.end(); ++it) hiper.append(QJsonObject{{"nombre", it.key()}, {"valor", textoValor(it.value())}});
    p["hiperparametros"] = hiper;

    QJsonArray pasos, metricas;
    leerProgreso(dir.filePath("progreso.csv"), "entrenamiento", &pasos, &metricas);
    leerProgreso(dir.filePath("progreso_inferencia.csv"), "inferencia", &pasos, &metricas);
    p["pasos"] = pasos;
    p["metricas"] = metricas;

    // Serie unida: vacia si la corrida no llego a medir (p. ej. error antes de t_start)
    QList<analisis::FilaRejilla> filas;
    QString sinSerie;
    if (tStart > 0) analisis::rejilla(dirCorrida, fases::desdeMetadata(m), &filas, &sinSerie);
    p["serie"] = serieJson(filas);
    return p;
}

QJsonArray serieJson(const QList<analisis::FilaRejilla>& filas)
{
    QJsonArray serie;
    for (const analisis::FilaRejilla& f : filas) {
        QJsonObject o{{"t", f.timestamp}, {"t_rel_s", f.tRelS}, {"en_entrenamiento", f.enEntrenamiento}};
        o["fase"] = f.fase.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(f.fase);
        o["paso_desde"] = f.pasoDesde >= 0 ? QJsonValue(f.pasoDesde) : QJsonValue(QJsonValue::Null);
        o["paso_hasta"] = f.pasoHasta >= 0 ? QJsonValue(f.pasoHasta) : QJsonValue(QJsonValue::Null);
        for (const auto& col : analisis::COLUMNAS_REJILLA) {
            const auto v = f.valores.constFind(QString::fromLatin1(col.nombre));
            o[col.nombre] = v != f.valores.constEnd() ? QJsonValue(v.value()) : QJsonValue(QJsonValue::Null);
        }
        o["timestamp_trama"] = f.timestampTrama.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(f.timestampTrama);
        serie.append(o);
    }
    return serie;
}

QList<analisis::FilaRejilla> filasDesdeJson(const QJsonArray& serie)
{
    QList<analisis::FilaRejilla> filas;
    for (const QJsonValue& v : serie) {
        const QJsonObject o = v.toObject();
        analisis::FilaRejilla f;
        f.timestamp = isoMedidor(o["t"].toString());
        f.ms = QDateTime::fromString(f.timestamp, Qt::ISODateWithMs).toMSecsSinceEpoch();
        f.tRelS = o["t_rel_s"].toDouble();
        f.enEntrenamiento = o["en_entrenamiento"].toBool();
        // Corridas subidas antes de la migracion 005: sin fase ni pasos (null)
        f.fase = o["fase"].toString();
        f.pasoDesde = o["paso_desde"].isDouble() ? o["paso_desde"].toInt() : -1;
        f.pasoHasta = o["paso_hasta"].isDouble() ? o["paso_hasta"].toInt() : -1;
        for (const auto& col : analisis::COLUMNAS_REJILLA)
            if (o[col.nombre].isDouble()) f.valores[col.nombre] = o[col.nombre].toDouble();
        if (o["timestamp_trama"].isString()) f.timestampTrama = isoMedidor(o["timestamp_trama"].toString());
        filas.append(f);
    }
    return filas;
}

} // namespace PaqueteCorrida
