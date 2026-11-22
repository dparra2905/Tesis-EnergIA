// ResumenCorrida.h - Resumen de las mediciones de una corrida terminada (F9).
//
// Lee los archivos que dejan los medidores en la carpeta de la corrida y calcula minimo,
// maximo y promedio por metrica en una ventana de tiempo, series para graficas y la
// exportacion de la rejilla de 1 s (D-007) a un solo CSV. No interpreta los resultados.
//
//   Monitor de software (sistema_metrics.csv): cada fila resume el intervalo de ~1 s que
//   TERMINA en su timestamp. Se usan las filas cuyo intervalo completo cae en la ventana.
//     cpu_proceso_pct  orq_cpu_s / cpu_sis_total_s * 100   (parte de la capacidad del equipo)
//     cpu_sistema_pct  cpu_sis_ocupado_s / cpu_sis_total_s * 100
//     ram_proceso_mb   orq_ws_bytes / 2^20
//     ram_sistema_pct  ram_sis_usada_bytes / ram_total_bytes (sistema_info.json) * 100
//   Monitor fisico de energia (energia_atorch.csv): cada fila es el estado de la ultima trama
//   vigente EN su timestamp. Se usan las filas con trama dentro de la ventana.
//     potencia_w, voltaje_v, corriente_a
// La energia del experimento NO se calcula aqui: es la de resultados.json (D-008).
//
// Codigo nuevo de EnergIA.
#pragma once

#include "FasesCorrida.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace analisis {

// {serie: {n, min, max, media}} para el monitor de software en [desdeMs, hastaMs]
QVariantMap resumenSistema(const QString& dirCorrida, qint64 desdeMs, qint64 hastaMs);
// {serie: {n, min, max, media}, filas, filas_con_trama} para el monitor fisico en [desdeMs, hastaMs]
QVariantMap resumenEnergia(const QString& dirCorrida, qint64 desdeMs, qint64 hastaMs);

// Series para graficas: {t_s: [segundos desde t_start], <serie>: [valores o null]}
QVariantMap seriesSistema(const QString& dirCorrida, qint64 tStartMs);
QVariantMap seriesEnergia(const QString& dirCorrida, qint64 tStartMs);

// Rejilla de 1 s de ambos medidores, unida por timestamp (D-007). Es la misma serie que se
// exporta a CSV y que se sube a la base de datos (tabla serie_unida, D-011).
//
// Cada fila representa el intervalo (t - 1 s, t] de su punto t:
//   fase        base_pre | entrenamiento | base_media | inferencia | base_post | transicion | ""
//               (reglas en FasesCorrida.h)
//   paso_desde,
//   paso_hasta  rango de step_index cuyos intervalos [timestamp_inicio, timestamp_fin] tocan el
//               de la fila (inicio <= t y fin > t - 1 s); -1 si ninguno. Los pasos son los de
//               progreso.csv, salvo en las filas con t posterior a t_inf_antes, que usan los
//               grupos de progreso_inferencia.csv (D-014). Si base_media dura menos de 1 s, la
//               fila que cruza t_despues y t_inf_antes muestra solo los grupos de la inferencia.
struct FilaRejilla {
    qint64 ms = 0;              // punto de la rejilla (epoch ms)
    QString timestamp;          // el mismo punto, tal como lo escribio el medidor
    double tRelS = 0;           // segundos desde t_start
    bool enEntrenamiento = false;  // t_antes <= t <= t_despues
    QString fase;
    int pasoDesde = -1, pasoHasta = -1;
    QMap<QString, double> valores;  // columnas de COLUMNAS_REJILLA con dato
    QString timestampTrama;     // hora real de la trama del Atorch ("" si no hubo dato)
};
// Columnas de valor de la rejilla, en orden, con sus decimales en el CSV
struct ColumnaRejilla { const char* nombre; int decimales; };
extern const QList<ColumnaRejilla> COLUMNAS_REJILLA;

bool rejilla(const QString& dirCorrida, const fases::Marcas& marcas, QList<FilaRejilla>* filas, QString* error);

// Escribe la rejilla en un CSV:
//   timestamp, t_rel_s, en_entrenamiento, fase, paso_desde, paso_hasta, cpu_proceso_pct,
//   cpu_sistema_pct, ram_proceso_mb, ram_sistema_pct, potencia_w, voltaje_v, corriente_a,
//   timestamp_trama
// Las columnas de un medidor que no participo (o una fila sin dato) quedan vacias, igual que
// fase y pasos cuando no tienen valor.
bool escribirRejillaCsv(const QList<FilaRejilla>& filas, const QString& destino, QString* error);

// Lo mismo que resumenSistema/resumenEnergia, pero desde la rejilla (p. ej. la serie unida
// descargada de la base): {sistema: {serie: {n, min, max, media}}, energia: {...}} en
// [desdeMs, hastaMs], con las mismas reglas de ventana que los CSV de los medidores.
QVariantMap resumenRejilla(const QList<FilaRejilla>& filas, qint64 desdeMs, qint64 hastaMs);
// Series para graficas desde la rejilla: {series_sistema: {t_s, ...}, series_energia: {t_s, ...}}.
// Cada familia solo incluye los puntos en que su medidor tiene dato.
QVariantMap seriesRejilla(const QList<FilaRejilla>& filas);

// rejilla() + escribirRejillaCsv()
bool exportarRejilla(const QString& dirCorrida, const fases::Marcas& marcas, const QString& destino, int* filas,
                     QString* error);

} // namespace analisis
