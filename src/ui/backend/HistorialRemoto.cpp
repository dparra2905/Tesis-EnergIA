// HistorialRemoto.cpp - ver HistorialRemoto.h.
#include "HistorialRemoto.h"

#include "ClienteSupabase.h"
#include "Historial.h"
#include "PaqueteCorrida.h"
#include "ResumenCorrida.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

HistorialRemoto::HistorialRemoto(ClienteSupabase* sesion, QObject* parent) : QObject(parent), sesion_(sesion)
{
    // Otro usuario, otra lista
    connect(sesion_, &ClienteSupabase::sesionCerrada, this, [this] {
        corridas_.clear();
        ponerEstado("inactivo", {});
    });
}

void HistorialRemoto::ponerEstado(const QString& estado, const QString& error)
{
    estado_ = estado;
    error_ = error;
    emit cambiado();
}

void HistorialRemoto::recargar()
{
    if (estado_ == "cargando") return;
    ponerEstado("cargando", {});
    QUrlQuery q;
    q.addQueryItem("select", "id,usuario_id,equipo,numero,t_creacion,visibilidad,estado,modelo_dll,t_start,subida_en,"
                             "duracion_entrenamiento_s,energia_entrenamiento_wh,energia_total_wh,potencia_media_w,"
                             "accuracy_val,veredicto,nota,inferencia_activada,n_predicciones,energia_inferencia_wh,"
                             "f1_macro_val,experimento(nombre),perfil(nombre)");
    q.addQueryItem("order", "subida_en.desc");
    q.addQueryItem("limit", QString::number(LIMITE_LISTA));
    sesion_->rest("GET", "corrida", q, {}, [this](const ClienteSupabase::Resultado& r) {
        if (!r.ok()) {
            const bool sin008 = r.error.contains("inferencia_activada") || r.error.contains("n_predicciones")
                                || r.error.contains("energia_inferencia_wh");
            const bool sin009 = r.error.contains("f1_macro_val");
            ponerEstado("error", r.error.contains("t_creacion")
                                     ? "Falta aplicar la migración 003 en la base de datos (" + r.error + ")"
                                 : r.error.contains("veredicto") || r.error.contains("nota")
                                     ? "Falta aplicar la migración 007 en la base de datos (" + r.error + ")"
                                 : sin008 ? "Falta aplicar la migración 008 en la base de datos (" + r.error + ")"
                                 : sin009 ? "Falta aplicar la migración 009 en la base de datos (" + r.error + ")"
                                          : r.error);
            if (sin008) emit migracion008(false);
            // Sin la 008 la consulta falla antes de llegar a la columna de la 009: no se concluye nada de ella
            if (sin009) emit migracion009(false);
            return;
        }
        QVariantList lista;
        const QString yo = sesion_->usuarioId();
        for (const QJsonValue& v : r.cuerpo.array()) {
            const QJsonObject o = v.toObject();
            QVariantMap m = o.toVariantMap();
            m.remove("perfil");
            m["experimento"] = o["experimento"].isObject() ? o["experimento"].toObject()["nombre"].toString()
                                                           : o["experimento"].toString();
            m["dueno"] = o["perfil"].toObject()["nombre"].toString();
            m["propia"] = o["usuario_id"].toString() == yo;
            lista.append(m);
        }
        corridas_ = lista;
        ponerEstado("listo", {});
        emit migracion008(true);  // la lista trae las columnas de la 008
        emit migracion009(true);  // y la de la 009 (D-015)
        emit listaCargada(corridas_, corridas_.size() < LIMITE_LISTA);
    });
}

