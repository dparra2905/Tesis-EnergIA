// ProteccionLocal.h - Cifrado de secretos guardados en disco para el usuario actual (D-011).
//
// En Windows usa DPAPI (CryptProtectData / CryptUnprotectData): el resultado solo lo puede
// descifrar la misma cuenta de Windows en el mismo equipo. Copiar el archivo a otro equipo u
// otra cuenta no sirve para recuperar el secreto. Se usa para el refresh_token de la sesion
// de la base de datos (sesion.json).
//
// En otros sistemas no hay un equivalente en Qt Core: disponible() devuelve false y el
// llamador decide (ClienteSupabase guarda el token en claro, como antes, con un aviso).
#pragma once

#include <QByteArray>
#include <QString>

namespace ProteccionLocal {

bool disponible();
// Texto cifrado en base64 ("" si fallo)
QString proteger(const QString& secreto);
// Secreto descifrado; "" si no se pudo (otro usuario/equipo, archivo alterado)
QString desproteger(const QString& cifradoBase64);

} // namespace ProteccionLocal
