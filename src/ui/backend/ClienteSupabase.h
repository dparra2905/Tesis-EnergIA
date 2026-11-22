// ClienteSupabase.h - Sesion y llamadas a la base de datos de EnergIA en Supabase (D-011).
//
// Vive en ui.exe. Habla con dos servicios de Supabase por HTTPS:
//   Auth (/auth/v1)  login con correo y contrasena, registro, cierre de sesion y renovacion
//                    del token de acceso con el refresh_token;
//   REST (/rest/v1)  tablas con Row Level Security y la funcion subir_corrida.
// Solo usa la llave PUBLICA del proyecto (constante en el codigo, pensada para ir en una app
// de escritorio). La llave secreta nunca va en el repositorio ni en el ejecutable: los
// permisos los imponen las reglas RLS de la base (database/energia_supabase.sql).
//
// Sesion local: se guarda el refresh_token (y los datos del usuario) en un JSON de la carpeta
// de datos de la aplicacion, para no pedir la contrasena en cada inicio. En Windows el token
// va cifrado con DPAPI (ProteccionLocal): solo lo descifra la misma cuenta en el mismo equipo. El token de acceso
// (1 h) solo se guarda en memoria y se renueva antes de vencer o al recibir un 401.
//
// Sin conexion: si la sesion guardada no se puede renovar por falta de red, el estado queda
// "sin_conexion" y la aplicacion sigue usable (la medicion no depende de la base); se vuelve
// a intentar periodicamente. Si nunca hubo sesion, se puede "continuar sin conexion" (estado
// "local"): las corridas quedan pendientes hasta que alguien inicie sesion en este equipo.
//
// Solo Qt Core + Network, para poder probarlo sin interfaz contra un servidor falso.
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTimer>
#include <QUrlQuery>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class ClienteSupabase : public QObject {
    Q_OBJECT
    // "sin_sesion" | "verificando" | "activa" | "sin_conexion" | "local"
    Q_PROPERTY(QString estado READ estado NOTIFY sesionCambiada)
    // Hay token valido: se puede hablar con la base
    Q_PROPERTY(bool conectado READ conectado NOTIFY sesionCambiada)
    // La pantalla de acceso puede dar paso a la aplicacion
    Q_PROPERTY(bool puedeEntrar READ puedeEntrar NOTIFY sesionCambiada)
    Q_PROPERTY(QString usuarioId READ usuarioId NOTIFY sesionCambiada)
    Q_PROPERTY(QString correo READ correo NOTIFY sesionCambiada)
    Q_PROPERTY(QString nombre READ nombre NOTIFY sesionCambiada)
    Q_PROPERTY(QString rol READ rol NOTIFY sesionCambiada)
    Q_PROPERTY(bool esAdmin READ esAdmin NOTIFY sesionCambiada)
    // Hay una operacion de login/registro/cierre en curso
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY ocupadoCambiado)
    // Ultimo error y ultimo aviso para la pantalla de acceso ("" si no hay)
    Q_PROPERTY(QString error READ error NOTIFY mensajeCambiado)
    Q_PROPERTY(QString aviso READ aviso NOTIFY mensajeCambiado)
    // El ultimo error fue de red: la pantalla ofrece continuar sin conexion
    Q_PROPERTY(bool errorDeRed READ errorDeRed NOTIFY mensajeCambiado)

