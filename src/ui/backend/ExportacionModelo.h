// ExportacionModelo.h - Exportar el modelo de una corrida para usarlo fuera de EnergIA (D-013).
//
// EnergIA no recarga modelo.bin (contrato v4, D-009), pero el Arbol y el MLP se pueden usar fuera
// con la exportacion: un .zip, en la ubicacion que elija el usuario, con
//   modelo_mlpack.bin o modelo_libtorch.pt  el modelo.bin de la carpeta, renombrado segun su
//                                           formato (FormatosModelo.h)
//   modelo.json                             formato, libreria, hiperparametros, columnas de entrada,
//                                           clases, metricas, particion, corrida y sha256 del dataset;
//                                           resumen de las metricas del modelo (D-015)
//   LEEME.txt                               como cargar el modelo y preparar los datos
//   metricas_modelo.json                    copia del archivo de la corrida (D-015), si existe
// El modelo exportado NO se sube a la base.
//
// sha256 del dataset (modelo.json: sha256_dataset y sha256_origen):
//   "corrida"                metadata.datos.sha256, calculado al preparar los datos;
//   "calculada_al_exportar"  corridas anteriores a esa huella: de huellas_datasets.json si coinciden
//                            ruta, tamano y fecha, o leyendo el CSV; describe el archivo ACTUAL;
//   null                     datos sinteticos o CSV que ya no existe.
// Corridas anteriores a columnas_entrada: se exportan igual, con columnas_entrada null y un aviso.
//
// Codigo nuevo de EnergIA. Solo Qt Core.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace exportacion {

constexpr int VERSION_EXPORTACION = 1;
constexpr const char* ARCHIVO_JSON = "modelo.json";
constexpr const char* ARCHIVO_LEEME = "LEEME.txt";
constexpr const char* ARCHIVO_METRICAS = "metricas_modelo.json";

// Si el modelo de la corrida se puede exportar, sin leer el dataset:
//   {exportable, motivo (si no), formato, archivo (nombre en el .zip), columnas_conocidas,
//    datos_sinteticos, avisos: [texto]}
// Exportable = existe modelo.bin y el modelo tiene un formato exportable (no la DLL de prueba).
QJsonObject info(const QString& dirCorrida);

// sha256 del dataset para modelo.json: {sha256_dataset, sha256_origen}. Puede leer el CSV
// completo (llamarla fuera del hilo de la interfaz).
QJsonObject huellaDataset(const QJsonObject& metadata, const QString& rutaHuellas);

// Resumen de metricas_modelo.json para modelo.json (D-015): {archivo, validacion, entrenamiento}, con
// exactitud y precision, recall y F1 macro de cada conjunto (null y su motivo si predecir fallo).
// null si la corrida no tiene metricas (corrida vieja o sin entrenamiento completo).
QJsonValue resumenMetricasModelo(const QJsonObject& metricasModelo);

// Contenido de modelo.json a partir de metadata.json, la huella y metricas_modelo.json (vacio si no hay)
QJsonObject modeloJson(const QJsonObject& metadata, const QJsonObject& huella, const QDateTime& exportado,
                       const QJsonObject& metricasModelo = QJsonObject());

// Contenido de LEEME.txt a partir de modelo.json
QString leeme(const QJsonObject& modeloJson);

// Escribe el .zip. false (con el motivo) si la corrida no es exportable o no se pudo escribir.
// Puede tardar (sha256 de corridas viejas): fuera del hilo de la interfaz.
bool exportar(const QString& dirCorrida, const QString& destinoZip, const QString& rutaHuellas, QJsonObject* modeloJson,
              QString* error);

} // namespace exportacion
