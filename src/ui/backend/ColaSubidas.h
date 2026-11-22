// ColaSubidas.h - Subida de corridas terminadas a la base de datos, con reintento (D-011).
//
// La medicion no cambia: la base entra DESPUES de la corrida. Al terminar una corrida
// (ClienteOrquestador::corridaTerminada) se encola su carpeta; si hay sesion, se arma el
// paquete (PaqueteCorrida, en un hilo aparte: puede leer CSV grandes y calcular el sha256 del
// dataset) y se llama a subir_corrida. Sin red o sin sesion la corrida queda "pendiente" y se
// reintenta: con espera creciente, al volver la sesion y al pedirlo el usuario.
//
// Estados de cada corrida en la cola:
//   esperando_decision  el modo de subida pide que el usuario decida (nunca se sube sola)
//   pendiente           esperando sesion o red
//   subiendo            paquete en preparacion o en envio
//   subida              guardada en la base (id_remoto); si ya estaba subida, se marca igual
//   rechazada           la base o la carpeta la rechazaron (no se reintenta sola: ver ultimo_error)
// Modo de subida (D-012), preferencia en preferencias.json ("modo_subida"). Solo se aplica al
// encolar automaticamente una corrida que termina (encolar):
//   subir_todo                  (por defecto) todas quedan pendientes y se suben solas;
//   preguntar_si_hay_problemas  las de veredicto "valida" se suben solas; las demas (tambien sin
//                               auditoria) esperan decision;
//   preguntar_siempre           todas esperan decision.
// Decisiones: subir (pasa a pendiente) o no subir (sale de la cola: "Solo local", se puede subir
// despues). Subir a mano (reintentar) no pregunta. Si una corrida sin auditoria.json se va a subir,
// el hilo que arma el paquete la calcula antes (auditoria::auditarYGuardar).
// Corrida activa: mientras una corrida esta activa (desde que se lanza el orquestador hasta su
// fin), la cola se pausa: no arma paquetes ni envia, tampoco por el reintento automatico, y se
// reanuda sola al terminar (setCorridaActiva). Un paquete que ya se estaba armando cuando empezo
// la corrida no se envia: vuelve a pendiente.
// Dueno de cada corrida (propietario): la cuenta con sesion cuando termino la corrida.
// Una corrida terminada sin cuenta (modo sin conexion o sesion vencida) NO se sube sola con
// la cuenta de quien inicie sesion despues: queda "sin_cuenta" hasta que alguien la reclame
// (reclamarSinCuenta o reintentar). Las de otra cuenta solo las sube su dueno.
// Una respuesta 409 (ya subida) se comprueba en la base por equipo y t_creacion antes de
// darla por subida.
// Migracion 008 (D-014): una corrida con inferencia solo se sube si la base tiene la 008 (si no,
// el subir_corrida anterior descartaria en silencio sus columnas). Se comprueba una vez por
// sesion (GET corrida?select=inferencia_activada) y se recuerda en memoria; se vuelve a consultar
// al iniciar sesion y si la subida de una corrida con inferencia falla. Sin la 008, la corrida
// queda rechazada con MOTIVO_SIN_008 y vuelve sola a pendiente cuando se detecta la migracion (al
// iniciar sesion o al recargar la lista de Base de datos: setMigracion008). Si la comprobacion no
// responde (sin red), no se concluye nada: la corrida sigue pendiente. Las corridas sin
// inferencia se suben como antes, con o sin la 008.
// Migracion 009 (D-015): el mismo mecanismo para una corrida con metricas del modelo
// (metricas_modelo.json): se comprueba con GET corrida?select=metricas_modelo; sin la 009 queda
// rechazada con MOTIVO_SIN_009 y vuelve sola a pendiente al detectarla (setMigracion009). Una
// corrida con inferencia y metricas necesita las dos.
// La cola se guarda en un JSON de la carpeta de datos de la aplicacion; la carpeta de la
// corrida no se modifica. La huella sha256 de cada dataset se guarda en otro JSON para no
// recalcularla mientras el archivo no cambie (ruta, tamano y fecha).
//
// Solo Qt Core + Network.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class ClienteSupabase;

