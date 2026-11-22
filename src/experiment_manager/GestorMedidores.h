// GestorMedidores.h - Lanzamiento y cierre de los medidores de una corrida (F5, F6).
//
// Cada medidor es un proceso aparte que escribe sus archivos en la carpeta de la corrida y
// mide sobre la rejilla que empieza en t_start (D-007):
//   - se lanza despues de calcular t_start, con t_start, la carpeta, una duracion maxima de
//     seguridad y el PID del proceso que entrena (D-010: se mide el orquestador, no la interfaz);
//   - puede declarar un archivo "listo" que debe existir al llegar t_start;
//   - se detiene creando stop.flag en la carpeta; si no termina a tiempo, se cierra a la fuerza.
//
// Origen: cfg::Medidor, cfg::medidores(), lanzarMedidores(), alLlegarTStart() (parte de
// archivoListo), alTerminarMedidor(), todosTerminados(), detenerMedidores() y
// forzarCierreMedidores() de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David
// Parra). Cambios: clase propia sin Qt Widgets; los avisos salen como eventos del protocolo;
// la salida de los medidores va al diagnostico (stderr) y no a stdout, que en EnergIA esta
// reservado a los eventos del protocolo; la lista de medidores la decide config.json.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

struct DefinicionMedidor {
    QString nombre;          // "sistema", "energia"
    QString programa;        // relativo: se busca primero junto al orquestador
    QStringList argumentos;  // con marcadores {T_START_UTC} {CARPETA_CORRIDA} {DURACION_MAX_S} {PID_ORQUESTADOR}
    QString archivoListo;    // archivo que el medidor crea cuando ya mide (vacio: no aplica)
    QStringList archivos;    // archivos que deja en la carpeta de la corrida
    QString ayudaSiFalta;    // se agrega al error si el programa no existe
};

class GestorMedidores : public QObject {
    Q_OBJECT
public:
    static constexpr int ESPERA_ARRANQUE_MS = 5000;        // maximo para que un medidor arranque
    static constexpr int ESPERA_CIERRE_MS = 15000;         // tras stop.flag, antes de forzar el cierre
    static constexpr int ESPERA_KILL_MS = 5000;            // tras kill(), antes de seguir sin el medidor
    static constexpr int DURACION_MAXIMA_S = 4 * 3600;     // tope de seguridad (--duration)
    static constexpr const char* ARCHIVO_STOP = "stop.flag";

    explicit GestorMedidores(QObject* parent = nullptr);
    ~GestorMedidores() override;

    // Definicion del monitor de software (system_meter.exe, F5)
    static DefinicionMedidor sistema();
    // Definicion del monitor fisico de energia (energia_ble.py con su Python, F6)
    static DefinicionMedidor energia(const QString& python, const QString& script, const QString& mac);

    void configurar(const QList<DefinicionMedidor>& definiciones) { definiciones_ = definiciones; }
    bool vacio() const { return definiciones_.isEmpty(); }
    QStringList nombres() const;

    // Lanza todos los medidores. Si uno no arranca devuelve false con el motivo.
    bool lanzar(qint64 tStartMs, const QString& dirCorrida, qint64 pidMedido, QString* error);
    // Al llegar t_start: medidores que ya terminaron o que no crearon su archivo "listo"
    QStringList sinConfirmarListos();
    // Crea stop.flag y espera su cierre. Emite todosDetenidos() una sola vez.
    void detener();

    QJsonArray resumen() const;

signals:
    void todosDetenidos();
    // estado: lanzado | termino_antes | terminado | forzado
    void evento(const QString& nombre, const QString& estado, const QJsonObject& detalle);

private:
    struct Estado {
        DefinicionMedidor def;
        QProcess* proceso = nullptr;
        qint64 pid = 0;
        int codigoSalida = -1;
        bool terminado = false;
        bool terminoAntes = false;
        bool forzado = false;
        int listoEnTStart = -1;  // -1 no aplica, 0 no, 1 si
    };

    void alTerminar(int indice, int codigo, QProcess::ExitStatus status);
    void forzarCierre();
    bool todosTerminados() const;
    void avisarDetenidos();

    QList<DefinicionMedidor> definiciones_;
    QList<Estado> medidores_;
    QString dirCorrida_;
    QTimer timerForzar_;
    bool deteniendo_ = false;
    bool matados_ = false;
    bool avisado_ = false;
};