void HistorialRemoto::cambiarVisibilidad(qint64 id, const QString& visibilidad)
{
    if (visibilidad != "privada" && visibilidad != "compartida") {
        emit visibilidadCambiada(id, visibilidad, "Visibilidad desconocida: " + visibilidad);
        return;
    }
    QUrlQuery q;
    q.addQueryItem("id", QString("eq.%1").arg(id));
    q.addQueryItem("select", "id,visibilidad");
    const QByteArray cuerpo = QJsonDocument(QJsonObject{{"visibilidad", visibilidad}}).toJson(QJsonDocument::Compact);
    // return=representation: con RLS, una corrida ajena no da error sino 0 filas actualizadas
    sesion_->rest("PATCH", "corrida", q, cuerpo,
                  [this, id, visibilidad](const ClienteSupabase::Resultado& r) {
                      QString error = r.ok() ? QString() : r.error;
                      if (r.ok() && r.http != 204 && r.cuerpo.array().isEmpty())
                          error = "Solo quien subió la corrida puede cambiar su visibilidad.";
                      if (error.isEmpty()) {
                          for (QVariant& v : corridas_) {
                              QVariantMap m = v.toMap();
                              if (m.value("id").toLongLong() != id) continue;
                              m["visibilidad"] = visibilidad;
                              v = m;
                          }
                          emit cambiado();
                      }
                      emit visibilidadCambiada(id, visibilidad, error);
                  },
                  {{"Prefer", "return=representation"}});
}

void HistorialRemoto::cambiarNota(qint64 id, const QString& nota)
{
    QUrlQuery q;
    q.addQueryItem("id", QString("eq.%1").arg(id));
    q.addQueryItem("select", "id,nota");
    const QJsonValue valor = nota.trimmed().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(nota);
    const QByteArray cuerpo = QJsonDocument(QJsonObject{{"nota", valor}}).toJson(QJsonDocument::Compact);
    // return=representation: con RLS, una corrida ajena no da error sino 0 filas actualizadas
    sesion_->rest("PATCH", "corrida", q, cuerpo,
                  [this, id, nota](const ClienteSupabase::Resultado& r) {
                      QString error = r.ok() ? QString() : r.error;
                      if (r.ok() && r.http != 204 && r.cuerpo.array().isEmpty())
                          error = "Solo quien subió la corrida puede cambiar su nota.";
                      if (error.isEmpty()) {
                          for (QVariant& v : corridas_) {
                              QVariantMap m = v.toMap();
                              if (m.value("id").toLongLong() != id) continue;
                              m["nota"] = nota;
                              v = m;
                          }
                          if (detalle_.value("id_remoto").toLongLong() == id) {
                              detalle_["nota"] = nota;
                              emit detalleCambiado();
                          }
                          emit cambiado();
                      }
                      emit notaCambiada(id, nota, error);
                  },
                  {{"Prefer", "return=representation"}});
}

void HistorialRemoto::exportarCsv(qint64 id, const QString& destino)
{
    if (exportando_ != 0) {
        emit exportacionTerminada(id, destino, 0, "Ya hay una exportación en curso.");
        return;
    }
    exportando_ = id;
    emit exportacionCambiada();
    descargarSerie(id, 0, {}, [this, id, destino](const QJsonArray& serie, const QString& errorRed) {
        auto terminar = [&](int filas, const QString& error) {
            exportando_ = 0;
            emit exportacionCambiada();
            emit exportacionTerminada(id, destino, filas, error);
        };
        if (!errorRed.isEmpty()) {
            terminar(0, errorRed);
            return;
        }
        if (serie.isEmpty()) {
            terminar(0, "La corrida no tiene serie unida en la base (no llegó a medir).");
            return;
        }
        QString error;
        if (!analisis::escribirRejillaCsv(PaqueteCorrida::filasDesdeJson(serie), destino, &error)) {
            terminar(0, error);
            return;
        }
        terminar(static_cast<int>(serie.size()), {});
    });
}