class ColaSubidas : public QObject {
    Q_OBJECT
    // [{dir, experimento, corrida, estado, intentos, ultimo_error, id_remoto, usuario_id, actualizado}]
    Q_PROPERTY(QVariantList elementos READ elementos NOTIFY cambiado)
    // Corridas que todavia no estan en la base (pendientes + subiendo)
    Q_PROPERTY(int pendientes READ pendientes NOTIFY cambiado)
    Q_PROPERTY(int rechazadas READ rechazadas NOTIFY cambiado)
    Q_PROPERTY(bool subiendo READ subiendo NOTIFY cambiado)
    // Corridas sin cuenta que esperan que alguien las reclame
    Q_PROPERTY(int sinCuenta READ sinCuenta NOTIFY cambiado)
    // Corridas que esperan que el usuario decida si se suben (modo de subida, D-012)
    Q_PROPERTY(int esperandoDecision READ esperandoDecision NOTIFY cambiado)
    // "subir_todo" | "preguntar_si_hay_problemas" | "preguntar_siempre" (preferencias.json)
    Q_PROPERTY(QString modoSubida READ modoSubida WRITE setModoSubida NOTIFY modoSubidaCambiado)
    // Nombre de este equipo (corrida.equipo de lo que se sube desde aqui)
    Q_PROPERTY(QString equipo READ equipo CONSTANT)

public:
    struct Opciones {
        QString rutaCola;       // JSON de la cola
        QString rutaHuellas;    // JSON con las huellas sha256 ya calculadas
        QString equipo;         // nombre del PC (el consumo depende del hardware)
        int esperaInicialMs = 30000;          // primer reintento tras un fallo de red
        int esperaMaximaMs = 15 * 60 * 1000;  // tope de la espera creciente
        QString rutaPreferencias;             // preferencias.json ("" = el modo no se guarda)
    };

    static constexpr const char* MODO_SUBIR_TODO = "subir_todo";
    static constexpr const char* MODO_PREGUNTAR_SI_HAY_PROBLEMAS = "preguntar_si_hay_problemas";
    static constexpr const char* MODO_PREGUNTAR_SIEMPRE = "preguntar_siempre";
    static constexpr const char* MOTIVO_SIN_008 = "Falta aplicar la migración 008 en la base de datos";
    static constexpr const char* MOTIVO_SIN_009 = "Falta aplicar la migración 009 en la base de datos";

    ColaSubidas(const Opciones& opciones, ClienteSupabase* sesion, QObject* parent = nullptr);
    ~ColaSubidas() override;

    QVariantList elementos() const;
    int pendientes() const;
    int rechazadas() const;
    int sinCuenta() const;
    int esperandoDecision() const;
    bool subiendo() const { return !actual_.isEmpty(); }
    QString modoSubida() const { return modo_; }
    void setModoSubida(const QString& modo);
    QString equipo() const { return op_.equipo; }

