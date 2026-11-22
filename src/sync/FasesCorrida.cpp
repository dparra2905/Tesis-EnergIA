// FasesCorrida.cpp - ver FasesCorrida.h.
#include "FasesCorrida.h"

#include <QDateTime>

namespace fases {

namespace {

constexpr qint64 INTERVALO_MS = 1000;           // rejilla de 1 s (D-007)
constexpr qint64 POSTERIOR_MINIMA_MS = 1000;    // salvaguarda al deducir la marca

qint64 ms(const QJsonValue& v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

bool contiene(const Intervalo& i, qint64 tMs)
{
    return i.existe && tMs - INTERVALO_MS >= i.desdeMs && tMs <= i.hastaMs;
}

bool cruza(qint64 marcaMs, qint64 tMs)
{
    return marcaMs > 0 && tMs - INTERVALO_MS < marcaMs && marcaMs < tMs;
}

} // namespace

Marcas desdeMetadata(const QJsonObject& metadata)
{
    const QJsonObject t = metadata["tiempos"].toObject();
    Marcas m;
    m.tStartMs = ms(t["t_start"]);
    m.tAntesMs = ms(t["t_antes"]);
    m.tDespuesMs = ms(t["t_despues"]);
    m.tStopMs = ms(t["t_stop"]);
    if (t["fase_posterior_iniciada"].isBool()) {
        m.posteriorIniciada = t["fase_posterior_iniciada"].toBool();
        m.origenPosterior = "metadata";
    } else {
        const QJsonValue status = metadata["entrenamiento"].toObject()["status_dll"];
        m.posteriorIniciada = status.isDouble() && status.toInt() == 0 && t["fase_posterior_ms"].toDouble() > 0
                              && m.tDespuesMs > 0 && m.tStopMs - m.tDespuesMs >= POSTERIOR_MINIMA_MS;
        m.origenPosterior = "deducida";
    }
    m.tInfAntesMs = ms(t["t_inf_antes"]);
    m.tInfDespuesMs = ms(t["t_inf_despues"]);
    m.mediaIniciada = t["fase_media_iniciada"].toBool(false);
    m.inferenciaIniciada = t["inferencia_iniciada"].toBool(false);
    return m;
}

Intervalo basePre(const Marcas& m)
{
    const qint64 fin = m.tAntesMs > 0 ? m.tAntesMs : m.tStopMs;
    if (m.tStartMs <= 0 || fin < m.tStartMs || fin <= 0) return {};
    return {true, m.tStartMs, fin};
}

Intervalo entrenamiento(const Marcas& m)
{
    const qint64 fin = m.tDespuesMs > 0 ? m.tDespuesMs : m.tStopMs;
    if (m.tAntesMs <= 0 || fin < m.tAntesMs || fin <= 0) return {};
    return {true, m.tAntesMs, fin};
}

Intervalo baseMedia(const Marcas& m)
{
    const qint64 fin = m.tInfAntesMs > 0 ? m.tInfAntesMs : m.tStopMs;
    if (!m.mediaIniciada || m.tDespuesMs <= 0 || fin < m.tDespuesMs || fin <= 0) return {};
    return {true, m.tDespuesMs, fin};
}

Intervalo inferencia(const Marcas& m)
{
    const qint64 fin = m.tInfDespuesMs > 0 ? m.tInfDespuesMs : m.tStopMs;
    if (!m.inferenciaIniciada || m.tInfAntesMs <= 0 || fin < m.tInfAntesMs || fin <= 0) return {};
    return {true, m.tInfAntesMs, fin};
}

Intervalo basePost(const Marcas& m)
{
    // Con la inferencia, la fase posterior empieza al terminar inferir
    const qint64 inicio = (m.inferenciaIniciada && m.tInfDespuesMs > 0) ? m.tInfDespuesMs : m.tDespuesMs;
    if (!m.posteriorIniciada || inicio <= 0 || m.tStopMs < inicio) return {};
    return {true, inicio, m.tStopMs};
}

QString faseFila(const Marcas& m, qint64 tMs)
{
    if (m.tStopMs <= 0) return {};  // detencion forzada: no se sabe donde termino cada fase
    if (cruza(m.tAntesMs, tMs) || cruza(m.tDespuesMs, tMs) || cruza(m.tInfAntesMs, tMs) || cruza(m.tInfDespuesMs, tMs))
        return TRANSICION;
    if (contiene(basePre(m), tMs)) return BASE_PRE;
    if (contiene(entrenamiento(m), tMs)) return ENTRENAMIENTO;
    if (contiene(baseMedia(m), tMs)) return BASE_MEDIA;
    if (contiene(inferencia(m), tMs)) return INFERENCIA;
    if (contiene(basePost(m), tMs)) return BASE_POST;
    return {};
}

} // namespace fases
