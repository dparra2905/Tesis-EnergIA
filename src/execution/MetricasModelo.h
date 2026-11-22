// MetricasModelo.h - Metricas de clasificacion del modelo entrenado (D-015).
//
// Con las predicciones de la DLL (predecir) y las etiquetas reales de un conjunto calcula, igual
// para todos los modelos:
//   - la matriz de confusion n_clases x n_clases (filas = clase real, columnas = clase predicha);
//   - la exactitud;
//   - por clase: precision, recall, F1 y soporte (filas reales de la clase);
//   - los promedios macro (media simple de las clases) y ponderado (por el soporte) de precision,
//     recall y F1.
//
// Division por cero como scikit-learn (precision_recall_fscore_support con zero_division=0 y
// labels = todas las clases): una metrica no definida vale 0 y se marca con indefinida: true.
//   precision = tp / (tp + fp)          indefinida si la clase nunca se predijo
//   recall    = tp / (tp + fn)          indefinida si la clase no tiene filas (soporte 0)
//   F1        = 2 tp / (2 tp + fp + fn) indefinida si la clase ni aparece ni se predijo
// F1 se calcula con la formula de conteos, como scikit-learn desde la version 1.3: si la clase tiene
// soporte pero nunca se predijo, F1 vale 0 y esta definida. Los promedios incluyen todas las clases
// (las indefinidas con 0), como scikit-learn con labels.
//
// Lo usa el orquestador al cerrar el modelo, con la medicion ya detenida (Trabajador::cerrarModelo).
// Solo Qt Core (JSON); no depende de la DLL.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>

namespace metricas {

// metricas_modelo.json, en la carpeta de la corrida
constexpr const char* ARCHIVO = "metricas_modelo.json";
constexpr int VERSION_FORMATO = 1;
// Filas por bloque de predecir: el mismo valor por defecto que el lote de la inferencia
// (ConfigExperimento::INFERENCIA_LOTE_DEFECTO). No lo configura el usuario.
constexpr int32_t LOTE_PREDECIR = 65536;

// Metricas de un conjunto: n_filas, matriz_confusion, exactitud, por_clase, macro y ponderado.
// 'predicciones' y 'reales' tienen n filas con indices 0 .. nClases-1 (las reales llegan como double,
// como en EnergiaDatos::Y). 'clases' son las etiquetas originales (clases_originales), una por indice.
// Una prediccion fuera de rango cuenta como error (en el soporte y la exactitud) y no entra en la
// matriz; queda en "predicciones_fuera_de_rango" (0 con una DLL correcta).
QJsonObject calcular(const int32_t* predicciones, const double* reales, int64_t n, int32_t nClases,
                     const QJsonArray& clases);

} // namespace metricas
