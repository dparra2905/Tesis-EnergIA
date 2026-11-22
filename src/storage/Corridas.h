// Corridas.h - Carpetas de las corridas y escritura de sus archivos JSON.
//
// Estructura: <dir_salida>/corridas/<experimento>/corrida_NNN/
// La raiz (<dir_salida>) la recibe el orquestador en config.json: no hay ruta global fija.
//
// Origen: siguienteNumeroCorrida() y la creacion de la carpeta en
// VentanaPrincipal::iniciarCorrida() de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor
// David Parra). Adaptacion: la raiz deja de ser la carpeta del ejecutable; se agrega la
// escritura atomica de JSON (archivo temporal + reemplazo).
#pragma once

#include <QJsonObject>
#include <QString>

namespace corridas {

constexpr const char* CARPETA_CORRIDAS = "corridas";

// Experimento -> nombre seguro para carpeta ([^A-Za-z0-9_-] -> _)
QString nombreSeguro(const QString& experimento);

// Siguiente numero libre en <dirExperimento> (corrida_001, corrida_002, ...)
int siguienteNumero(const QString& dirExperimento);

// Crea <dirSalida>/corridas/<experimento>/corrida_NNN. Devuelve la ruta y el numero.
bool crearCarpeta(const QString& dirSalida, const QString& experimento, QString* dirCorrida, int* numero,
                  QString* error);

// Escribe JSON con sangria de forma atomica (archivo .tmp + reemplazo)
bool escribirJson(const QString& ruta, const QJsonObject& objeto, QString* error = nullptr);

} // namespace corridas
