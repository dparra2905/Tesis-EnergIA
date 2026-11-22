// ZipSimple.h - Escritor minimo de archivos .zip (D-013, exportacion del modelo).
//
// Qt no trae un escritor publico de .zip. Este escribe el formato ZIP basico (PKWARE APPNOTE):
// entradas sin comprimir (metodo 0, "stored"), nombres en UTF-8 (bit 11) y CRC-32. Sin ZIP64:
// cada archivo y el total deben ocupar menos de 4 GB. Los modelos y sus descripciones son
// pequenos; no se comprime para no depender de zlib. Cualquier descompresor (Explorador de
// Windows, 7-Zip, unzip, Python zipfile) lo abre.
//
// Codigo nuevo de EnergIA. Solo Qt Core.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

namespace zip {

// CRC-32 (polinomio 0xEDB88320), el que usa ZIP
quint32 crc32(const QByteArray& datos);

// Escribe 'destino' con las entradas {nombre dentro del .zip, contenido}, en ese orden, de forma
// atomica. 'fecha' es la fecha de modificacion de las entradas (hora local, resolucion de 2 s).
// false (con el motivo) si no se pudo escribir o algo supera los 4 GB.
bool escribir(const QString& destino, const QList<QPair<QString, QByteArray>>& entradas, const QDateTime& fecha,
              QString* error);

} // namespace zip
