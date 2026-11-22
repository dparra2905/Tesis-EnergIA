// DatosCsv.h - Lectura del dataset (CSV numerico) y datos sinteticos de prueba.
//
// El CSV debe venir limpio: con encabezado, sin comprimir y con todas las columnas numericas.
// Acepta separador coma, punto y coma o tabulacion, coma decimal cuando el separador no es
// coma, BOM de Excel y campos entre comillas.
//
// Origen: detectarSeparador(), limpiarCampo(), leerEncabezado(), Trabajador::cargarCsv() y
// Trabajador::generarSinteticos() de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor
// David Parra). Adaptacion: funciones libres que reciben la ruta, el indice de la columna
// objetivo y la semilla en vez de la configuracion de la ventana.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace datos {

// Datos sinteticos de prueba (no son un dataset real)
constexpr int SINTETICO_FILAS = 1500;
constexpr int SINTETICO_COLS = 8;
constexpr int SINTETICO_CLASES = 3;

// Lee solo la primera linea: nombres de columna y separador
bool leerEncabezado(const QString& ruta, QStringList* columnas, char* separador, QString* error);

// Lee todas las filas. X queda por filas (n_filas x nCols, sin la columna objetivo). El resumen
// lleva las columnas de entrada y la huella del archivo: sha256 (calculado con los mismos bytes
// que se leen), tam_bytes y fecha_modificacion (UTC), tomados antes de leer.
bool cargarCsv(const QString& ruta, int columnaObjetivo, std::vector<double>* X, std::vector<double>* Y,
               int* nCols, QJsonObject* resumen, QString* error);

// Datos sinteticos reproducibles a partir de la semilla
void generarSinteticos(uint32_t semilla, std::vector<double>* X, std::vector<double>* Y, int* nCols,
                       QJsonObject* resumen);

} // namespace datos