void HistorialRemoto::descargarSerie(qint64 id, int desde, QJsonArray acumulado, AlTenerSerie alTerminar)
{
    QUrlQuery q;
    QStringList columnas{"t", "t_rel_s", "en_entrenamiento", "fase", "paso_desde", "paso_hasta"};
    for (const auto& c : analisis::COLUMNAS_REJILLA) columnas << c.nombre;
    columnas << "timestamp_trama";
    q.addQueryItem("select", columnas.join(','));
    q.addQueryItem("corrida_id", QString("eq.%1").arg(id));
    q.addQueryItem("order", "t.asc");
    q.addQueryItem("limit", QString::number(FILAS_POR_PAGINA));
    q.addQueryItem("offset", QString::number(desde));
    sesion_->rest("GET", "serie_unida", q, {},
                  [this, id, desde, acumulado, alTerminar](const ClienteSupabase::Resultado& r) mutable {
                      if (!r.ok()) {
                          alTerminar({}, r.error.contains("fase") || r.error.contains("paso_desde")
                                             ? "Falta aplicar la migración 005 en la base de datos (" + r.error + ")" : r.error);
                          return;
                      }
                      const QJsonArray pagina = r.cuerpo.array();
                      for (const QJsonValue& v : pagina) acumulado.append(v);
                      if (pagina.size() >= FILAS_POR_PAGINA) {
                          descargarSerie(id, desde + static_cast<int>(pagina.size()), acumulado, alTerminar);
                          return;
                      }
                      alTerminar(acumulado, {});
                  });
}

void HistorialRemoto::obtenerDetalle(qint64 id, AlTenerDetalle alTerminar)
{
    QUrlQuery q;
    q.addQueryItem("select", "*,experimento(nombre),perfil(nombre),"
                             "hardware_equipo(cpu,nucleos_logicos,ram_total_bytes,sistema_operativo,arquitectura),"
                             "dataset(nombre,filas,columnas,sha256)");
    q.addQueryItem("id", QString("eq.%1").arg(id));
    sesion_->rest("GET", "corrida", q, {}, [this, id, alTerminar](const ClienteSupabase::Resultado& r) {
        if (!r.ok()) {
            alTerminar({}, r.error.contains("hardware_equipo") || r.error.contains("resultados")
                               ? "Falta aplicar la migración 004 en la base de datos (" + r.error + ")" : r.error);
            return;
        }
        if (r.cuerpo.array().isEmpty()) {
            alTerminar({}, "La corrida no existe o no tienes permiso para verla.");
            return;
        }
        const QJsonObject corrida = r.cuerpo.array().first().toObject();
        descargarSerie(id, 0, {}, [this, corrida, alTerminar](const QJsonArray& serie, const QString& error) {
            if (!error.isEmpty()) {
                alTerminar({}, error);
                return;
            }
            alTerminar(construirDetalle(corrida, serie, sesion_->usuarioId()), {});
        });
    });
}

void HistorialRemoto::cargarDetalle(qint64 id)
{
    pedidoDetalle_ = id;
    detalle_.clear();
    estadoDetalle_ = "cargando";
    errorDetalle_.clear();
    emit detalleCambiado();
    obtenerDetalle(id, [this, id](const QVariantMap& d, const QString& error) {
        if (id != pedidoDetalle_) return;  // el usuario ya pidio otra
        detalle_ = d;
        estadoDetalle_ = error.isEmpty() ? "listo" : "error";
        errorDetalle_ = error;
        emit detalleCambiado();
    });
}

void HistorialRemoto::cargarComparacion(const QVariantList& ids)
{
    ++generacionComparacion_;
    comparacion_.clear();
    errorComparacion_.clear();
    if (ids.size() < 2 || ids.size() > MAX_COMPARAR) {
        estadoComparacion_ = "error";
        errorComparacion_ = QString("Elige entre 2 y %1 corridas para comparar.").arg(MAX_COMPARAR);
        emit comparacionCambiada();
        return;
    }
    estadoComparacion_ = "cargando";
    emit comparacionCambiada();
    siguienteComparacion(ids);
}

