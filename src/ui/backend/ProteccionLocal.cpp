// ProteccionLocal.cpp - ver ProteccionLocal.h.
#include "ProteccionLocal.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace ProteccionLocal {

namespace {
// Entropia adicional: otra aplicacion del mismo usuario no descifra el token llamando a DPAPI
// sin saber este valor (no es secreto fuerte; solo separa los datos de EnergIA)
const char ENTROPIA[] = "EnergIA/sesion/v1";
}

#ifdef Q_OS_WIN

bool disponible() { return true; }

QString proteger(const QString& secreto)
{
    QByteArray claro = secreto.toUtf8();
    DATA_BLOB entrada{static_cast<DWORD>(claro.size()), reinterpret_cast<BYTE*>(claro.data())};
    DATA_BLOB entropia{static_cast<DWORD>(sizeof(ENTROPIA) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(ENTROPIA))};
    DATA_BLOB salida{};
    if (!CryptProtectData(&entrada, L"EnergIA sesion", &entropia, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &salida))
        return {};
    const QByteArray cifrado(reinterpret_cast<const char*>(salida.pbData), static_cast<int>(salida.cbData));
    LocalFree(salida.pbData);
    claro.fill('\0');
    return QString::fromLatin1(cifrado.toBase64());
}

QString desproteger(const QString& cifradoBase64)
{
    QByteArray cifrado = QByteArray::fromBase64(cifradoBase64.toLatin1());
    if (cifrado.isEmpty()) return {};
    DATA_BLOB entrada{static_cast<DWORD>(cifrado.size()), reinterpret_cast<BYTE*>(cifrado.data())};
    DATA_BLOB entropia{static_cast<DWORD>(sizeof(ENTROPIA) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(ENTROPIA))};
    DATA_BLOB salida{};
    if (!CryptUnprotectData(&entrada, nullptr, &entropia, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &salida))
        return {};
    const QString secreto = QString::fromUtf8(reinterpret_cast<const char*>(salida.pbData), static_cast<int>(salida.cbData));
    SecureZeroMemory(salida.pbData, salida.cbData);
    LocalFree(salida.pbData);
    return secreto;
}

#else

bool disponible() { return false; }
QString proteger(const QString&) { return {}; }
QString desproteger(const QString&) { return {}; }

#endif

} // namespace ProteccionLocal
