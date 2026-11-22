// PaqueteCorrida.h - JSON de una corrida para la funcion subir_corrida de la base (D-011).
//
// Nivel 3: configuracion + resumen + serie unida por segundo. Se arma con los archivos de la
// carpeta de la corrida, sin modificarlos:
//   metadata.json     configuracion, estado, tiempos, hiperparametros (y una copia integra)
//   resultados.json   energia del experimento por ventana (dato derivado, D-008)
//   auditoria.json    auditoria de la corrida (copia integra y veredicto, D-012)
//   nota.txt          nota de condiciones (D-012)
//   progreso.csv      un paso por unidad (epoca, arbol, ...) y sus metricas, tarea "entrenamiento"
//   progreso_inferencia.csv  un paso por grupo de pasadas y sus metricas, tarea "inferencia" (D-014)
//   rejilla de 1 s    la misma serie que exporta Historial::exportarCsv (D-007)
// Los CSV crudos de los medidores NO se suben: se quedan en el PC donde se midio.
//
// Estructura: ver la cabecera de subir_corrida en database/energia_supabase.sql.
#pragma once

#include "ResumenCorrida.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace PaqueteCorrida {

// Arma el paquete. 'sha256Dataset' es la huella del CSV de datos ("" = sin dataset, p. ej.
// datos sinteticos o archivo ya no disponible). "" en *error si salio bien.
QJsonObject construir(const QString& dirCorrida, const QString& equipo, const QString& sha256Dataset, QString* error);

// Foto del equipo donde se midio (tabla hardware_equipo): nombre, CPU, nucleos, RAM, sistema
// operativo y arquitectura. Nucleos y RAM salen de sistema_info.json de la corrida si existe;
// el resto, de este equipo (la cola solo sube corridas medidas aqui).
QJsonObject hardwareLocal(const QString& equipo, const QString& dirCorrida);

// metadata.datos de la corrida (vacio si no hay metadata.json). En las corridas nuevas trae la
// huella del dataset (sha256, tam_bytes, fecha_modificacion), calculada al preparar los datos.
QJsonObject datosMetadata(const QString& dirCorrida);

// Ruta del CSV de datos de la corrida ("" si los datos fueron sinteticos)
QString rutaDataset(const QString& dirCorrida);

// sha256 (hex en minusculas) de un archivo; "" si no se pudo leer. Lee por bloques: sirve
// para CSV de varios GB, pero tarda (llamarlo fuera del hilo de la interfaz).
QString huellaArchivo(const QString& ruta, QString* error);

// Serie de la rejilla <-> JSON con los nombres de columna de la tabla serie_unida
QJsonArray serieJson(const QList<analisis::FilaRejilla>& filas);
QList<analisis::FilaRejilla> filasDesdeJson(const QJsonArray& serie);

} // namespace PaqueteCorrida
