// ClienteOrquestador.h - Comunicacion de la interfaz con energia_orquestador.exe (D-010).
//
// Vive en el proceso de la interfaz (ui.exe). Lanza el orquestador con QProcess:
//   --catalogo    para descubrir los tipos de modelo (D-009);
//   --ejecutar    para una corrida: lee sus eventos JSON Lines por stdout y le envia ordenes
//                 por stdin (docs/sdd/PROTOCOLO_ORQUESTADOR.md).
// No mide, no entrena y no toma marcas temporales de la corrida: todo eso ocurre en el
// orquestador. Aqui solo se refleja el estado para QML.
//
// Robustez (D-010):
//   - si el orquestador termina sin emitir 'fin' (fallo o detencion forzada), la corrida
//     queda en estado "error" y la interfaz sigue abierta;
//   - si la interfaz se cierra durante una corrida, el proceso del orquestador no se termina.
//
// Solo usa Qt Core, para poder probarlo sin interfaz grafica.
#pragma once

#include "LectorMetricasEnergia.h"
#include "LectorMetricasSistema.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class ClienteOrquestador : public QObject {
    Q_OBJECT

    // Las carpetas se pueden cambiar desde Configuración, salvo durante una corrida.
    Q_PROPERTY(QString rutaOrquestador READ rutaOrquestador CONSTANT)
    Q_PROPERTY(QString dirModelos READ dirModelos NOTIFY carpetasCambiadas)
    Q_PROPERTY(QString dirSalida READ dirSalida NOTIFY carpetasCambiadas)
    Q_PROPERTY(QString dirDatasets READ dirDatasets NOTIFY carpetasCambiadas)
    Q_PROPERTY(bool modoDesarrollo READ modoDesarrollo CONSTANT)
    Q_PROPERTY(bool orquestadorEncontrado READ orquestadorEncontrado CONSTANT)
    // Linea base por defecto de las corridas nuevas (pagina Configuracion), en segundos
    Q_PROPERTY(int lineaBaseSegundos READ lineaBaseSegundos WRITE setLineaBaseSegundos NOTIFY lineaBaseCambiada)

    // Catalogo de modelos
    Q_PROPERTY(QString estadoCatalogo READ estadoCatalogo NOTIFY catalogoCambiado)
    Q_PROPERTY(QString errorCatalogo READ errorCatalogo NOTIFY catalogoCambiado)
    Q_PROPERTY(QVariantList modelos READ modelos NOTIFY catalogoCambiado)
    Q_PROPERTY(QVariantList descartadas READ descartadas NOTIFY catalogoCambiado)
    Q_PROPERTY(int modelosInternosOcultos READ modelosInternosOcultos NOTIFY catalogoCambiado)

    // Corrida
    Q_PROPERTY(QString estado READ estado NOTIFY estadoCambiado)
    Q_PROPERTY(bool activa READ activa NOTIFY estadoCambiado)
    Q_PROPERTY(QString fase READ fase NOTIFY estadoCambiado)
    Q_PROPERTY(QVariantMap corrida READ corrida NOTIFY corridaCambiada)
    Q_PROPERTY(int pasosCompletados READ pasosCompletados NOTIFY progresoCambiado)
    Q_PROPERTY(int pasoActual READ pasoActual NOTIFY progresoCambiado)
    Q_PROPERTY(int totalPasos READ totalPasos NOTIFY progresoCambiado)
    Q_PROPERTY(QString unidadPaso READ unidadPaso NOTIFY progresoCambiado)
    Q_PROPERTY(QVariantMap metricasPaso READ metricasPaso NOTIFY progresoCambiado)
    // Inferencia en curso (D-014): {grupos, pasadas, predicciones}; vacio si no hay
    Q_PROPERTY(QVariantMap progresoInferencia READ progresoInferencia NOTIFY progresoCambiado)
    Q_PROPERTY(QVariantList registro READ registro NOTIFY registroCambiado)

    // Medidores de la corrida y metricas en vivo del monitor de software (F5)
    Q_PROPERTY(QVariantList medidoresCorrida READ medidoresCorrida NOTIFY metricasCambiadas)
    Q_PROPERTY(QVariantMap metricasSistema READ metricasSistema NOTIFY metricasCambiadas)
    Q_PROPERTY(QVariantMap historialSistema READ historialSistema NOTIFY metricasCambiadas)
    // Lecturas en vivo del monitor fisico de energia (F6): potencia, voltaje, corriente
    Q_PROPERTY(QVariantMap metricasEnergia READ metricasEnergia NOTIFY metricasCambiadas)
    Q_PROPERTY(QVariantMap historialEnergia READ historialEnergia NOTIFY metricasCambiadas)

