// Reloj.h - Reloj comun de EnergIA y formato de tiempo.
//
// Reloj de pared UTC en milisegundos (GetSystemTimePreciseAsFileTime en Windows) y texto
// ISO UTC con milisegundos (2026-09-22T00:52:45.334Z), el mismo formato que usan los medidores.
//
// Origen: ahoraMs(), isoUtc() y msHasta() de Back-EnergIA (orquestador.cpp, commit 1ab19f1,
// autor David Parra). Sin cambios de comportamiento.
#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace reloj {

qint64 ahoraMs();
QByteArray isoUtc(qint64 ms);
int msHasta(qint64 objetivoMs);

} // namespace reloj