    // Agrega una corrida que acaba de terminar (idempotente). Segun el modo de subida y el
    // veredicto de su auditoria queda pendiente (e intenta subirla) o esperando decision.
    Q_INVOKABLE void encolar(const QString& dirCorrida);
    // Decision sobre una corrida que espera: subir = true la pasa a pendiente; false la saca de la
    // cola ("Solo local"; se puede subir despues con reintentar)
    Q_INVOKABLE void decidir(const QString& dirCorrida, bool subir);
    // Vuelve a poner en "pendiente" una corrida (o todas las no subidas) y lo intenta ya
    Q_INVOKABLE void reintentar(const QString& dirCorrida);
    Q_INVOKABLE void reintentarTodo();
    // Asigna a la cuenta con sesion las corridas terminadas sin cuenta y las sube
    Q_INVOKABLE void reclamarSinCuenta();
    // Corridas que ya estan en la base ([{id, equipo, t_creacion}], de HistorialRemoto): una
    // carpeta local que coincide (mismo equipo y t_creacion) se informa como subida aunque no
    // este en la cola (p. ej. se subio desde otra instalacion o se borro la cola)
    void sincronizarRemotas(const QVariantList& corridasRemotas);
    // Una corrida se borro de la base: su carpeta deja de estar "subida" (se puede volver a subir)
    void olvidarRemota(qint64 idRemoto);
    // La lista de la base se cargo sin error (HistorialRemoto::listaCargada). Las corridas de la
    // cuenta con sesion que la cola tiene como "subida" y cuyo id_remoto ya no esta en la lista
    // (borradas por fuera de la aplicacion) vuelven a "Solo local" (D3). Si la lista no vino
    // completa (trajo el maximo de corridas), antes se piden esas corridas por id: solo se
    // olvidan las que la base confirma que no existen.
    void depurarSubidas(const QVariantList& corridasRemotas, bool listaCompleta);
    // Estado de una carpeta: {estado: "no_encolada"|..., ultimo_error, id_remoto,
    //   propietario: "propia"|"sin_cuenta"|"otra", origen: "cola"|"base"}
    Q_INVOKABLE QVariantMap estadoDe(const QString& dirCorrida) const;
    // Intenta subir la siguiente pendiente (si hay sesion y no hay otra en curso)
    Q_INVOKABLE void procesar();
    // Hay una corrida activa (ClienteOrquestador::activa): la cola se pausa hasta que termine
    void setCorridaActiva(bool activa);
    // Lo que se sabe de la migracion 008 en la base (p. ej. por la lista de HistorialRemoto). Con
    // true, las corridas rechazadas por MOTIVO_SIN_008 vuelven a pendiente y se intenta subirlas.
    void setMigracion008(bool aplicada);
    // Lo mismo para la migracion 009 y MOTIVO_SIN_009 (D-015)
    void setMigracion009(bool aplicada);

signals:
    void cambiado();
    void corridaSubida(const QString& dirCorrida, qint64 idRemoto);
    // tipo: "success" | "info" | "warning" | "error" (los del centro de notificaciones)
    void notificacion(const QString& tipo, const QString& titulo, const QString& descripcion);
    void modoSubidaCambiado();
    // Una corrida que termino quedo esperando decision (el aviso lleva las acciones)
    void decisionPendiente(const QString& dirCorrida, const QString& nombre, const QString& veredicto);

private:
    struct Preparado {
        QJsonObject paquete;
        QString error;
        QString rutaDataset;
        QJsonObject huella;  // {tam, fecha, sha256} si se calculo
        QString errorAuditoria;  // no se pudo calcular la auditoria que faltaba
    };
    void agregar(const QString& dirCorrida, const QString& estado);
    static QString veredictoDe(const QString& dirCorrida);
    void cargarPreferencias();
    void alPreparar(const QString& clave, const Preparado& p);
    void enviar(const QString& clave, const QJsonObject& paquete);
    // Pregunta a la base si tiene la migracion i (0 = 008, 1 = 009) y luego llama a 'luego' (si respondio)
    void comprobarMigracion(int i, std::function<void()> luego, std::function<void(const QString&)> sinRespuesta);
    void setMigracion(int i, bool aplicada);
    void alResponder(const QString& clave, int http, bool red, const QByteArray& crudo, const QString& error);
    void terminar(const QString& clave, const QString& estado, const QString& error);
    void verificarDuplicado(const QString& clave);
    void olvidarAusentes(const QList<qint64>& ausentes);
    QString propietario(const QJsonObject& e) const;
    qint64 tCreacionMs(const QString& dirCorrida) const;
    void programarReintento();
    void cargar();
    void guardar() const;
    void guardarHuellas() const;
    static QString clave(const QString& dirCorrida);

    Opciones op_;
    ClienteSupabase* sesion_;
    QList<QJsonObject> cola_;       // en orden de llegada
    QHash<QString, QJsonObject> huellas_;  // ruta del dataset -> {tam, fecha, sha256}
    QHash<QString, qint64> remotas_;       // "equipo|t_creacion en ms" -> id en la base
    mutable QHash<QString, qint64> tCreacion_;  // clave de carpeta -> t_creacion en ms
    QString actual_;                // clave de la corrida en curso ("" = ninguna)
    QThread* hilo_ = nullptr;
    QTimer reintento_;
    int fallosSeguidos_ = 0;
    QString modo_ = MODO_SUBIR_TODO;
    bool corridaActiva_ = false;
    // Migraciones 008 y 009 en la base (indice 0 y 1): -1 = no se sabe (se comprueba antes de subir
    // una corrida que la necesita), 0 = falta, 1 = aplicada
    int migracion_[2] = {-1, -1};
};
