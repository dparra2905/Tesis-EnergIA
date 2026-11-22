// Trabajador.h - Hilo de trabajo de la corrida: prepara datos, llama a la DLL (entrenar e
// inferir), evalua, calcula las metricas del modelo, guarda y libera el modelo.
//
// Vive en un QThread propio. Las llamadas a entrenar() e inferir() son bloqueantes y se hacen en
// este hilo; t_antes / t_despues y t_inf_antes / t_inf_despues se toman justo alrededor de ellas,
// y el callback de la DLL toma la hora antes de cualquier otra cosa (D-010: los tiempos se
// controlan en el proceso de ejecucion).
//
// Inferencia (D-014): predice sobre la particion de validacion. El callback escribe una fila de
// progreso_inferencia.csv por grupo de pasadas (mismo formato que progreso.csv) y aplica la regla
// de duracion: devuelve ENERGIA_FINALIZAR en el primer END en que han pasado al menos
// duracionMinMs desde t_inf_antes (ENERGIA_DETENER si se pidio cancelar).
//
// Origen: clase Trabajador de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David
// Parra). Adaptaciones: la lectura de CSV y los datos sinteticos pasan a datos:: (DatosCsv.h);
// la columna objetivo y el origen de datos llegan en ConfigCorrida; se agrega la cantidad de
// pasos registrados en progreso.csv al resultado de la corrida.
#pragma once

#include "DllModelo.h"
#include "energia_dll.h"

#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <atomic>
#include <vector>

struct ConfigCorrida {
    bool usarSintetico = true;
    QString rutaCsv;
    int columnaObjetivo = -1;
    int porcentajeTrain = 80;
    uint32_t semilla = 42;
    EnergiaParams params{};
    FuncionesDll fn;
    QString rutaProgreso;
};

class Trabajador : public QObject {
    Q_OBJECT
public:
    // Maximo de clases: mas valores distintos que esto probablemente es regresion
    static constexpr int MAX_CLASES = 100;

    // Se puede llamar desde otro hilo: solo toca una variable atomica
    void solicitarCancelacion() { cancelar_.store(true); }

    void preparar(const ConfigCorrida& c);
    void ejecutar();
    // Mide la inferencia con el modelo entrenado sobre la particion de validacion (D-014)
    void inferir(qint64 modeloId, int lote, qint64 duracionMinMs, const QString& rutaProgreso);
    // Se llama con la medicion ya detenida: nada de esto entra en la medicion.
    // Orden: evaluar en validacion -> metricas del modelo (predecir en validacion y en
    // entrenamiento, D-015) -> guardar (si se pidio) -> liberar -> soltar los datos.
    // 'evaluar' decide tambien las metricas: las dos se calculan en las mismas corridas.
    void cerrarModelo(qint64 modeloId, bool evaluar, bool guardar, const QString& ruta);

signals:
    void preparado(bool ok, const QString& error, const QJsonObject& resumen);
    void progreso(int fase, int stepIndex, int stepTotal, qint64 tsMs, const QJsonObject& metricas);
    void terminado(int status, qint64 tAntesMs, qint64 tDespuesMs, qint64 modeloId, int pasosRegistrados);
    // Un aviso por grupo de pasadas (END): pasadas y predicciones del grupo y acumuladas
    void progresoInferencia(int grupo, qint64 tsMs, qint64 pasadasGrupo, qint64 pasadas, qint64 predicciones);
    void inferenciaTerminada(int status, qint64 tInfAntesMs, qint64 tInfDespuesMs, qint64 nPredicciones, qint64 pasadas,
                             int gruposRegistrados, qint64 nFilas);
    // 'metricas': {validacion, entrenamiento} con las metricas de cada conjunto (MetricasModelo.h),
    // su status_dll, motivo y duracion; vacio si no se calcularon
    void modeloCerrado(int statusGuardar, int statusLiberar, const QJsonObject& evaluacion, const QJsonObject& metricas);

private:
    static int32_t callback(const EnergiaProgreso* p, void* userData);
    int32_t alProgreso(const EnergiaProgreso* p);
    static int32_t callbackInferencia(const EnergiaProgreso* p, void* userData);
    int32_t alProgresoInferencia(const EnergiaProgreso* p);
    // Fila de progreso.csv / progreso_inferencia.csv para el END de un paso; deja sus metricas en *metricas
    QByteArray filaProgreso(qint64 tFinMs, const EnergiaProgreso* p, QJsonObject* metricas) const;
    // Predice un conjunto con la DLL y calcula sus metricas (D-015)
    QJsonObject metricasConjunto(qint64 modeloId, const std::vector<double>& X, const std::vector<double>& Y) const;
    void liberarDatos();

    ConfigCorrida cfg_;
    QFile archivo_;
    std::vector<double> Xtrain_, Ytrain_, Xval_, Yval_;
    int32_t nCols_ = 0;
    int32_t nClases_ = 0;
    qint64 tInicioPaso_ = 0;
    int pasosRegistrados_ = 0;
    bool listo_ = false;
    // Inferencia en curso
    QFile archivoInferencia_;
    qint64 tInfAntes_ = 0;
    qint64 duracionMinInferenciaMs_ = 0;
    qint64 pasadasInferencia_ = 0;
    int gruposInferencia_ = 0;
    std::atomic<bool> cancelar_{false};
};