public:
    // Proyecto de Supabase de EnergIA. La llave publica puede ir en el codigo; la secreta no.
    static constexpr const char* URL_BASE = "https://xukkopxbgxswlsosekwx.supabase.co";
    static constexpr const char* LLAVE_PUBLICA = "sb_publishable_IvJcBKwELEWDcHW8p_6Nkg_BYFvWJWm";

    struct Opciones {
        QString urlBase = URL_BASE;
        QString llavePublica = LLAVE_PUBLICA;
        QString rutaSesion;              // JSON con el refresh_token ("" = no se guarda)
        int limiteAuthMs = 30000;        // tiempo maximo de una llamada de Auth
        int limiteRestMs = 120000;       // tiempo maximo de una llamada REST (la subida es grande)
        int reintentoSinConexionMs = 60000;
    };

    // Respuesta de una llamada a Supabase
    struct Resultado {
        int http = 0;          // 0 si no hubo respuesta HTTP
        bool red = false;      // sin conexion, DNS, TLS o tiempo agotado: conviene reintentar
        QJsonDocument cuerpo;  // vacio si la respuesta no es un objeto o un arreglo
        QByteArray crudo;      // cuerpo tal cual (p. ej. el id que devuelve una funcion RPC)
        QByteArray contentRange;  // cabecera Content-Range (paginacion de REST)
        QString error;         // "" si salio bien; si no, el motivo legible
        bool ok() const { return !red && http >= 200 && http < 300; }
    };
    using AlResponder = std::function<void(const Resultado&)>;

    // Intentos ante errores de red en los que la peticion no llego a enviarse
    static constexpr int INTENTOS_RED = 3;

    explicit ClienteSupabase(const Opciones& opciones, QObject* parent = nullptr);
    ~ClienteSupabase() override;

    QString estado() const { return estado_; }
    bool conectado() const { return estado_ == "activa"; }
    bool puedeEntrar() const { return estado_ == "activa" || estado_ == "sin_conexion" || estado_ == "local"; }
    QString usuarioId() const { return usuarioId_; }
    QString correo() const { return correo_; }
    QString nombre() const { return nombre_; }
    QString rol() const { return rol_; }
    bool esAdmin() const { return rol_ == "admin"; }
    bool ocupado() const { return ocupado_; }
    QString error() const { return error_; }
    QString aviso() const { return aviso_; }
    bool errorDeRed() const { return errorDeRed_; }

    // Si hay una sesion guardada, la renueva (estado "verificando" -> "activa"/"sin_conexion")
    Q_INVOKABLE void restaurarSesion();
    Q_INVOKABLE void iniciarSesion(const QString& correo, const QString& contrasena);
    // Registro abierto. Con confirmacion de correo activa en el proyecto, no deja sesion:
    // se avisa que hay que confirmar el correo y luego iniciar sesion.
    Q_INVOKABLE void registrar(const QString& correo, const QString& contrasena, const QString& nombre);
    // Cierra la sesion en el servidor (si hay red) y borra la sesion guardada
    Q_INVOKABLE void cerrarSesion();
    // Entra sin cuenta cuando no hay red (estado "local"); las subidas quedan pendientes
    Q_INVOKABLE void continuarSinConexion();
    Q_INVOKABLE void limpiarMensajes();

    // ---- Cuenta. Resultado: cuentaActualizada(ok, mensaje)
    // Nombre visible para los demas (tabla perfil; solo el propio)
    Q_INVOKABLE void cambiarNombre(const QString& nombre);
    // Pide la contrasena actual: se verifica con un login nuevo, lo que ademas cumple la regla
    // de Supabase de haber iniciado sesion hace poco para cambiarla
    Q_INVOKABLE void cambiarContrasena(const QString& actual, const QString& nueva);

    // ---- Recuperacion de la contrasena sin enlace web (codigo de 6 digitos por correo).
    // Requiere que la plantilla "Reset Password" del proyecto incluya {{ .Token }}.
    // Resultado en error/aviso, como el login.
    Q_INVOKABLE void solicitarRecuperacion(const QString& correo);
    // Verifica el codigo (deja la sesion iniciada) y fija la contrasena nueva
    Q_INVOKABLE void restablecerContrasena(const QString& correo, const QString& codigo, const QString& nueva);

    // Llamada REST autenticada: /rest/v1/<ruta>. Renueva el token si hace falta y reintenta
    // una vez ante un 401. 'cabeceras' se agregan a las estandar (apikey, Authorization).
    void rest(const QByteArray& metodo, const QString& ruta, const QUrlQuery& consulta, const QByteArray& cuerpo,
              AlResponder alResponder, const QList<QPair<QByteArray, QByteArray>>& cabeceras = {});
    // POST /rest/v1/rpc/subir_corrida con {"p": paquete}. Devuelve el id de la corrida creada.
    void subirCorrida(const QJsonObject& paquete, AlResponder alResponder);

    // Texto legible de un error de Auth o de PostgREST (publico para las pruebas)
    static QString textoError(int http, const QJsonDocument& cuerpo);

signals:
    void sesionCambiada();
    void ocupadoCambiado();
    void mensajeCambiado();
    // La sesion paso a tener token valido (tras login, restauracion o volver la red)
    void sesionActivada();
    // El usuario cerro la sesion (o pidio iniciar sesion desde el modo local): main.cpp vuelve
    // a la pantalla de acceso. Una sesion que vence sola no emite esta senal.
    void sesionCerrada();
    void cuentaActualizada(bool ok, const QString& mensaje);
    // Se envio el codigo de recuperacion (la pantalla pasa a pedir el codigo)
    void codigoEnviado(const QString& correo);

private:
    void llamarAuth(const QString& ruta, const QJsonObject& cuerpo, const QByteArray& token, AlResponder alResponder);
    // 'intento' cuenta desde 1: los errores en los que la peticion no llego al servidor (DNS en
    // frio, conexion rechazada) se reintentan solos hasta INTENTOS_RED veces
    void enviar(const QByteArray& metodo, const QUrl& url, const QByteArray& token, const QByteArray& cuerpo,
                int limiteMs, const QList<QPair<QByteArray, QByteArray>>& cabeceras, AlResponder alResponder,
                int intento = 1);
    void renovar(std::function<void(bool ok)> alTerminar);
    void aplicarSesion(const QJsonObject& sesion);
    void cargarPerfil();
    // PUT /auth/v1/user {password} con la sesion vigente
    void fijarContrasena(const QString& nueva, std::function<void(const Resultado&)> alTerminar);
    void guardarSesion() const;
    void borrarSesionGuardada() const;
    void olvidarSesion();
    void ponerEstado(const QString& estado);
    void ponerOcupado(bool ocupado);
    void ponerMensajes(const QString& error, const QString& aviso, bool deRed = false);
    bool tokenVigente() const;

    Opciones op_;
    QNetworkAccessManager* red_ = nullptr;
    QString estado_ = "sin_sesion";
    QString usuarioId_, correo_, nombre_, rol_;
    QByteArray tokenAcceso_;
    QString tokenRenovacion_;
    qint64 venceEnMs_ = 0;       // epoch ms en que vence el token de acceso
    bool ocupado_ = false;
    QString error_, aviso_;
    bool errorDeRed_ = false;
    bool renovando_ = false;
    QList<std::function<void(bool)>> esperandoRenovacion_;
    QTimer timerRenovacion_;     // renueva antes de que venza el token
    QTimer timerSinConexion_;    // reintenta renovar mientras no hay red
};
