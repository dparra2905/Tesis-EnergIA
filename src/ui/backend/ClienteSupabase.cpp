// ClienteSupabase.cpp - ver ClienteSupabase.h.
#include "ClienteSupabase.h"

#include "ProteccionLocal.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <utility>

namespace {

// Se renueva el token de acceso este tiempo antes de que venza
constexpr qint64 MARGEN_RENOVACION_MS = 120000;

qint64 ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

ClienteSupabase::ClienteSupabase(const Opciones& opciones, QObject* parent)
    : QObject(parent), op_(opciones), red_(new QNetworkAccessManager(this))
{
    while (op_.urlBase.endsWith('/')) op_.urlBase.chop(1);
    // Resuelve el DNS y abre la conexion TLS mientras el usuario escribe sus datos: el primer
    // login no paga ese tiempo ni el fallo de DNS en frio (ver enviar())
    const QUrl base(op_.urlBase);
    if (base.scheme() == "https") red_->connectToHostEncrypted(base.host(), static_cast<quint16>(base.port(443)));
    timerRenovacion_.setSingleShot(true);
    connect(&timerRenovacion_, &QTimer::timeout, this, [this] { renovar([](bool) {}); });
    timerSinConexion_.setInterval(op_.reintentoSinConexionMs);
    connect(&timerSinConexion_, &QTimer::timeout, this, [this] {
        if (estado_ == "sin_conexion") renovar([](bool) {});
        else timerSinConexion_.stop();
    });
}

ClienteSupabase::~ClienteSupabase() = default;

// ---------------------------------------------------------------- sesion

void ClienteSupabase::restaurarSesion()
{
    QFile f(op_.rutaSesion);
    if (op_.rutaSesion.isEmpty() || !f.open(QIODevice::ReadOnly)) return;
    const QJsonObject s = QJsonDocument::fromJson(f.readAll()).object();
    // Cifrado con DPAPI (vigente) o en claro (archivos anteriores; se cifra al renovar)
    const QString token = s.contains("refresh_token_dpapi")
        ? ProteccionLocal::desproteger(s["refresh_token_dpapi"].toString())
        : s["refresh_token"].toString();
    if (token.isEmpty()) {
        // Otro usuario de Windows u otro equipo: el token no se puede usar
        if (s.contains("refresh_token_dpapi")) borrarSesionGuardada();
        return;
    }
    tokenRenovacion_ = token;
    usuarioId_ = s["usuario_id"].toString();
    correo_ = s["correo"].toString();
    nombre_ = s["nombre"].toString();
    rol_ = s["rol"].toString();
    ponerEstado("verificando");
    renovar([](bool) {});
}

void ClienteSupabase::iniciarSesion(const QString& correo, const QString& contrasena)
{
    if (ocupado_) return;
    if (correo.trimmed().isEmpty() || contrasena.isEmpty()) {
        ponerMensajes("Escribe el correo y la contraseña.", {});
        return;
    }
    ponerMensajes({}, {});
    ponerOcupado(true);
    llamarAuth("/auth/v1/token?grant_type=password",
               QJsonObject{{"email", correo.trimmed()}, {"password", contrasena}}, {},
               [this](const Resultado& r) {
                   ponerOcupado(false);
                   if (!r.ok()) {
                       ponerMensajes(r.error, {}, r.red);
                       return;
                   }
                   aplicarSesion(r.cuerpo.object());
               });
}

void ClienteSupabase::registrar(const QString& correo, const QString& contrasena, const QString& nombre)
{
    if (ocupado_) return;
    if (correo.trimmed().isEmpty() || contrasena.isEmpty()) {
        ponerMensajes("Escribe el correo y la contraseña.", {});
        return;
    }
    ponerMensajes({}, {});
    ponerOcupado(true);
    QJsonObject cuerpo{{"email", correo.trimmed()}, {"password", contrasena}};
    // El trigger crear_perfil toma el nombre de raw_user_meta_data (si no, usa el correo)
    if (!nombre.trimmed().isEmpty()) cuerpo["data"] = QJsonObject{{"nombre", nombre.trimmed()}};
    llamarAuth("/auth/v1/signup", cuerpo, {}, [this, correo](const Resultado& r) {
        ponerOcupado(false);
        if (!r.ok()) {
            ponerMensajes(r.error, {}, r.red);
            return;
        }
        const QJsonObject o = r.cuerpo.object();
        if (!o["access_token"].toString().isEmpty()) {
            // Proyecto sin confirmacion de correo: el registro ya deja sesion
            aplicarSesion(o);
            return;
        }
        ponerMensajes({}, QString("Cuenta creada. Revisa %1 y abre el enlace de confirmación; "
                                  "después inicia sesión.").arg(correo.trimmed()));
    });
}

void ClienteSupabase::cerrarSesion()
{
    const QByteArray token = tokenVigente() ? tokenAcceso_ : QByteArray();
    if (!token.isEmpty()) {
        // Revoca el refresh_token en el servidor. Si falla (sin red), igual se olvida localmente.
        llamarAuth("/auth/v1/logout", {}, token, [](const Resultado&) {});
    }
    borrarSesionGuardada();
    olvidarSesion();
    ponerMensajes({}, {});
    emit sesionCerrada();
}

void ClienteSupabase::continuarSinConexion()
{
    if (puedeEntrar()) return;
    olvidarSesion();
    ponerEstado("local");
}

void ClienteSupabase::limpiarMensajes() { ponerMensajes({}, {}); }

// ---------------------------------------------------------------- cuenta

void ClienteSupabase::cambiarNombre(const QString& nombre)
{
    const QString n = nombre.trimmed();
    if (n.isEmpty() || n.size() > 80) {
        emit cuentaActualizada(false, "El nombre debe tener entre 1 y 80 caracteres.");
        return;
    }
    QUrlQuery q;
    q.addQueryItem("id", "eq." + usuarioId_);
    q.addQueryItem("select", "nombre");
    rest("PATCH", "perfil", q, QJsonDocument(QJsonObject{{"nombre", n}}).toJson(QJsonDocument::Compact),
         [this, n](const Resultado& r) {
             if (!r.ok() || r.cuerpo.array().isEmpty()) {
                 emit cuentaActualizada(false, r.ok() ? QString("No se pudo cambiar el nombre.") : r.error);
                 return;
             }
             nombre_ = n;
             guardarSesion();
             emit sesionCambiada();
             emit cuentaActualizada(true, "Nombre actualizado.");
         },
         {{"Prefer", "return=representation"}});
}

void ClienteSupabase::fijarContrasena(const QString& nueva, std::function<void(const Resultado&)> alTerminar)
{
    enviar("PUT", QUrl(op_.urlBase + "/auth/v1/user"), tokenAcceso_,
           QJsonDocument(QJsonObject{{"password", nueva}}).toJson(QJsonDocument::Compact), op_.limiteAuthMs, {},
           std::move(alTerminar));
}

void ClienteSupabase::cambiarContrasena(const QString& actual, const QString& nueva)
{
    if (!conectado()) {
        emit cuentaActualizada(false, "Hace falta conexión con la base de datos.");
        return;
    }
    if (nueva.size() < 6) {
        emit cuentaActualizada(false, "La contraseña nueva debe tener al menos 6 caracteres.");
        return;
    }
    // 1. Verificar la actual con un login nuevo (la sesion queda renovada)
    llamarAuth("/auth/v1/token?grant_type=password", QJsonObject{{"email", correo_}, {"password", actual}}, {},
               [this, nueva](const Resultado& r) {
                   if (!r.ok()) {
                       emit cuentaActualizada(false, r.http == 400 ? QString("La contraseña actual no es correcta.") : r.error);
                       return;
                   }
                   aplicarSesion(r.cuerpo.object());
                   // 2. Fijar la nueva
                   fijarContrasena(nueva, [this](const Resultado& r2) {
                       emit cuentaActualizada(r2.ok(), r2.ok() ? QString("Contraseña cambiada.") : r2.error);
                   });
               });
}

void ClienteSupabase::solicitarRecuperacion(const QString& correo)
{
    if (ocupado_) return;
    if (correo.trimmed().isEmpty()) {
        ponerMensajes("Escribe el correo de la cuenta.", {});
        return;
    }
    ponerMensajes({}, {});
    ponerOcupado(true);
    const QString c = correo.trimmed();
    llamarAuth("/auth/v1/recover", QJsonObject{{"email", c}}, {}, [this, c](const Resultado& r) {
        ponerOcupado(false);
        if (!r.ok()) {
            ponerMensajes(r.error, {}, r.red);
            return;
        }
        // Supabase responde igual exista o no la cuenta (no revela que correos estan registrados)
        ponerMensajes({}, QString("Si %1 tiene una cuenta, te llegó un código de 6 dígitos. Escríbelo abajo.").arg(c));
        emit codigoEnviado(c);
    });
}

void ClienteSupabase::restablecerContrasena(const QString& correo, const QString& codigo, const QString& nueva)
{
    if (ocupado_) return;
    if (codigo.trimmed().isEmpty() || nueva.size() < 6) {
        ponerMensajes(codigo.trimmed().isEmpty() ? QString("Escribe el código que te llegó por correo.")
                                                 : QString("La contraseña nueva debe tener al menos 6 caracteres."), {});
        return;
    }
    ponerMensajes({}, {});
    ponerOcupado(true);
    llamarAuth("/auth/v1/verify",
               QJsonObject{{"type", "recovery"}, {"email", correo.trimmed()}, {"token", codigo.trimmed()}}, {},
               [this, nueva](const Resultado& r) {
                   if (!r.ok() || r.cuerpo.object()["access_token"].toString().isEmpty()) {
                       ponerOcupado(false);
                       ponerMensajes(r.ok() ? QString("La base de datos no devolvió una sesión.") : r.error, {}, r.red);
                       return;
                   }
                   // La sesion se aplica despues de fijar la contrasena: asi la pantalla de
                   // acceso no da paso a la aplicacion con la contrasena a medio cambiar
                   const QJsonObject sesion = r.cuerpo.object();
                   tokenAcceso_ = sesion["access_token"].toString().toUtf8();
                   fijarContrasena(nueva, [this, sesion](const Resultado& r2) {
                       ponerOcupado(false);
                       if (!r2.ok()) {
                           tokenAcceso_.clear();
                           ponerMensajes(r2.error, {}, r2.red);
                           return;
                       }
                       aplicarSesion(sesion);
                   });
               });
}

void ClienteSupabase::aplicarSesion(const QJsonObject& s)
{
    tokenAcceso_ = s["access_token"].toString().toUtf8();
    tokenRenovacion_ = s["refresh_token"].toString();
    const qint64 expiraEn = s["expires_in"].toInteger(3600);
    venceEnMs_ = s["expires_at"].isDouble() ? s["expires_at"].toInteger() * 1000 : ahoraMs() + expiraEn * 1000;
    const QJsonObject u = s["user"].toObject();
    if (!u.isEmpty()) {
        const QString id = u["id"].toString();
        if (id != usuarioId_) rol_.clear();  // otro usuario: el rol se vuelve a leer
        usuarioId_ = id;
        correo_ = u["email"].toString();
        const QString n = u["user_metadata"].toObject()["nombre"].toString();
        if (!n.isEmpty()) nombre_ = n;
    }
    // Se renueva un poco antes de vencer (nunca antes de 10 s, por si el servidor da plazos cortos)
    timerRenovacion_.start(static_cast<int>(std::clamp<qint64>(venceEnMs_ - ahoraMs() - MARGEN_RENOVACION_MS,
                                                               10000, 24LL * 3600 * 1000)));
    timerSinConexion_.stop();
    guardarSesion();
    ponerMensajes({}, {});
    ponerEstado("activa");
    emit sesionActivada();
    cargarPerfil();
}

void ClienteSupabase::cargarPerfil()
{
    QUrlQuery q;
    q.addQueryItem("select", "nombre,rol");
    q.addQueryItem("id", "eq." + usuarioId_);
    rest("GET", "perfil", q, {}, [this](const Resultado& r) {
        if (!r.ok()) return;
        const QJsonArray filas = r.cuerpo.array();
        if (filas.isEmpty()) return;
        const QJsonObject p = filas.first().toObject();
        nombre_ = p["nombre"].toString(nombre_);
        rol_ = p["rol"].toString();
        guardarSesion();
        emit sesionCambiada();
    });
}

void ClienteSupabase::renovar(std::function<void(bool ok)> alTerminar)
{
    esperandoRenovacion_.append(std::move(alTerminar));
    if (renovando_) return;
    if (tokenRenovacion_.isEmpty()) {
        const auto pendientes = std::exchange(esperandoRenovacion_, {});
        for (const auto& f : pendientes) f(false);
        return;
    }
    renovando_ = true;
    llamarAuth("/auth/v1/token?grant_type=refresh_token", QJsonObject{{"refresh_token", tokenRenovacion_}}, {},
               [this](const Resultado& r) {
                   renovando_ = false;
                   bool ok = false;
                   if (r.ok()) {
                       aplicarSesion(r.cuerpo.object());
                       ok = true;
                   } else if (r.red || r.http >= 500) {
                       // Sin red: se conserva la sesion guardada y se reintenta mas tarde
                       tokenAcceso_.clear();
                       ponerEstado("sin_conexion");
                       if (!timerSinConexion_.isActive()) timerSinConexion_.start();
                   } else {
                       // Token revocado o vencido: hay que volver a iniciar sesion
                       borrarSesionGuardada();
                       olvidarSesion();
                       ponerMensajes("La sesión guardada ya no es válida. Inicia sesión de nuevo.", {});
                   }
                   const auto pendientes = std::exchange(esperandoRenovacion_, {});
                   for (const auto& f : pendientes) f(ok);
               });
}

bool ClienteSupabase::tokenVigente() const
{
    return !tokenAcceso_.isEmpty() && ahoraMs() < venceEnMs_ - 30000;
}

void ClienteSupabase::guardarSesion() const
{
    if (op_.rutaSesion.isEmpty() || tokenRenovacion_.isEmpty()) return;
    QDir().mkpath(QFileInfo(op_.rutaSesion).absolutePath());
    QSaveFile f(op_.rutaSesion);
    if (!f.open(QIODevice::WriteOnly)) return;
    QJsonObject s{{"usuario_id", usuarioId_}, {"correo", correo_}, {"nombre", nombre_}, {"rol", rol_},
                  {"url_base", op_.urlBase}};
    // El refresh_token da acceso a la cuenta: en Windows se guarda cifrado con DPAPI (solo lo
    // descifra esta cuenta de Windows en este equipo). Sin DPAPI se guarda en claro.
    const QString cifrado = ProteccionLocal::disponible() ? ProteccionLocal::proteger(tokenRenovacion_) : QString();
    if (!cifrado.isEmpty()) s["refresh_token_dpapi"] = cifrado;
    else if (!ProteccionLocal::disponible()) s["refresh_token"] = tokenRenovacion_;
    else return;  // DPAPI fallo: mejor no guardar la sesion que guardarla en claro
    f.write(QJsonDocument(s).toJson());
    f.commit();
}

void ClienteSupabase::borrarSesionGuardada() const
{
    if (!op_.rutaSesion.isEmpty()) QFile::remove(op_.rutaSesion);
}

void ClienteSupabase::olvidarSesion()
{
    timerRenovacion_.stop();
    timerSinConexion_.stop();
    tokenAcceso_.clear();
    tokenRenovacion_.clear();
    venceEnMs_ = 0;
    usuarioId_.clear();
    correo_.clear();
    nombre_.clear();
    rol_.clear();
    ponerEstado("sin_sesion");
    emit sesionCambiada();  // los datos del usuario cambian aunque el estado no cambie
}

void ClienteSupabase::ponerEstado(const QString& estado)
{
    if (estado_ == estado) return;
    estado_ = estado;
    emit sesionCambiada();
}

void ClienteSupabase::ponerOcupado(bool ocupado)
{
    if (ocupado_ == ocupado) return;
    ocupado_ = ocupado;
    emit ocupadoCambiado();
}

void ClienteSupabase::ponerMensajes(const QString& error, const QString& aviso, bool deRed)
{
    if (error_ == error && aviso_ == aviso && errorDeRed_ == deRed) return;
    error_ = error;
    aviso_ = aviso;
    errorDeRed_ = deRed;
    emit mensajeCambiado();
}

// ---------------------------------------------------------------- llamadas

void ClienteSupabase::rest(const QByteArray& metodo, const QString& ruta, const QUrlQuery& consulta,
                           const QByteArray& cuerpo, AlResponder alResponder,
                           const QList<QPair<QByteArray, QByteArray>>& cabeceras)
{
    QUrl url(op_.urlBase + "/rest/v1/" + ruta);
    url.setQuery(consulta);

    // Envia con el token vigente; ante un 401 renueva y reintenta una sola vez
    // (la funcion se guarda a si misma como weak_ptr; la mantiene viva la respuesta pendiente)
    auto intentar = std::make_shared<std::function<void(bool)>>();
    std::weak_ptr<std::function<void(bool)>> debil = intentar;
    *intentar = [this, metodo, url, cuerpo, alResponder, cabeceras, debil](bool esReintento) {
        auto intentar = debil.lock();
        enviar(metodo, url, tokenAcceso_, cuerpo, op_.limiteRestMs, cabeceras,
               [this, alResponder, intentar, esReintento](const Resultado& r) {
                   if (r.http == 401 && !esReintento) {
                       tokenAcceso_.clear();
                       renovar([alResponder, intentar, r](bool ok) {
                           if (ok) (*intentar)(true);
                           else alResponder(r);
                       });
                       return;
                   }
                   alResponder(r);
               });
    };

    if (tokenVigente()) {
        (*intentar)(false);
        return;
    }
    renovar([this, intentar, alResponder](bool ok) {
        if (ok) {
            (*intentar)(false);
            return;
        }
        Resultado r;
        r.red = (estado_ == "sin_conexion");
        r.error = r.red ? QString("Sin conexión con la base de datos.") : QString("No hay una sesión iniciada.");
        alResponder(r);
    });
}

void ClienteSupabase::subirCorrida(const QJsonObject& paquete, AlResponder alResponder)
{
    const QByteArray cuerpo = QJsonDocument(QJsonObject{{"p", paquete}}).toJson(QJsonDocument::Compact);
    rest("POST", "rpc/subir_corrida", {}, cuerpo, std::move(alResponder));
}

void ClienteSupabase::llamarAuth(const QString& ruta, const QJsonObject& cuerpo, const QByteArray& token,
                                 AlResponder alResponder)
{
    enviar("POST", QUrl(op_.urlBase + ruta), token, QJsonDocument(cuerpo).toJson(QJsonDocument::Compact),
           op_.limiteAuthMs, {}, std::move(alResponder));
}

void ClienteSupabase::enviar(const QByteArray& metodo, const QUrl& url, const QByteArray& token,
                             const QByteArray& cuerpo, int limiteMs,
                             const QList<QPair<QByteArray, QByteArray>>& cabeceras, AlResponder alResponder,
                             int intento)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(limiteMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("apikey", op_.llavePublica.toUtf8());
    // Con las llaves sb_publishable_ el Authorization lleva solo el token del usuario
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    for (const auto& c : cabeceras) req.setRawHeader(c.first, c.second);

    QNetworkReply* reply = red_->sendCustomRequest(req, metodo, cuerpo);
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        reply->deleteLater();
        Resultado r;
        const QVariant http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        // La primera resolucion DNS del proyecto (cache del sistema vacia) puede fallar en unos
        // ms con "Host ... not found" aunque haya red. En estos errores la peticion no salio del
        // equipo, asi que reintentar no duplica nada (tampoco un login o una subida).
        const auto e = reply->error();
        const bool sinEnviar = e == QNetworkReply::HostNotFoundError || e == QNetworkReply::ConnectionRefusedError
                            || e == QNetworkReply::TemporaryNetworkFailureError || e == QNetworkReply::NetworkSessionFailedError;
        if (!http.isValid() && sinEnviar && intento < INTENTOS_RED) {
            QTimer::singleShot(400 * intento, this, [=, this] {
                enviar(metodo, url, token, cuerpo, limiteMs, cabeceras, alResponder, intento + 1);
            });
            return;
        }
        const QByteArray datos = reply->readAll();
        r.crudo = datos;
        r.contentRange = reply->rawHeader("Content-Range");
        if (!http.isValid()) {
            r.red = true;
            r.error = (reply->error() == QNetworkReply::OperationCanceledError
                       || reply->error() == QNetworkReply::TimeoutError)
                ? QString("La base de datos no respondió a tiempo.")
                : "Sin conexión con la base de datos (" + reply->errorString() + ").";
        } else {
            r.http = http.toInt();
            r.cuerpo = QJsonDocument::fromJson(datos);
            if (!r.ok()) r.error = textoError(r.http, r.cuerpo);
        }
        alResponder(r);
    });
}

