// test_supabase.cpp - Sesion de ClienteSupabase contra un servidor falso local (D-011).
//
// Login, error de credenciales, registro con confirmacion de correo, sesion guardada y
// restaurada, renovacion ante un 401, cierre de sesion y comportamiento sin red. No usa el
// proyecto real de Supabase.

#include "ClienteSupabase.h"
#include "ProteccionLocal.h"
#include "ServidorSupabaseFalso.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include <cstdio>
#include <functional>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

bool esperar(const std::function<bool()>& condicion, int limiteMs = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!condicion() && t.elapsed() < limiteMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return condicion();
}

ClienteSupabase::Opciones opciones(const QString& url, const QString& rutaSesion)
{
    ClienteSupabase::Opciones o;
    o.urlBase = url;
    o.llavePublica = "llave-publica-de-prueba";
    o.rutaSesion = rutaSesion;
    o.limiteAuthMs = 3000;
    o.limiteRestMs = 3000;
    return o;
}

// Puerto local sin nadie escuchando: simula la falta de red
QString urlSinServidor()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 puerto = s.serverPort();
    s.close();
    return QString("http://127.0.0.1:%1").arg(puerto);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString rutaSesion = tmp.filePath("datos/sesion.json");
    ServidorSupabaseFalso srv;

    std::printf("\n== 1. Login\n");
    {
        ClienteSupabase c(opciones(srv.url(), rutaSesion));
        comprobar(c.estado() == "sin_sesion" && !c.puedeEntrar(), "sin sesion al iniciar");
        c.iniciarSesion("ana@ejemplo.com", "mala");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.estado() == "sin_sesion", "contrasena incorrecta: sigue sin sesion");
        comprobar(c.error() == "Correo o contraseña incorrectos.", "mensaje de credenciales: " + c.error());
        comprobar(!c.errorDeRed(), "no es un error de red");

        c.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return !c.ocupado() && !c.rol().isEmpty(); });
        comprobar(c.estado() == "activa" && c.conectado() && c.puedeEntrar(), "login correcto: sesion activa");
        comprobar(c.usuarioId() == "u-ana" && c.correo() == "ana@ejemplo.com", "datos del usuario");
        comprobar(c.rol() == "admin" && c.esAdmin(), "rol leido de la tabla perfil");
        comprobar(c.error().isEmpty(), "sin error tras el login");
        const auto& login = srv.peticiones[srv.peticiones.size() - 2];
        comprobar(login.cabeceras.value("apikey") == "llave-publica-de-prueba", "Auth recibe la llave publica");
        comprobar(!login.cabeceras.contains("authorization"), "el login no envia Authorization");
        comprobar(srv.peticiones.last().cabeceras.value("authorization") == "Bearer " + srv.tokenAcceso.toUtf8(),
                  "REST usa el token del usuario");

        QFile f(rutaSesion);
        comprobar(f.open(QIODevice::ReadOnly), "sesion guardada en disco");
        const QByteArray crudo = f.readAll();
        const QJsonObject s = QJsonDocument::fromJson(crudo).object();
        comprobar(!s.contains("access_token"), "el token de acceso no se guarda en disco");
#ifdef Q_OS_WIN
        comprobar(!s.contains("refresh_token") && !crudo.contains(srv.tokenRenovacion.toUtf8()),
                  "el refresh_token NO esta en claro en el archivo");
        comprobar(ProteccionLocal::desproteger(s["refresh_token_dpapi"].toString()) == srv.tokenRenovacion,
                  "el refresh_token vigente se guarda cifrado con DPAPI");
        comprobar(ProteccionLocal::desproteger("bm8tZXMtZHBhcGk=").isEmpty(), "un dato que no es de DPAPI no se descifra");
#else
        comprobar(s["refresh_token"].toString() == srv.tokenRenovacion, "sin DPAPI: se guarda el refresh_token vigente");