void HistorialRemoto::siguienteComparacion(QVariantList pendientes)
{
    if (pendientes.isEmpty()) {
        estadoComparacion_ = "listo";
        emit comparacionCambiada();
        return;
    }
    const qint64 id = pendientes.takeFirst().toLongLong();
    const int generacion = generacionComparacion_;
    obtenerDetalle(id, [this, pendientes, generacion](const QVariantMap& d, const QString& error) {
        if (generacion != generacionComparacion_) return;  // se pidio otra comparacion
        if (!error.isEmpty()) {
            estadoComparacion_ = "error";
            errorComparacion_ = error;
            emit comparacionCambiada();
            return;
        }
        comparacion_.append(d);
        siguienteComparacion(pendientes);
    });
}

void HistorialRemoto::borrar(qint64 id)
{
    QUrlQuery q;
    q.addQueryItem("id", QString("eq.%1").arg(id));
    q.addQueryItem("select", "id");
    // return=representation: con RLS, una corrida ajena no da error sino 0 filas borradas
    sesion_->rest("DELETE", "corrida", q, {},
                  [this, id](const ClienteSupabase::Resultado& r) {
                      QString error = r.ok() ? QString() : r.error;
                      if (r.ok() && r.cuerpo.array().isEmpty()) error = "Solo quien subió la corrida puede borrarla.";
                      if (error.isEmpty()) {
                          for (int i = corridas_.size() - 1; i >= 0; --i)
                              if (corridas_[i].toMap().value("id").toLongLong() == id) corridas_.removeAt(i);
                          if (detalle_.value("id_remoto").toLongLong() == id) {
                              detalle_.clear();
                              estadoDetalle_ = "inactivo";
                              emit detalleCambiado();
                          }
                          emit cambiado();
                      }
                      emit corridaBorrada(id, error);
                  },
                  {{"Prefer", "return=representation"}});
}

