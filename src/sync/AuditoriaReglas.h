// AuditoriaReglas.h - Umbrales de la auditoria de corridas (D-012), en un solo lugar.
//
// PROVISIONALES: salieron de las primeras corridas de una hora y se ajustaran con mas corridas.
// Cualquier cambio de un umbral o de una regla sube VERSION_REGLAS, que queda escrita en
// auditoria.json (asi se sabe con que reglas se audito cada corrida).
//
// Coberturas como fraccion (0,98 = 98 %); porcentajes de CPU en puntos (0 a 100).
//
// Codigo nuevo de EnergIA. Solo C++ estandar.
#pragma once

namespace auditoria::reglas {

// 1: primera version (D-012). 2: inferencia (D-014): cobertura_inferencia, pasos_inferencia,
//    base_media en cobertura_lineas_base, minutos de inferencia en actividad_ajena y los motivos
//    de estado de las fases nuevas. Las corridas auditadas con la version 1 no se recalculan solas.
constexpr int VERSION_REGLAS = 2;

// cobertura_entrenamiento y cobertura_inferencia: cobertura de energia de la ventana sin contar
// los huecos borde_inicial / borde_final. ok >= 98 %, advertencia 90-98 %, no_valida < 90 %
constexpr double COBERTURA_OK = 0.98;
constexpr double COBERTURA_MINIMA = 0.90;

// cobertura_lineas_base: cada ventana de linea base que exista (base_pre, base_media, base_post).
// ok >= 90 %, advertencia < 90 %
constexpr double COBERTURA_LINEA_BASE_OK = 0.90;

// caidas_ble: energia_info.json -> cierre.caidas. ok 0-2, advertencia >= 3
constexpr int CAIDAS_BLE_MAXIMAS_OK = 2;

// muestras_saltadas: sistema_info.json -> muestras_saltadas / filas_escritas.
// ok <= 1 %, advertencia 1-10 %, no_valida > 10 %
constexpr double SALTADAS_OK = 0.01;
constexpr double SALTADAS_MAXIMA = 0.10;

// Residuo de CPU de una fila: 100 * (cpu_sis_ocupado_s - orq_cpu_s - med_cpu_s) / cpu_sis_total_s
// (la CPU del equipo que no es ni del proceso medido ni del monitor de software).
// Referencia: el residuo medio de la base previa si tiene al menos FILAS_REFERENCIA filas; si no,
// RESIDUO_REFERENCIA_DEFECTO.
constexpr int FILAS_REFERENCIA = 10;
constexpr double RESIDUO_REFERENCIA_DEFECTO = 3.0;

// actividad_ajena: porcentaje de minutos de entrenamiento (y de inferencia) cuyo residuo medio
// supera la referencia + MARGEN_ACTIVIDAD puntos. ok <= 10 %, advertencia > 10 %. Nunca invalida:
// medir con carga ajena puede ser el objeto del experimento.
constexpr double MARGEN_ACTIVIDAD = 3.0;
constexpr double MINUTOS_AJENOS_OK = 0.10;
constexpr long long MINUTO_MS = 60 * 1000;

// reposo_base_previa: residuo medio de la base previa. ok <= 10 %, advertencia > 10 %
constexpr double RESIDUO_REPOSO_OK = 10.0;

} // namespace auditoria::reglas