#endif
    }

    std::printf("\n== 1b. Sesion guardada en claro (version anterior): se cifra al renovar\n");
    {
        ServidorSupabaseFalso srv1b;  // propio: renovar rota el refresh_token del servidor
        const QString rutaVieja = rutaSesion + ".vieja";
        QFile f(rutaVieja);
        comprobar(f.open(QIODevice::WriteOnly), "sesion en claro escrita");
        f.write(QJsonDocument(QJsonObject{{"refresh_token", srv1b.tokenRenovacion}, {"usuario_id", "u-ana"},
                                          {"correo", "ana@ejemplo.com"}}).toJson());
        f.close();
        ClienteSupabase c(opciones(srv1b.url(), rutaVieja));
        c.restaurarSesion();
        esperar([&] { return c.estado() != "verificando"; });
        comprobar(c.estado() == "activa", "la sesion en claro se restaura");
        comprobar(f.open(QIODevice::ReadOnly), "sesion reescrita");
        const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
#ifdef Q_OS_WIN
        comprobar(!s.contains("refresh_token") && s.contains("refresh_token_dpapi"), "y queda cifrada");
#endif
        QFile::remove(rutaVieja);
    }

    std::printf("\n== 2. Sesion restaurada y renovacion ante un 401\n");
    {
        ClienteSupabase c(opciones(srv.url(), rutaSesion));
        c.restaurarSesion();
        comprobar(c.estado() == "verificando" && !c.puedeEntrar(), "verificando la sesion guardada");
        esperar([&] { return c.estado() != "verificando"; });
        comprobar(c.estado() == "activa", "sesion restaurada con el refresh_token");
        comprobar(c.correo() == "ana@ejemplo.com", "usuario restaurado");

        const int renovaciones = srv.contar("/auth/v1/token");
        srv.revocarAcceso();
        bool listo = false;
        ClienteSupabase::Resultado res;
        c.subirCorrida(QJsonObject{{"experimento", QJsonObject{{"nombre", "e1"}}},
                                   {"corrida", QJsonObject{{"equipo", "PC"}, {"numero", 1}, {"t_creacion", "2026-10-01T10:00:00.000Z"}}}},
                       [&](const ClienteSupabase::Resultado& r) { res = r; listo = true; });
        esperar([&] { return listo; });
        comprobar(res.ok() && res.crudo.trimmed() == "1",
                  "subir_corrida responde el id tras renovar el token (http " + QString::number(res.http) + ")");
        comprobar(srv.contar("/auth/v1/token") == renovaciones + 1, "un 401 provoca una sola renovacion");

        listo = false;
        c.subirCorrida(QJsonObject{{"experimento", QJsonObject{{"nombre", "e1"}}},
                                   {"corrida", QJsonObject{{"equipo", "PC"}, {"numero", 1}, {"t_creacion", "2026-10-01T10:00:00.000Z"}}}},
                       [&](const ClienteSupabase::Resultado& r) { res = r; listo = true; });
        esperar([&] { return listo; });
        comprobar(res.http == 409 && !res.red, "la misma corrida otra vez: 409 (ya subida)");
    }

    std::printf("\n== 3. Sesion guardada sin red\n");
    {
        ClienteSupabase c(opciones(urlSinServidor(), rutaSesion));
        c.restaurarSesion();
        esperar([&] { return c.estado() != "verificando"; });
        comprobar(c.estado() == "sin_conexion", "sin red: estado sin_conexion");
        comprobar(c.puedeEntrar() && !c.conectado(), "se puede entrar a la aplicacion, sin hablar con la base");
        comprobar(QFile::exists(rutaSesion), "la sesion guardada se conserva");
        bool listo = false;
        ClienteSupabase::Resultado res;
        c.rest("GET", "corrida", {}, {}, [&](const ClienteSupabase::Resultado& r) { res = r; listo = true; });
        esperar([&] { return listo; });
        comprobar(res.red && !res.ok(), "una llamada REST sin red se marca como error de red");
    }

    std::printf("\n== 4. Sesion revocada y cierre de sesion\n");
    {
        ClienteSupabase c(opciones(srv.url(), rutaSesion));
        c.restaurarSesion();
        esperar([&] { return c.estado() != "verificando"; });
        comprobar(c.estado() == "activa", "sesion restaurada otra vez");
        c.cerrarSesion();
        comprobar(c.estado() == "sin_sesion" && c.usuarioId().isEmpty(), "cerrar sesion la olvida");
        comprobar(!QFile::exists(rutaSesion), "cerrar sesion borra la sesion guardada");
        esperar([&] { return srv.contar("/auth/v1/logout") == 1; });
        comprobar(srv.contar("/auth/v1/logout") == 1, "se avisa al servidor (logout)");

        // Un refresh_token que el servidor ya no acepta
        QFile f(rutaSesion);
        comprobar(f.open(QIODevice::WriteOnly), "se escribe una sesion vieja");
        f.write(R"({"refresh_token": "viejo", "usuario_id": "u-ana", "correo": "ana@ejemplo.com"})");
        f.close();
        c.restaurarSesion();
        esperar([&] { return c.estado() != "verificando"; });
        comprobar(c.estado() == "sin_sesion", "refresh_token rechazado: hay que iniciar sesion");
        comprobar(!c.error().isEmpty() && !QFile::exists(rutaSesion), "se avisa y se borra la sesion invalida");
    }

    std::printf("\n== 5. Registro\n");
    {
        ClienteSupabase c(opciones(srv.url(), rutaSesion));
        c.registrar("nuevo@ejemplo.com", "123", "Nuevo");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.error().contains("débil"), "contrasena debil: " + c.error());
        c.registrar("nuevo@ejemplo.com", "clave-larga", "Nuevo");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.estado() == "sin_sesion" && c.aviso().contains("confirmación"),
                  "con confirmacion de correo: aviso y sin sesion (" + c.aviso() + ")");
        const QJsonObject cuerpo = QJsonDocument::fromJson(srv.peticiones.last().cuerpo).object();
        comprobar(cuerpo["data"].toObject()["nombre"].toString() == "Nuevo", "el nombre va en los metadatos del registro");
    }

    std::printf("\n== 6. Sin red y sin sesion guardada\n");
    {
        ClienteSupabase c(opciones(urlSinServidor(), rutaSesion));
        c.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.errorDeRed() && !c.puedeEntrar(), "login sin red: error de red (" + c.error() + ")");
        c.continuarSinConexion();
        comprobar(c.estado() == "local" && c.puedeEntrar() && !c.conectado(), "continuar sin conexion");
    }

    std::printf("\n== 7. Primera conexion fallida (DNS en frio / conexion rechazada)\n");
    {
        QTcpServer reserva;
        reserva.listen(QHostAddress::LocalHost, 0);
        const quint16 puerto = reserva.serverPort();
        reserva.close();
        QTemporaryDir tmp7;
        ClienteSupabase c(opciones(QString("http://127.0.0.1:%1").arg(puerto), tmp7.filePath("sesion.json")));
        std::unique_ptr<ServidorSupabaseFalso> tarde;
        // El servidor aparece despues del primer intento: el cliente debe reintentar solo
        QTimer::singleShot(300, [&] { tarde = std::make_unique<ServidorSupabaseFalso>(nullptr, puerto); });
        c.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.estado() == "activa" && c.error().isEmpty(),
                  "el login entra sin mostrar error de conexion (" + c.error() + ")");
        comprobar(tarde && tarde->contar("/auth/v1/token") == 1, "una sola peticion de login llego al servidor");
    }

    std::printf("\n== 8. Cuenta: nombre, contrasena y recuperacion por codigo\n");
    {
        ServidorSupabaseFalso srv8;
        QTemporaryDir tmp8;
        ClienteSupabase c(opciones(srv8.url(), tmp8.filePath("sesion.json")));
        bool ok = false;
        QString mensaje = "sin respuesta";
        QObject::connect(&c, &ClienteSupabase::cuentaActualizada, [&](bool o, const QString& m) { ok = o; mensaje = m; });
        c.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return c.conectado() && !c.rol().isEmpty(); });

        c.cambiarNombre("  Ana Pérez  ");
        esperar([&] { return mensaje != "sin respuesta"; });
        comprobar(ok && c.nombre() == "Ana Pérez" && srv8.nombrePerfil == "Ana Pérez", "cambiar el nombre: " + mensaje);
        mensaje = "sin respuesta";
        c.cambiarNombre("   ");
        comprobar(!ok && mensaje.contains("entre 1 y 80"), "nombre vacio rechazado sin llamar a la base");

        mensaje = "sin respuesta";
        c.cambiarContrasena("equivocada", "nueva-clave");
        esperar([&] { return mensaje != "sin respuesta"; });
        comprobar(!ok && mensaje == "La contraseña actual no es correcta.", "contrasena actual incorrecta: " + mensaje);
        mensaje = "sin respuesta";
        c.cambiarContrasena("secreta1", "nueva-clave");
        esperar([&] { return mensaje != "sin respuesta"; });
        comprobar(ok && srv8.contrasena == "nueva-clave", "cambiar la contrasena: " + mensaje);
        comprobar(c.conectado(), "la sesion sigue activa tras el cambio");

        c.cerrarSesion();
        QString codigoPara;
        QObject::connect(&c, &ClienteSupabase::codigoEnviado, [&](const QString& correo) { codigoPara = correo; });
        c.solicitarRecuperacion("ana@ejemplo.com");
        esperar([&] { return !c.ocupado(); });
        comprobar(codigoPara == "ana@ejemplo.com" && c.aviso().contains("código"), "codigo de recuperacion pedido: " + c.aviso());
        c.restablecerContrasena("ana@ejemplo.com", "000000", "otra-clave");
        esperar([&] { return !c.ocupado(); });
        comprobar(!c.puedeEntrar() && c.error().contains("no es válido"), "codigo incorrecto: " + c.error());
        c.restablecerContrasena("ana@ejemplo.com", "123456", "otra-clave");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.estado() == "activa" && srv8.contrasena == "otra-clave", "codigo correcto: contrasena nueva y sesion iniciada");
        c.cerrarSesion();
        c.iniciarSesion("ana@ejemplo.com", "otra-clave");
        esperar([&] { return !c.ocupado(); });
        comprobar(c.estado() == "activa", "se entra con la contrasena nueva");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "SUPABASE OK" : "SUPABASE CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