QVariantMap HistorialRemoto::construirDetalle(const QJsonObject& c, const QJsonArray& serie, const QString& usuarioId)
{
    const qint64 id = c["id"].toInteger();
    const QJsonObject metadata = c["metadata"].toObject();
    const QJsonObject resultados = c["resultados"].toObject();
    QVariantMap r = Historial::resumenDesdeJson(metadata, resultados, QString("base:%1").arg(id));
    if (r.isEmpty()) {
        // Sin copia de metadata.json: lo minimo desde las columnas
        r["dir"] = QString("base:%1").arg(id);
        r["estado"] = c["estado"].toString();
        r["modelo_nombre"] = c["modelo_dll"].toString();
        r["hiperparametros"] = QVariantMap();
        r["evaluacion"] = QVariantMap();
        r["medidores"] = QStringList();
    }
    r["origen"] = "base";
    r["id_remoto"] = id;
    r["experimento"] = c["experimento"].isObject() ? c["experimento"].toObject()["nombre"].toString() : r.value("experimento");
    r["equipo"] = c["equipo"].toString();
    r["visibilidad"] = c["visibilidad"].toString();
    r["propia"] = c["usuario_id"].toString() == usuarioId;
    r["dueno"] = c["perfil"].toObject()["nombre"].toString();
    r["subida_en"] = c["subida_en"].toString();
    r["hardware"] = c["hardware_equipo"].isObject() ? QVariant(c["hardware_equipo"].toObject().toVariantMap()) : QVariant();
    r["dataset"] = c["dataset"].isObject() ? QVariant(c["dataset"].toObject().toVariantMap()) : QVariant();
    // Auditoria y nota (D-012): null en las corridas subidas antes de la migracion 007
    r["veredicto"] = c["veredicto"].toString();
    r["auditoria"] = c["auditoria"].toObject().toVariantMap();
    r["nota"] = c["nota"].toString();
    r["t_creacion"] = c["t_creacion"].toString(r.value("t_creacion").toString());
    // Metricas del modelo (D-015): la copia de metricas_modelo.json; vacia si no hay (corrida sin
    // metricas o subida antes de la migracion 009), como en el detalle local
    r["metricas_modelo"] = c["metricas_modelo"].toObject().toVariantMap();
    // Resumen guardado en columnas (para la comparacion)
    for (const char* k : {"energia_entrenamiento_wh", "energia_total_wh", "potencia_media_w", "potencia_max_w",
                          "cpu_media_pct", "accuracy_val", "duracion_entrenamiento_s",
                          // Inferencia (D-014, migracion 008): null en las corridas subidas antes
                          "inferencia_activada", "n_predicciones", "energia_inferencia_wh", "energia_por_prediccion_j",
                          "tiempo_por_prediccion_us",
                          // Metricas del modelo (D-015, migracion 009): null en las corridas subidas antes
                          "f1_macro_val", "f1_ponderado_val", "precision_macro_val", "recall_macro_val",
                          "accuracy_entrenamiento", "f1_macro_entrenamiento"})
        r[QString("col_") + k] = c[k].toVariant();
    // Corridas subidas antes de la copia de resultados.json: la energia de la columna
    if (!r.contains("energia_experimento") && c["energia_entrenamiento_wh"].isDouble())
        r["energia_experimento"] = QVariantMap{{"entrenamiento", QVariantMap{{"energia_wh", c["energia_entrenamiento_wh"].toDouble()}}}};

    // Estadisticas por ventana y series, desde la serie unida (mismas reglas que con los CSV)
    const QList<analisis::FilaRejilla> filas = PaqueteCorrida::filasDesdeJson(serie);
    auto ms = [&r](const char* k) {
        const QDateTime t = QDateTime::fromString(r.value(k).toString(), Qt::ISODateWithMs);
        return t.isValid() ? t.toMSecsSinceEpoch() : qint64(0);
    };
    const qint64 tStart = ms("t_start"), tAntes = ms("t_antes"), tDespues = ms("t_despues"), tStop = ms("t_stop");
    const qint64 tInfAntes = ms("t_inf_antes"), tInfDespues = ms("t_inf_despues");
    QVariantMap ventanas;
    if (tAntes > 0 && tDespues > tAntes) ventanas["entrenamiento"] = analisis::resumenRejilla(filas, tAntes, tDespues);
    if (tStart > 0 && tStop > tStart) ventanas["medicion"] = analisis::resumenRejilla(filas, tStart, tStop);
    if (tInfAntes > 0 && tInfDespues > tInfAntes) ventanas["inferencia"] = analisis::resumenRejilla(filas, tInfAntes, tInfDespues);
    r["ventanas"] = ventanas;
    if (tStart > 0) {
        const QVariantMap s = analisis::seriesRejilla(filas);
        r["series_sistema"] = s.value("series_sistema");
        r["series_energia"] = s.value("series_energia");
        r["t_antes_rel_s"] = tAntes > 0 ? QVariant((tAntes - tStart) / 1000.0) : QVariant();
        r["t_despues_rel_s"] = tDespues > 0 ? QVariant((tDespues - tStart) / 1000.0) : QVariant();
        r["t_inf_antes_rel_s"] = tInfAntes > 0 ? QVariant((tInfAntes - tStart) / 1000.0) : QVariant();
        r["t_inf_despues_rel_s"] = tInfDespues > 0 ? QVariant((tInfDespues - tStart) / 1000.0) : QVariant();
    }
    // Serie del entrenamiento desde su inicio (segundo 0 = t_antes), para superponer corridas
    QVariantList potencia, cpu;
    for (const analisis::FilaRejilla& f : filas) {
        if (!f.enEntrenamiento) continue;
        potencia.append(f.valores.contains("potencia_w") ? QVariant(f.valores.value("potencia_w")) : QVariant());
        cpu.append(f.valores.contains("cpu_proceso_pct") ? QVariant(f.valores.value("cpu_proceso_pct")) : QVariant());
    }
    r["serie_entrenamiento"] = QVariantMap{{"potencia_w", potencia}, {"cpu_proceso_pct", cpu}};
    r["puntos_serie"] = static_cast<int>(filas.size());
    return r;
}

QString HistorialRemoto::rutaLocal(const QUrl& url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
