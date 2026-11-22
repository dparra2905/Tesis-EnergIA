// Auditoria.h - Auditoria de una corrida terminada (D-012).
//
// La auditoria INFORMA; no bloquea ni la corrida ni la subida. Se calcula siempre al terminar la
// corrida (despues de resultados.json, fuera de la medicion), con --postprocesar y, si falta,
// antes de subir la corrida. Se guarda en auditoria.json, en la carpeta de la corrida.
//
// Cada comprobacion tiene id, descripcion, valor medido, umbral aplicado, resultado (ok,
// advertencia, no_valida o no_evaluable), motivo legible y, si mira varias ventanas, fases o
// medidores, un detalle por cada uno. El veredicto (valida, con_advertencias, no_valida) es el
// peor resultado entre las comprobaciones evaluables: no_evaluable (corridas viejas o sin el
// medidor que hace falta) no empeora el veredicto.
//
// Comprobaciones (umbrales provisionales en AuditoriaReglas.h):
//   estado                    completada = ok; cancelada, error o detencion forzada = advertencia
//   pasos                     corrida completada: pasos registrados = step_total
//   medidores                 codigo de salida, forzado y termino_antes de cada medidor
//   cobertura_entrenamiento   cobertura de energia sin los huecos de borde
//   cobertura_inferencia      lo mismo en la ventana de inferencia, si existe (D-014)
//   pasos_inferencia          con la inferencia completa: al menos una pasada completa (D-014)
//   cobertura_lineas_base     cada ventana de linea base (base_pre, base_media, base_post)
//   caidas_ble                caidas de la conexion con el Atorch
//   muestras_saltadas         puntos de la rejilla que el monitor de software salto
//   actividad_ajena           minutos de entrenamiento e inferencia con CPU ajena al proceso
//                             medido (nunca invalida)
//   reposo_base_previa        CPU ajena media en la base previa
// Sin la inferencia activada, cobertura_inferencia y pasos_inferencia quedan no_evaluable.
//
// Para agregar una comprobacion: una funcion en Auditoria.cpp que devuelve una Comprobacion, su
// linea en la lista de auditar() y, si tiene umbral, su constante en AuditoriaReglas.h (y subir
// VERSION_REGLAS si cambia el veredicto de corridas existentes).
//
// Lee metadata.json, resultados.json, progreso.csv, sistema_metrics.csv, sistema_info.json y
// energia_info.json. Codigo nuevo de EnergIA. Solo Qt Core.
#pragma once

#include <QJsonObject>
#include <QString>

namespace auditoria {

constexpr const char* ARCHIVO = "auditoria.json";

constexpr const char* VALIDA = "valida";
constexpr const char* CON_ADVERTENCIAS = "con_advertencias";
constexpr const char* NO_VALIDA = "no_valida";

constexpr const char* OK = "ok";
constexpr const char* ADVERTENCIA = "advertencia";
constexpr const char* NO_EVALUABLE = "no_evaluable";

// Calcula la auditoria con los archivos de la carpeta. false (con el motivo) si no hay
// metadata.json. No escribe nada.
bool auditar(const QString& dirCorrida, QJsonObject* auditoria, QString* error);

// auditar() y escribe auditoria.json en la carpeta (de forma atomica)
bool auditarYGuardar(const QString& dirCorrida, QJsonObject* auditoria, QString* error);

// Residuo de CPU de una fila de sistema_metrics.csv, en puntos: (ocupado - proceso - medidor) /
// total * 100. false si falta algun dato o el total no es positivo.
bool residuoCpu(double ocupadoS, double totalS, double orquestadorS, double medidorS, double* residuo);

} // namespace auditoria
