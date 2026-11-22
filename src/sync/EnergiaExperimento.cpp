// EnergiaExperimento.cpp - ver EnergiaExperimento.h.
#include "EnergiaExperimento.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace energia {

namespace {

QString iso(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString("yyyy-MM-ddTHH:mm:ss.zzzZ");
}

qint64 leerIso(const QString& texto, bool* ok)
{
    const QDateTime t = QDateTime::fromString(texto.trimmed(), Qt::ISODateWithMs);
    *ok = t.isValid();
    return *ok ? t.toMSecsSinceEpoch() : 0;
}

// Potencia en el instante t dentro del segmento [a, b] (lineal entre las dos tramas)
double potenciaEn(const Trama& a, const Trama& b, qint64 t)
{
    if (b.tMs == a.tMs) return a.potenciaW;
    const double f = static_cast<double>(t - a.tMs) / static_cast<double>(b.tMs - a.tMs);
    return a.potenciaW + f * (b.potenciaW - a.potenciaW);
}

} // namespace

QList<Trama> tramasUnicas(const QList<Trama>& tramas, int* repetidas)
{
    QList<Trama> r;
    QSet<QString> vistas;
    int rep = 0;
    for (const Trama& t : tramas) {
        const QString clave = QString::number(t.tMs) + "|" + t.rawHex;
        if (vistas.contains(clave)) { ++rep; continue; }
        vistas.insert(clave);
        r.append(t);
    }
    std::stable_sort(r.begin(), r.end(), [](const Trama& a, const Trama& b) { return a.tMs < b.tMs; });
    if (repetidas) *repetidas = rep;
    return r;
}

ResultadoVentana integrar(const QList<Trama>& tramas, qint64 inicioMs, qint64 finMs, double umbralHuecoS)
{
    ResultadoVentana r;
    r.inicioMs = inicioMs;
    r.finMs = finMs;
    if (finMs <= inicioMs) return r;
    const qint64 umbralMs = static_cast<qint64>(umbralHuecoS * 1000.0);

    // Tramas que participan: las de la ventana y la ultima anterior / primera posterior, para
    // poder cubrir los bordes con el segmento que los atraviesa
    QList<Trama> uso;
    for (int i = 0; i < tramas.size(); ++i) {
        const bool dentro = tramas[i].tMs >= inicioMs && tramas[i].tMs <= finMs;
        const bool previa = i + 1 < tramas.size() && tramas[i].tMs < inicioMs && tramas[i + 1].tMs >= inicioMs;
        const bool posterior = i > 0 && tramas[i].tMs > finMs && tramas[i - 1].tMs <= finMs;
        if (dentro) ++r.tramasUsadas;
        if (dentro || previa || posterior) uso.append(tramas[i]);
    }
    // Se recorre la ventana de izquierda a derecha: cada tramo queda integrado o registrado
    // como hueco. 'cursor' es el instante hasta el que la ventana ya esta clasificada.
    qint64 cursor = inicioMs;
    double energia = 0.0;
    qint64 cubiertoMs = 0;
    for (int i = 0; i + 1 < uso.size(); ++i) {
        const Trama& a = uso[i];
        const Trama& b = uso[i + 1];
        const qint64 u = std::max(a.tMs, inicioMs);
        const qint64 v = std::min(b.tMs, finMs);
        if (v <= u) continue;
        if (u > cursor) r.huecos.append({cursor, u, "borde_inicial"});  // no hay trama antes del inicio
        if (b.tMs - a.tMs > umbralMs) {
            r.huecos.append({u, v, "entre_tramas"});  // no se integra ni se interpola
        } else {
            energia += (static_cast<double>(v - u) / 1000.0) * (potenciaEn(a, b, u) + potenciaEn(a, b, v)) / 2.0;
            cubiertoMs += v - u;
        }
        cursor = v;
    }
    if (cursor < finMs)
        r.huecos.append({cursor, finMs, cursor == inicioMs ? "sin_tramas" : "borde_final"});

    r.energiaJ = energia;
    r.segundosCubiertos = static_cast<double>(cubiertoMs) / 1000.0;
    r.cobertura = static_cast<double>(cubiertoMs) / static_cast<double>(finMs - inicioMs);
    return r;
}

