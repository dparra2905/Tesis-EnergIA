// EnergiaExperimento.h - Energia del experimento a partir de las tramas del Atorch (D-008, F7).
//
// Dato derivado: se integra power_w en el tiempo usando las tramas reales y su hora de llegada
// (timestamp_trama, D-007). Reglas (D-008):
//   - cada trama cuenta una sola vez, aunque aparezca repetida en la rejilla de 1 s;
//   - integracion por trapecios entre tramas consecutivas, recortada a la ventana pedida;
//   - un salto entre tramas mayor que el umbral de hueco NO se integra ni se interpola: queda
//     registrado como hueco y reduce la cobertura;
//   - el resultado registra metodo, version, ventana, cobertura y huecos;
//   - el contador de energia del medidor (energy_kwh) se informa aparte, solo como diagnostico.
// No se decide aqui si un resultado es valido: la politica de cobertura esta pendiente.
//
// Codigo nuevo de EnergIA (Back-EnergIA no tiene post-proceso).
#pragma once

#include "FasesCorrida.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace energia {

constexpr int VERSION_METODO = 1;
constexpr const char* METODO = "trapecio_tramas_unicas";
// Mismo valor que UMBRAL_TRAMA_VIEJA_S de energia_ble.py (calibrado con el Atorch del laboratorio)
constexpr double UMBRAL_HUECO_S = 3.0;

struct Trama {
    qint64 tMs = 0;           // timestamp_trama en ms UTC
    double potenciaW = 0.0;
    double contadorKwh = -1;  // energy_kwh del medidor; < 0 si no se conoce
    QString rawHex;           // para identificar repeticiones
};

struct Hueco {
    qint64 inicioMs = 0;
    qint64 finMs = 0;
    QString tipo;             // "entre_tramas" | "borde_inicial" | "borde_final" | "sin_tramas"
};

struct ResultadoVentana {
    qint64 inicioMs = 0, finMs = 0;
    double energiaJ = 0.0;
    double segundosCubiertos = 0.0;
    double cobertura = 0.0;   // segundosCubiertos / duracion de la ventana
    int tramasUsadas = 0;
    QList<Hueco> huecos;
};

// Quita repeticiones (misma hora de llegada y mismos bytes) y ordena por tiempo
QList<Trama> tramasUnicas(const QList<Trama>& tramas, int* repetidas = nullptr);

// Integra power_w sobre [inicioMs, finMs]. 'tramas' debe venir de tramasUnicas().
ResultadoVentana integrar(const QList<Trama>& tramas, qint64 inicioMs, qint64 finMs,
                          double umbralHuecoS = UMBRAL_HUECO_S);

// Lee las tramas de un CSV del medidor. Acepta energia_tramas.csv (una fila por trama) y
// energia_atorch.csv (rejilla de 1 s: las filas sin trama se ignoran y las repetidas se
// eliminan despues con tramasUnicas()). Columnas usadas: timestamp_trama, power_w,
// energy_kwh y raw_hex.
bool leerTramasCsv(const QString& ruta, QList<Trama>* tramas, QString* error);

QJsonObject aJson(const ResultadoVentana& r);

// Contador del medidor en la ventana (diagnostico, no es la energia del experimento)
QJsonObject contadorMedidor(const QList<Trama>& tramas, qint64 inicioMs, qint64 finMs);

// Archivo de resultados derivados de una corrida
constexpr const char* ARCHIVO_RESULTADOS = "resultados.json";
// 2: ventanas base_pre y base_post (linea base medida, sin correccion) y fase_posterior_iniciada
// 3: ventanas base_media e inferencia (D-014); la de inferencia con n_predicciones,
//    energia_por_prediccion_j, tiempo_por_prediccion_us y motivo_sin_por_prediccion
constexpr int VERSION_RESULTADOS = 3;

// Resultados derivados de la energia de una corrida a partir de los archivos del medidor que hay
// en su carpeta (energia_tramas.csv; si no existe, energia_atorch.csv). Ventanas, todas con el
// mismo metodo, umbral de huecos y cobertura:
//   "entrenamiento" [t_antes, t_despues]  (si ambos > 0)
//   "medicion"      [t_start, t_stop]     (si ambos > 0)
//   "base_pre"      linea base previa: [t_start, t_antes], o [t_start, t_stop] sin entrenamiento
//   "base_media"    linea base intermedia (D-014): [t_despues, t_inf_antes], si empezo
//   "inferencia"    [t_inf_antes, t_inf_despues], si empezo (D-014)
//   "base_post"     linea base posterior: [t_despues o t_inf_despues, t_stop], solo si empezo
// Las de linea base y la de inferencia siguen las reglas de FasesCorrida.h (las mismas que la
// columna 'fase' de la serie unida) y no existen si falta su fase. Su energia es en bruto: no se
// resta nada, porque la correccion por linea base no esta decidida (D-008).
// La ventana "inferencia" agrega, desde 'inferencia' (metadata.inferencia):
//   n_predicciones             el de la DLL (filas x pasadas completas)
//   energia_por_prediccion_j   energia_j / n_predicciones, solo si inferir devolvio ENERGIA_OK
//   tiempo_por_prediccion_us   copiado de metadata.inferencia (no se recalcula)
//   motivo_sin_por_prediccion  "inferencia cancelada" o "error de inferir" si los dos son null
// No escribe el archivo: devuelve el JSON. false si no hay datos del medidor que leer.
bool resultadosCorrida(const QString& dirCorrida, const fases::Marcas& marcas, QJsonObject* resultados, QString* error,
                       const QJsonObject& inferencia = QJsonObject());

} // namespace energia
