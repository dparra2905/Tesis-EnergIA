// ExperimentManager.h - Linea de tiempo de una corrida, sin interfaz (D-010).
//
//   iniciar -> preparar datos -> [t_start] -> fase previa -> [t_antes | DLL | t_despues]
//     -> fase posterior -> detener medicion -> evaluar / metricas del modelo / guardar / liberar
//     -> metadata.json
//
// Con la inferencia activada (D-014, opcional por corrida) y el entrenamiento terminado bien:
//   ... [t_despues] -> base_media -> [t_inf_antes | inferir | t_inf_despues] -> fase posterior
//     -> stop.flag -> evaluar / guardar / liberar
// base_media dura tiempos.fase_media_ms (por defecto, lo mismo que la fase previa). La
// inferencia termina cuando el callback devuelve ENERGIA_FINALIZAR (Trabajador). Cancelar en
// base_media o en la inferencia deja la corrida cancelada; un error de inferir, en error con
// error.tipo = "inferencia". En los dos casos se evalua, no se guarda el modelo y se libera.
//
// Medidores (GestorMedidores): los que pida config.json ("medidores"); sin la clave no hay
// ninguno, como en F3. Se lanzan al calcular t_start, se detienen con stop.flag antes de
// evaluar el modelo y su resumen queda en metadata.json. Las metricas del modelo (D-015: matriz
// de confusion, precision, recall y F1 en validacion y entrenamiento) se calculan al cerrar el
// modelo, en las mismas corridas que evaluar, y quedan en metricas_modelo.json. Las fases previa y posterior son la
// linea base: duracion configurable, 20 s por defecto (ConfigExperimento). La energia de cada
// fase se calcula en bruto al terminar (EnergiaExperimento); la correccion por linea base no
// esta decidida (D-008).
//
// Origen: la logica de la corrida de VentanaPrincipal en Back-EnergIA (orquestador.cpp,
// commit 1ab19f1, autor David Parra): iniciarCorrida, alPreparado, alLlegarTStart,
// lanzarEntrenamiento, alProgreso, alTerminar, cancelar, detenerMedidores, cerrarModelo,
// alCerrarModelo, abortar, finalizarCorrida y escribirMetadata. Sin Qt Widgets: la
// configuracion llega por config.json y los avisos salen como eventos JSON Lines.
#pragma once

#include "CatalogoModelos.h"
#include "ConfigExperimento.h"
#include "DllModelo.h"
#include "GestorMedidores.h"
#include "Trabajador.h"

#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>

class ExperimentManager : public QObject {
    Q_OBJECT
public:
    // Codigos de salida del proceso segun el estado final
    static constexpr int SALIDA_COMPLETADA = 0;
    static constexpr int SALIDA_CANCELADA = 5;
    static constexpr int SALIDA_ERROR = 6;
    // Margen minimo entre el lanzamiento de los medidores y t_start, para que esten midiendo
    // en t_start aunque config.json pida 0 ms
    static constexpr qint64 MARGEN_MINIMO_MEDIDORES_MS = 2000;
    // Con el monitor fisico de energia: tiempo para conectar con el Atorch por Bluetooth
    // (MARGEN_CONEXION_MS de Back-EnergIA)
    static constexpr qint64 MARGEN_MINIMO_ENERGIA_MS = 10000;

    explicit ExperimentManager(QObject* parent = nullptr);
    ~ExperimentManager() override;

    void iniciar(const ConfigExperimento& cfg);
    void cancelar();
    void procesarOrden(const QString& orden);

signals:
    void finalizado(int codigoSalida);

private:
    void alPreparado(bool ok, const QString& error, const QJsonObject& resumen);
    void alLlegarTStart();
    void lanzarEntrenamiento();
    void alProgreso(int faseDll, int stepIndex, int stepTotal, qint64 tsMs, const QJsonObject& metricas);
    void alTerminar(int status, qint64 tAntesMs, qint64 tDespuesMs, qint64 modeloId, int pasosRegistrados);
    void iniciarFasePosterior();
    void alVencerFasePosterior();
    void iniciarFaseMedia();
    void lanzarInferencia();
    void alProgresoInferencia(int grupo, qint64 tsMs, qint64 pasadasGrupo, qint64 pasadas, qint64 predicciones);
    void alTerminarInferencia(int status, qint64 tInfAntesMs, qint64 tInfDespuesMs, qint64 nPredicciones, qint64 pasadas,
                              int gruposRegistrados, qint64 nFilas);
    QJsonObject inferenciaJson() const;
    void detenerMedicion();
    void cerrarModelo();
    void alCerrarModelo(int statusGuardar, int statusLiberar, const QJsonObject& evaluacion, const QJsonObject& metricas);
    void escribirMetricasModelo(const QJsonObject& metricas);
    void abortar(const QString& tipo, const QString& motivo);
    void fallarAntesDeEmpezar(const QString& tipo, const QString& motivo);
    void finalizarCorrida();
    void fase(const QString& nombre);
    void escribirMetadata(const QString& estado);
    void calcularEnergia();
    void auditar();
    QJsonObject errorJson() const;

    ConfigExperimento cfg_;
    ModeloDisponible modelo_;
    DllModelo dll_;
    QThread hilo_;
    Trabajador* trabajador_ = nullptr;
    QTimer timerPre_, timerEntrenar_, timerMedia_, timerPost_;
    GestorMedidores medidores_;
    qint64 margenEfectivoMs_ = 0;
    bool tStartVerificado_ = false;   // medidores comprobados en t_start: se puede entrenar
    bool medidoresTarde_ = false;     // terminaron de arrancar despues de t_start
    bool fasePosteriorIniciada_ = false;  // empezo la linea base posterior (fases y energia base_post)
    bool faseMediaIniciada_ = false;      // empezo la linea base intermedia (D-014)
    bool inferenciaIniciada_ = false;     // se lanzo la inferencia (D-014)

    QString dirCorrida_;
    int numCorrida_ = 0;
    QJsonObject hiperUsados_;
    QJsonObject resumenDatos_;
    QJsonObject evaluacion_;
    QJsonObject artefactoModelo_;
    QJsonObject resultadosEnergia_;
    QJsonObject artefactoMetricas_;  // metadata.artefactos.metricas_modelo (D-015)

    QString estadoFinal_;
    QString errorTipo_;
    QString errorMensaje_;
    qint64 tCreacionMs_ = 0, tStartMs_ = 0, tAntesMs_ = 0, tDespuesMs_ = 0, tStopMs_ = 0, tFinMs_ = 0;
    qint64 tInfAntesMs_ = 0, tInfDespuesMs_ = 0;
    qint64 finFasePosteriorMs_ = 0;  // cuando debe terminar la linea base posterior
    // Resultado de inferir (D-014); -1 = no se llamo
    int statusInferencia_ = -1;
    qint64 nPredicciones_ = -1, pasadasInferencia_ = -1, filasInferencia_ = -1;
    int gruposInferencia_ = -1;
    qint64 modeloId_ = 0;
    int status_ = -1;
    int pasosRegistrados_ = 0;
    QString liberacionModelo_ = "no_aplica";
    bool dllDescargada_ = false;
    bool corriendo_ = false;
    bool entrenando_ = false;
    bool infiriendo_ = false;
    bool deteniendo_ = false;
    bool cerrando_ = false;
    bool finalizada_ = false;
    bool cancelado_ = false;
};