public:
    struct Opciones {
        QString rutaOrquestador;   // energia_orquestador.exe
        QString dirModelos;        // donde buscar EnergIA_*.dll
        QString dirSalida;         // raiz de corridas/ (se crea si no existe)
        QString dirDatasets;       // carpeta inicial del selector de CSV
        bool modoDesarrollo = false;  // muestra la DLL de prueba y los datos sinteticos
        // config/laboratorio.json: atorch_mac, python_medidor_energia, script_medidor_energia
        QVariantMap laboratorio;
        int limiteCatalogoMs = 30000;
        // Preferencias que se conservan entre ejecuciones (linea base por defecto). Vacio: no se
        // guardan (pruebas). ui.exe usa preferencias.json en la carpeta de datos de la aplicacion.
        QString rutaPreferencias;
    };

    // Estados de la corrida, en el orden en que pueden ocurrir
    static constexpr const char* ESTADO_INACTIVO = "inactivo";
    static constexpr const char* ESTADO_INICIANDO = "iniciando";
    static constexpr const char* ESTADO_EN_CURSO = "en_curso";
    static constexpr const char* ESTADO_CANCELANDO = "cancelando";
    static constexpr const char* ESTADO_COMPLETADA = "completada";
    static constexpr const char* ESTADO_CANCELADA = "cancelada";
    static constexpr const char* ESTADO_ERROR = "error";

    explicit ClienteOrquestador(const Opciones& opciones, QObject* parent = nullptr);
    ~ClienteOrquestador() override;

    QString rutaOrquestador() const { return op_.rutaOrquestador; }
    QString dirModelos() const { return op_.dirModelos; }
    QString dirSalida() const { return op_.dirSalida; }
    QString dirDatasets() const { return op_.dirDatasets; }
    // Configura una carpeta local existente; devuelve "" si la guardó o el error si falla.
    // No mueve corridas ni archivos previos. Al cambiar modelos se recarga el catálogo.
    Q_INVOKABLE QString configurarCarpeta(const QString& tipo, const QUrl& carpeta);
    Q_INVOKABLE QUrl urlCarpeta(const QString& ruta) const { return QUrl::fromLocalFile(ruta); }
    bool modoDesarrollo() const { return op_.modoDesarrollo; }
    bool orquestadorEncontrado() const;
    int lineaBaseSegundos() const { return lineaBaseS_; }
    void setLineaBaseSegundos(int segundos);

    QString estadoCatalogo() const { return estadoCatalogo_; }
    QString errorCatalogo() const { return errorCatalogo_; }
    QVariantList modelos() const { return modelos_; }
    QVariantList descartadas() const { return descartadas_; }
    int modelosInternosOcultos() const { return internosOcultos_; }

    QString estado() const { return estado_; }
    bool activa() const;
    QString fase() const { return fase_; }
    QVariantMap corrida() const { return corrida_; }
    int pasosCompletados() const { return pasosCompletados_; }
    int pasoActual() const { return pasoActual_; }
    int totalPasos() const { return totalPasos_; }
    QString unidadPaso() const { return unidadPaso_; }
    QVariantMap metricasPaso() const { return metricasPaso_; }
    QVariantMap progresoInferencia() const { return progresoInferencia_; }
    QVariantList registro() const { return registro_; }
    QVariantList medidoresCorrida() const { return medidoresCorrida_; }
    QVariantMap metricasSistema() const { return metricasSistema_; }
    QVariantMap historialSistema() const { return historialSistema_; }
    QVariantMap metricasEnergia() const { return metricasEnergia_; }
    QVariantMap historialEnergia() const { return historialEnergia_; }

    // Monitor fisico de energia segun la configuracion del laboratorio:
    // {mac, python, python_existe, script, script_existe}
    Q_INVOKABLE QVariantMap medidorEnergiaLaboratorio() const;
    // Disponibilidad de los monitores fuera de una corrida: {sistema: bool, energia: bool}.
    // Indica si estan instalados o configurados, no si hay conexion con el medidor.
    Q_INVOKABLE QVariantMap disponibilidadMonitores() const;
    // Datos reales del equipo: sistema operativo, CPU, nucleos, memoria, version de Qt
    Q_INVOKABLE QVariantMap infoSistema() const;

    // Puntos que se conservan por serie para las graficas en vivo
    static constexpr int MAX_HISTORIAL = 120;
    // Linea base: el equipo en reposo antes del entrenamiento (fase previa) y despues (fase
    // posterior), medido con los monitores. Un solo valor para las dos fases, en segundos
    // enteros de 0 a 3600 (el tope de cada fase en el orquestador). El de la pagina
    // Configuracion es el valor por defecto de las corridas nuevas y se conserva entre
    // ejecuciones; cada corrida puede cambiarlo. La correccion de la energia por linea base
    // (energia neta) no esta decidida (D-008).
    static constexpr int LINEA_BASE_DEFECTO_S = 20;
    static constexpr int LINEA_BASE_MAXIMA_S = 3600;
    // Inferencia (D-014), los mismos limites que valida el orquestador (ConfigExperimento.h). La
    // linea base intermedia dura lo mismo que la linea base de la corrida.
    static constexpr int INFERENCIA_DURACION_DEFECTO_S = 60;
    static constexpr int INFERENCIA_DURACION_MAXIMA_S = 3600;
    static constexpr int INFERENCIA_LOTE_DEFECTO = 65536;
    static constexpr int INFERENCIA_LOTE_MAXIMO = 1000000;

    // Lanza "--catalogo" de forma asincrona
    Q_INVOKABLE void cargarCatalogo();

    // Inicia una corrida. 'seleccion' (desde QML):
    //   experimento, modelo (ruta de la DLL), hiperparametros {clave: valor},
    //   datos {origen: "csv"|"sintetico", ruta, columna_objetivo},
    //   porcentaje_entrenamiento, semilla, guardar_modelo,
    //   linea_base_s (opcional: duracion de cada fase de linea base; por defecto lineaBaseSegundos),
    //   inferencia (opcional, D-014): {activada, duracion_min_s, lote}. Sin activarla, el
    //   config.json es el de una corrida sin inferencia.
    // Devuelve "" si se lanzo el orquestador, o el motivo por el que no se pudo.
    Q_INVOKABLE QString iniciar(const QVariantMap& seleccion);

    // Cancelacion cooperativa (orden por stdin)
    Q_INVOKABLE void cancelar();
    // Detencion forzada: termina el proceso del orquestador. La corrida queda incompleta.
    Q_INVOKABLE void forzarDetencion();
    // Tras una corrida terminada, vuelve al estado inactivo
    Q_INVOKABLE void descartarResultado();

    // Columnas y separador del encabezado de un CSV: {ok, columnas, separador, error}
    Q_INVOKABLE QVariantMap encabezadoCsv(const QString& ruta) const;
    // Texto legible de una fase del protocolo
    Q_INVOKABLE QString textoFase(const QString& fase) const;
    // Convierte la URL de un FileDialog en ruta local
    Q_INVOKABLE QString rutaLocal(const QUrl& url) const;

    // PID del proceso de ejecucion en curso (0 si no hay)
    qint64 pidEjecucion() const { return proceso_ ? proceso_->processId() : 0; }

    // Construye el config.json (version 1) de una seleccion. Publico para las pruebas.
    QJsonObject construirConfig(const QVariantMap& seleccion, QString* error) const;
    // Procesa un fragmento de stdout del orquestador (puede traer lineas partidas).
    // Publico para las pruebas.
    void procesarSalida(const QByteArray& datos);