bool leerTramasCsv(const QString& ruta, QList<Trama>* tramas, QString* error)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = "No se pudo abrir " + ruta + ": " + f.errorString();
        return false;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QStringList cab = in.readLine().trimmed().split(',');
    const int cT = cab.indexOf("timestamp_trama");
    const int cP = cab.indexOf("power_w");
    const int cE = cab.indexOf("energy_kwh");
    const int cR = cab.indexOf("raw_hex");
    if (cT < 0 || cP < 0) {
        *error = "Faltan las columnas timestamp_trama o power_w en " + ruta;
        return false;
    }
    tramas->clear();
    while (!in.atEnd()) {
        const QString linea = in.readLine().trimmed();
        if (linea.isEmpty()) continue;
        const QStringList c = linea.split(',');
        if (c.size() <= std::max(cT, cP)) continue;
        if (c[cT].isEmpty() || c[cP].isEmpty()) continue;  // fila de la rejilla sin trama vigente
        bool okT = false, okP = false;
        Trama t;
        t.tMs = leerIso(c[cT], &okT);
        t.potenciaW = c[cP].toDouble(&okP);
        if (!okT || !okP) continue;
        if (cE >= 0 && cE < c.size() && !c[cE].isEmpty()) t.contadorKwh = c[cE].toDouble();
        if (cR >= 0 && cR < c.size()) t.rawHex = c[cR];
        tramas->append(t);
    }
    return true;
}

QJsonObject aJson(const ResultadoVentana& r)
{
    QJsonObject o;
    o["inicio"] = iso(r.inicioMs);
    o["fin"] = iso(r.finMs);
    o["duracion_s"] = static_cast<double>(r.finMs - r.inicioMs) / 1000.0;
    o["energia_j"] = r.energiaJ;
    o["energia_wh"] = r.energiaJ / 3600.0;
    o["segundos_cubiertos"] = r.segundosCubiertos;
    o["cobertura"] = r.cobertura;
    o["potencia_media_w"] = r.segundosCubiertos > 0 ? QJsonValue(r.energiaJ / r.segundosCubiertos) : QJsonValue(QJsonValue::Null);
    o["tramas_en_ventana"] = r.tramasUsadas;
    QJsonArray h;
    for (const Hueco& x : r.huecos) {
        QJsonObject j;
        j["inicio"] = iso(x.inicioMs);
        j["fin"] = iso(x.finMs);
        j["duracion_s"] = static_cast<double>(x.finMs - x.inicioMs) / 1000.0;
        j["tipo"] = x.tipo;
        h.append(j);
    }
    o["huecos"] = h;
    return o;
}

