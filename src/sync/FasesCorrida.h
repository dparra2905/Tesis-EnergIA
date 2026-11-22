// FasesCorrida.h - Fases de la linea de tiempo de una corrida (linea base, entrenamiento e
// inferencia).
//
// Reglas unicas para la columna 'fase' de la serie unida (analisis::rejilla) y para las
// ventanas de energia base_pre / base_media / inferencia / base_post de resultados.json (D-008,
// D-011, D-014), para que la serie y la energia coincidan:
//
//   base_pre       [t_start, t_antes]; si no hubo entrenamiento (sin t_antes): [t_start, t_stop]
//   entrenamiento  [t_antes, t_despues]; si falta t_despues: [t_antes, t_stop]
//   base_media     [t_despues, t_inf_antes], solo si la linea base intermedia empezo; si se
//                  cancelo dentro de ella (sin t_inf_antes): [t_despues, t_stop]
//   inferencia     [t_inf_antes, t_inf_despues], solo si la inferencia empezo; si falta
//                  t_inf_despues: [t_inf_antes, t_stop]
//   base_post      [t_despues, t_stop] sin inferencia; [t_inf_despues, t_stop] si la inferencia
//                  empezo. Solo si la fase posterior empezo (si se cancelo dentro de ella, es
//                  parcial: lo medido)
//
// Columna 'fase' de cada fila de la rejilla. La fila del punto t representa el intervalo
// (t - 1 s, t]:
//   "transicion"   el intervalo cruza t_antes, t_despues, t_inf_antes o t_inf_despues
//                  (t - 1 s < marca < t);
//   la fase cuyo intervalo lo contiene completo (t - 1 s >= desde y t <= hasta);
//   ""             en otro caso (fila de t_start del Atorch, filas que cruzan o pasan t_stop,
//                  filas entre t_despues y t_stop si la fase posterior no empezo). Tambien en
//                  todas las filas si falta t_stop (detencion forzada: metadata en_curso).
//
// metadata.tiempos.fase_posterior_iniciada la escribe el orquestador desde el 21/10/2026. En
// las corridas anteriores se deduce: status_dll == 0, fase_posterior_ms > 0 y
// t_stop - t_despues >= 1 s (una cancelacion que llego justo despues del ultimo callback deja
// la DLL en OK pero no deja empezar la fase posterior). fase_media_iniciada e
// inferencia_iniciada (D-014) no se deducen: las corridas anteriores no tienen inferencia.
//
// Codigo nuevo de EnergIA. Solo Qt Core.
#pragma once

#include <QJsonObject>
#include <QString>

namespace fases {

constexpr const char* BASE_PRE = "base_pre";
constexpr const char* ENTRENAMIENTO = "entrenamiento";
constexpr const char* BASE_POST = "base_post";
constexpr const char* BASE_MEDIA = "base_media";
constexpr const char* INFERENCIA = "inferencia";
constexpr const char* TRANSICION = "transicion";

struct Marcas {
    qint64 tStartMs = 0, tAntesMs = 0, tDespuesMs = 0, tStopMs = 0;  // 0 = no existe
    bool posteriorIniciada = false;
    QString origenPosterior = "metadata";  // "metadata" | "deducida"
    // Inferencia (D-014); 0 / false si la corrida no la tuvo
    qint64 tInfAntesMs = 0, tInfDespuesMs = 0;
    bool mediaIniciada = false, inferenciaIniciada = false;
};

struct Intervalo {
    bool existe = false;
    qint64 desdeMs = 0, hastaMs = 0;
};

// Marcas de metadata.json (tiempos y entrenamiento.status_dll)
Marcas desdeMetadata(const QJsonObject& metadata);

Intervalo basePre(const Marcas& m);
Intervalo entrenamiento(const Marcas& m);
Intervalo baseMedia(const Marcas& m);
Intervalo inferencia(const Marcas& m);
Intervalo basePost(const Marcas& m);

// Fase de la fila de la rejilla cuyo punto es tMs (ver arriba); "" si no tiene
QString faseFila(const Marcas& m, qint64 tMs);

} // namespace fases