signals:
    void catalogoCambiado();
    void estadoCambiado();
    void corridaCambiada();
    void progresoCambiado();
    void registroCambiado();
    void metricasCambiadas();
    void lineaBaseCambiada();
    void carpetasCambiadas();
    // tipo: "success" | "info" | "warning" | "error" (los mismos del centro de notificaciones)
    void notificacion(const QString& tipo, const QString& titulo, const QString& descripcion);
    // El proceso de ejecucion termino y la carpeta de la corrida ya no cambia (D-011: se
    // encola para subirla a la base). Solo si la corrida llego a tener carpeta.
    void corridaTerminada(const QString& dirCorrida);

private:
    void alTerminarCatalogo(int codigo, QProcess::ExitStatus status);
    void alTerminarCorrida(int codigo, QProcess::ExitStatus status);
    void alFallarCorrida(QProcess::ProcessError error);
    void procesarEvento(const QJsonObject& ev);
    void cerrarConError(const QString& tipo, const QString& mensaje);
    void ponerEstado(const QString& estado);
    void anotar(const QString& tipo, const QString& mensaje);
    void reiniciarCorrida();
    void borrarConfigTemporal();
    QString ultimoDiagnostico() const;
    void leerMetricas();
    void ponerEstadoMedidor(const QString& nombre, const QString& estado);

    Opciones op_;
    int lineaBaseS_ = LINEA_BASE_DEFECTO_S;

    QProcess* procCatalogo_ = nullptr;
    QTimer limiteCatalogo_;
    QString estadoCatalogo_ = "sin_cargar";
    QString errorCatalogo_;
    QVariantList modelos_;
    QVariantList descartadas_;
    int internosOcultos_ = 0;

    QProcess* proceso_ = nullptr;
    QByteArray pendiente_;      // linea de stdout aun incompleta
    QByteArray diagnostico_;    // ultimas lineas de stderr
    QString rutaConfig_;
    QString estado_ = ESTADO_INACTIVO;
    QString fase_;
    QVariantMap corrida_;
    int pasosCompletados_ = 0;
    int pasoActual_ = 0;
    int totalPasos_ = 0;
    QString unidadPaso_;
    QVariantMap metricasPaso_;
    QVariantMap progresoInferencia_;
    QVariantList registro_;
    bool finRecibido_ = false;
    bool forzado_ = false;

    LectorMetricasSistema lector_;
    QTimer timerMetricas_;
    QVariantList medidoresCorrida_;   // [{nombre, estado}]
    QVariantMap metricasSistema_;     // ultima fila derivada
    QVariantMap historialSistema_;    // serie -> lista de valores
    LectorMetricasEnergia lectorEnergia_;
    QVariantMap metricasEnergia_;
    QVariantMap historialEnergia_;
};