bool resultadosCorrida(const QString& dirCorrida, const fases::Marcas& marcas, QJsonObject* resultados, QString* error,
                       const QJsonObject& inferencia)
{
    const qint64 tStartMs = marcas.tStartMs, tAntesMs = marcas.tAntesMs, tDespuesMs = marcas.tDespuesMs,
                 tStopMs = marcas.tStopMs;
    const QDir dir(dirCorrida);
    QString fuente = "energia_tramas.csv";
    if (!QFile::exists(dir.filePath(fuente))) fuente = "energia_atorch.csv";
    if (!QFile::exists(dir.filePath(fuente))) {
        *error = "No hay archivos del monitor fisico de energia en la corrida";
        return false;
    }
    QList<Trama> leidas;
    if (!leerTramasCsv(dir.filePath(fuente), &leidas, error)) return false;
    int repetidas = 0;
    const QList<Trama> tramas = tramasUnicas(leidas, &repetidas);

    QJsonObject ventanas;
    if (tAntesMs > 0 && tDespuesMs > tAntesMs) ventanas["entrenamiento"] = aJson(integrar(tramas, tAntesMs, tDespuesMs));
    if (tStartMs > 0 && tStopMs > tStartMs) ventanas["medicion"] = aJson(integrar(tramas, tStartMs, tStopMs));
    const fases::Intervalo pre = fases::basePre(marcas), post = fases::basePost(marcas);
    const fases::Intervalo media = fases::baseMedia(marcas), inf = fases::inferencia(marcas);
    if (pre.existe) ventanas[fases::BASE_PRE] = aJson(integrar(tramas, pre.desdeMs, pre.hastaMs));
    if (media.existe) ventanas[fases::BASE_MEDIA] = aJson(integrar(tramas, media.desdeMs, media.hastaMs));
    if (inf.existe) {
        // Por prediccion (D-014): solo si inferir devolvio ENERGIA_OK. El tiempo por prediccion se
        // calcula una vez, en el orquestador (metadata.inferencia); aqui se copia sin recalcular.
        QJsonObject v = aJson(integrar(tramas, inf.desdeMs, inf.hastaMs));
        const QJsonValue nPred = inferencia["n_predicciones"];
        const bool ok = inferencia["status_dll"].isDouble() && inferencia["status_dll"].toInt() == 0 && nPred.isDouble()
                        && nPred.toDouble() > 0;
        v["n_predicciones"] = nPred.isDouble() ? nPred : QJsonValue(QJsonValue::Null);
        v["energia_por_prediccion_j"] = ok ? QJsonValue(v["energia_j"].toDouble() / nPred.toDouble()) : QJsonValue(QJsonValue::Null);
        v["tiempo_por_prediccion_us"] = ok ? inferencia["tiempo_por_prediccion_us"] : QJsonValue(QJsonValue::Null);
        v["motivo_sin_por_prediccion"] = ok ? QJsonValue(QJsonValue::Null)
                                       : inferencia["motivo_sin_por_prediccion"].isString() ? inferencia["motivo_sin_por_prediccion"]
                                       : inferencia["status_dll"].toInt(-1) == 4 ? QJsonValue("inferencia cancelada")
                                                                                 : QJsonValue("error de inferir");
        ventanas[fases::INFERENCIA] = v;
    }
    if (post.existe) ventanas[fases::BASE_POST] = aJson(integrar(tramas, post.desdeMs, post.hastaMs));

    QJsonObject energia;
    energia["descripcion"] = "Energia del experimento: integracion de power_w en el tiempo (dato derivado, D-008)";
    energia["metodo"] = METODO;
    energia["version_metodo"] = VERSION_METODO;
    energia["umbral_hueco_s"] = UMBRAL_HUECO_S;
    energia["fuente"] = fuente;
    energia["tramas_leidas"] = static_cast<int>(leidas.size());
    energia["tramas_repetidas_descartadas"] = repetidas;
    energia["ventanas"] = ventanas;
    energia["validez"] = "sin evaluar: la politica de cobertura y la validacion con el Atorch real estan pendientes (D-008)";
    energia["linea_base"] = "medida (ventanas base_pre, base_media y base_post, energia en bruto); no se aplica ninguna "
                            "correccion: la correccion por linea base no esta decidida (D-008)";
    // Si la linea base posterior empezo: decide la ventana base_post. En las corridas anteriores
    // a la marca de metadata.json se deduce (FasesCorrida.h)
    energia["fase_posterior_iniciada"] = QJsonObject{{"valor", marcas.posteriorIniciada}, {"origen", marcas.origenPosterior}};

    QJsonObject r;
    r["version_resultados"] = VERSION_RESULTADOS;
    r["energia_experimento"] = energia;
    r["contador_medidor"] = contadorMedidor(tramas, tStartMs > 0 ? tStartMs : 0,
                                            tStopMs > 0 ? tStopMs : std::numeric_limits<qint64>::max());
    *resultados = r;
    return true;
}

QJsonObject contadorMedidor(const QList<Trama>& tramas, qint64 inicioMs, qint64 finMs)
{
    double inicial = -1, final = -1;
    for (const Trama& t : tramas) {
        if (t.tMs < inicioMs || t.tMs > finMs || t.contadorKwh < 0) continue;
        if (inicial < 0) inicial = t.contadorKwh;
        final = t.contadorKwh;
    }
    QJsonObject o;
    o["descripcion"] = "Contador de energia del propio medidor (resolucion 0,01 kWh). Dato bruto de diagnostico; "
                       "no es la energia del experimento (D-008).";
    o["inicial_kwh"] = inicial >= 0 ? QJsonValue(inicial) : QJsonValue(QJsonValue::Null);
    o["final_kwh"] = final >= 0 ? QJsonValue(final) : QJsonValue(QJsonValue::Null);
    o["diferencia_kwh"] = (inicial >= 0 && final >= 0) ? QJsonValue(final - inicial) : QJsonValue(QJsonValue::Null);
    return o;
}

} // namespace energia