QString ClienteSupabase::textoError(int http, const QJsonDocument& cuerpo)
{
    const QJsonObject o = cuerpo.object();
    const QString codigo = o["error_code"].toString(o["error"].toString());
    // Auth (GoTrue)
    if (codigo == "invalid_credentials" || codigo == "invalid_grant") {
        const QString d = o["error_description"].toString(o["msg"].toString());
        if (d.contains("refresh", Qt::CaseInsensitive)) return "La sesión venció. Inicia sesión de nuevo.";
        return "Correo o contraseña incorrectos.";
    }
    if (codigo == "email_not_confirmed") return "Falta confirmar el correo: abre el enlace que te llegó y vuelve a intentar.";
    if (codigo == "user_already_exists" || codigo == "email_exists") return "Ya existe una cuenta con ese correo.";
    if (codigo == "weak_password") return "La contraseña es muy débil (mínimo 6 caracteres).";
    if (codigo == "email_address_invalid" || codigo == "validation_failed") return "El correo no es válido.";
    if (codigo == "signup_disabled") return "El registro de cuentas está deshabilitado en el proyecto.";
    if (codigo == "otp_expired" || codigo == "otp_disabled") return "El código no es válido o ya venció. Pide uno nuevo.";
    if (codigo == "same_password") return "La contraseña nueva debe ser distinta de la actual.";
    if (codigo == "reauthentication_needed") return "Por seguridad, vuelve a iniciar sesión y repite el cambio.";
    if (codigo.startsWith("over_") && codigo.endsWith("rate_limit"))
        return "Demasiados intentos seguidos. Espera unos minutos y vuelve a intentar.";
    if (codigo == "refresh_token_not_found" || codigo == "refresh_token_already_used" || codigo == "session_not_found")
        return "La sesión venció. Inicia sesión de nuevo.";
    // REST (PostgREST): {code, message, details, hint}
    const QString mensaje = o["message"].toString(o["msg"].toString(o["error_description"].toString()));
    if (o["code"].toString() == "42501") return "Sin permiso para esta operación (" + mensaje + ").";
    if (!mensaje.isEmpty()) return QString("%1 (HTTP %2)").arg(mensaje).arg(http);
    return QString("La base de datos respondió HTTP %1.").arg(http);
}
