// ZipSimple.cpp - ver ZipSimple.h.
#include "ZipSimple.h"

#include <QDir>
#include <QSaveFile>

#include <array>
#include <limits>

namespace zip {

namespace {

constexpr quint32 FIRMA_LOCAL = 0x04034b50;
constexpr quint32 FIRMA_CENTRAL = 0x02014b50;
constexpr quint32 FIRMA_FIN = 0x06054b50;
constexpr quint16 VERSION = 20;              // 2.0: lo minimo para entradas "stored"
constexpr quint16 BANDERA_UTF8 = 0x0800;     // bit 11: nombre en UTF-8
constexpr quint64 MAXIMO = std::numeric_limits<quint32>::max();

void u16(QByteArray& b, quint16 v)
{
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
}

void u32(QByteArray& b, quint32 v)
{
    u16(b, static_cast<quint16>(v & 0xffff));
    u16(b, static_cast<quint16>(v >> 16));
}

// Fecha y hora en formato MS-DOS (desde 1980, segundos de 2 en 2)
void fechaDos(const QDateTime& f, quint16* hora, quint16* dia)
{
    const QDateTime l = f.isValid() && f.date().year() >= 1980 ? f.toLocalTime() : QDateTime(QDate(1980, 1, 1), QTime(0, 0));
    *hora = static_cast<quint16>((l.time().hour() << 11) | (l.time().minute() << 5) | (l.time().second() / 2));
    *dia = static_cast<quint16>(((l.date().year() - 1980) << 9) | (l.date().month() << 5) | l.date().day());
}

} // namespace

quint32 crc32(const QByteArray& datos)
{
    static const std::array<quint32, 256> tabla = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : datos) c = tabla[(c ^ static_cast<quint8>(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool escribir(const QString& destino, const QList<QPair<QString, QByteArray>>& entradas, const QDateTime& fecha,
              QString* error)
{
    quint16 hora = 0, dia = 0;
    fechaDos(fecha, &hora, &dia);
    QByteArray archivo, central;
    for (const auto& [nombre, datos] : entradas) {
        const QByteArray n = nombre.toUtf8();
        if (static_cast<quint64>(datos.size()) >= MAXIMO || static_cast<quint64>(archivo.size()) + datos.size() >= MAXIMO) {
            *error = "El archivo supera los 4 GB que admite el formato ZIP sin ZIP64: " + nombre;
            return false;
        }
        const quint32 crc = crc32(datos);
        const quint32 tam = static_cast<quint32>(datos.size());
        const quint32 desplazamiento = static_cast<quint32>(archivo.size());

        u32(archivo, FIRMA_LOCAL);
        u16(archivo, VERSION);
        u16(archivo, BANDERA_UTF8);
        u16(archivo, 0);  // metodo: stored
        u16(archivo, hora);
        u16(archivo, dia);
        u32(archivo, crc);
        u32(archivo, tam);  // comprimido
        u32(archivo, tam);  // original
        u16(archivo, static_cast<quint16>(n.size()));
        u16(archivo, 0);  // extra
        archivo.append(n);
        archivo.append(datos);

        u32(central, FIRMA_CENTRAL);
        u16(central, VERSION);  // hecho por
        u16(central, VERSION);  // necesario para extraer
        u16(central, BANDERA_UTF8);
        u16(central, 0);
        u16(central, hora);
        u16(central, dia);
        u32(central, crc);
        u32(central, tam);
        u32(central, tam);
        u16(central, static_cast<quint16>(n.size()));
        u16(central, 0);  // extra
        u16(central, 0);  // comentario
        u16(central, 0);  // disco
        u16(central, 0);  // atributos internos
        u32(central, 0);  // atributos externos
        u32(central, desplazamiento);
        central.append(n);
    }
    QByteArray fin;
    u32(fin, FIRMA_FIN);
    u16(fin, 0);  // este disco
    u16(fin, 0);  // disco del directorio central
    u16(fin, static_cast<quint16>(entradas.size()));
    u16(fin, static_cast<quint16>(entradas.size()));
    u32(fin, static_cast<quint32>(central.size()));
    u32(fin, static_cast<quint32>(archivo.size()));
    u16(fin, 0);  // comentario

    QSaveFile f(destino);
    if (!f.open(QIODevice::WriteOnly) || f.write(archivo) < 0 || f.write(central) < 0 || f.write(fin) < 0 || !f.commit()) {
        *error = "No se pudo escribir " + QDir::toNativeSeparators(destino) + ": " + f.errorString();
        return false;
    }
    return true;
}

} // namespace zip
