// ExperimentManager.cpp - ver ExperimentManager.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "ExperimentManager.h"

#include "Auditoria.h"
#include "Corridas.h"
#include "DatosCsv.h"
#include "EnergiaExperimento.h"
#include "FormatosModelo.h"
#include "MetricasModelo.h"
#include "Protocolo.h"
#include "Reloj.h"
#include "energia_comun.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char* ARCHIVO_CONFIG = "config.json";
constexpr const char* ARCHIVO_METADATA = "metadata.json";
constexpr const char* ARCHIVO_PROGRESO = "progreso.csv";
constexpr const char* ARCHIVO_PROGRESO_INFERENCIA = "progreso_inferencia.csv";
constexpr const char* ARCHIVO_MODELO = "modelo.bin";
constexpr int VERSION_METADATA = 1;

QString describirEstado(int st)
{
    switch (st) {
    case ENERGIA_OK:                  return "ok";
    case ENERGIA_ERR_PARAMETROS:      return "parametros invalidos";
    case ENERGIA_ERR_DATOS:           return "datos invalidos";
    case ENERGIA_ERR_INTERNO:         return "error interno de la DLL";
    case ENERGIA_CANCELADO:           return "cancelado";
    case ENERGIA_ERR_MODELO_INVALIDO: return "identificador de modelo invalido";
    case ENERGIA_ERR_ARCHIVO:         return "no se pudo escribir el archivo";
    default:                          return QString("codigo desconocido %1").arg(st);
    }
}

QJsonValue tiempo(qint64 ms)
{
    return ms > 0 ? QJsonValue(QString::fromLatin1(reloj::isoUtc(ms))) : QJsonValue(QJsonValue::Null);
}

// Hilos que uso la corrida: el hiperparametro 'hilos' si existe y es > 0; si no, los que declara
// la DLL (automaticos)
int hilosEfectivos(const QJsonObject& hiper, int automaticos)
{
    const QJsonValue h = hiper["hilos"];
    return h.isDouble() && h.toInt() > 0 ? h.toInt() : automaticos;
}

QString texto(const char* arreglo, size_t tam) { return QString::fromStdString(energia::leerTexto(arreglo, tam)); }
template <size_t N> QString texto(const char (&a)[N]) { return texto(a, N); }

} // namespace

ExperimentManager::ExperimentManager(QObject* parent) : QObject(parent)
{
    for (QTimer* t : {&timerPre_, &timerEntrenar_, &timerMedia_, &timerPost_}) {
        t->setSingleShot(true);
        t->setTimerType(Qt::PreciseTimer);
    }
    connect(&timerPre_, &QTimer::timeout, this, &ExperimentManager::alLlegarTStart);
    connect(&timerEntrenar_, &QTimer::timeout, this, &ExperimentManager::lanzarEntrenamiento);
    connect(&timerMedia_, &QTimer::timeout, this, &ExperimentManager::lanzarInferencia);
    connect(&timerPost_, &QTimer::timeout, this, &ExperimentManager::alVencerFasePosterior);

    // Los medidores detenidos (o cerrados a la fuerza) dan paso a evaluar y cerrar el modelo
    connect(&medidores_, &GestorMedidores::todosDetenidos, this, &ExperimentManager::cerrarModelo);
    connect(&medidores_, &GestorMedidores::evento, this,
            [](const QString& nombre, const QString& estado, const QJsonObject& detalle) {
                QJsonObject ev = detalle;
                ev["nombre"] = nombre;
                ev["estado"] = estado;
                protocolo::emitir("medidor", ev);
            });

    // El hilo de trabajo vive toda la sesion
    trabajador_ = new Trabajador;
    trabajador_->moveToThread(&hilo_);
    connect(&hilo_, &QThread::finished, trabajador_, &QObject::deleteLater);
    connect(trabajador_, &Trabajador::preparado, this, &ExperimentManager::alPreparado);
    connect(trabajador_, &Trabajador::progreso, this, &ExperimentManager::alProgreso);
    connect(trabajador_, &Trabajador::terminado, this, &ExperimentManager::alTerminar);
    connect(trabajador_, &Trabajador::progresoInferencia, this, &ExperimentManager::alProgresoInferencia);
    connect(trabajador_, &Trabajador::inferenciaTerminada, this, &ExperimentManager::alTerminarInferencia);
    connect(trabajador_, &Trabajador::modeloCerrado, this, &ExperimentManager::alCerrarModelo);
    hilo_.start();
}

ExperimentManager::~ExperimentManager()
{
    hilo_.quit();
    hilo_.wait();
    dll_.descargar();
}

// ------------------------------------------------------------------------- inicio
void ExperimentManager::iniciar(const ConfigExperimento& cfg)
{
    cfg_ = cfg;
    tCreacionMs_ = reloj::ahoraMs();

    // Todo lo que puede fallar por configuracion se comprueba ANTES de crear la corrida
    modelo_ = ModeloDisponible();
    modelo_.ruta = QFileInfo(cfg_.rutaDll).absoluteFilePath();
    QString error;
    if (!CatalogoModelos::leerDescripcion(&modelo_, &error)) { fallarAntesDeEmpezar("modelo", error); return; }
    modelo_.interno = (texto(modelo_.info.clave) == QLatin1String(CatalogoModelos::CLAVE_PRUEBA));

    EnergiaParams params;
    if (!config::resolverHiperparametros(modelo_, cfg_.hiperparametros, &params, &hiperUsados_, &error)) {
        fallarAntesDeEmpezar("configuracion", error);
        return;
    }

    int columna = -1;
    if (!cfg_.usarSintetico) {
        QStringList columnas;
        char sep = ',';
        if (!datos::leerEncabezado(cfg_.rutaCsv, &columnas, &sep, &error)) { fallarAntesDeEmpezar("datos", error); return; }
        columna = cfg_.columnaObjetivo.isEmpty() ? cfg_.columnaObjetivoIndice : static_cast<int>(columnas.indexOf(cfg_.columnaObjetivo));
        if (columna < 0 || columna >= columnas.size()) {
            fallarAntesDeEmpezar("datos", "La columna objetivo no existe en el CSV: "
                                              + (cfg_.columnaObjetivo.isEmpty() ? QString::number(cfg_.columnaObjetivoIndice) : cfg_.columnaObjetivo));
            return;
        }
    }

    if (!dll_.cargar(modelo_.ruta, &error)) { fallarAntesDeEmpezar("modelo", error); return; }

    if (!corridas::crearCarpeta(cfg_.dirSalida, cfg_.experimento, &dirCorrida_, &numCorrida_, &error)) {
        dll_.descargar();
        fallarAntesDeEmpezar("almacenamiento", error);
        return;
    }
    corridas::escribirJson(QDir(dirCorrida_).filePath(ARCHIVO_CONFIG), cfg_.original);

    corriendo_ = true;
    estadoFinal_ = "completada";

    QList<DefinicionMedidor> definiciones;
    if (cfg_.medidorSistema) definiciones << GestorMedidores::sistema();
    if (cfg_.medidorEnergia) {
        // Por defecto, el entorno virtual y el script de src/monitoring/hardware_monitor
        const QDir dirMed(QString::fromUtf8(ENERGIA_DIR_MEDIDORES));
#ifdef Q_OS_WIN
        const QString pythonDefecto = dirMed.filePath("venv/Scripts/python.exe");
#else
        const QString pythonDefecto = dirMed.filePath("venv/bin/python");
#endif
        definiciones << GestorMedidores::energia(
            cfg_.energiaPython.isEmpty() ? pythonDefecto : cfg_.energiaPython,
            cfg_.energiaScript.isEmpty() ? dirMed.filePath("energia_ble.py") : cfg_.energiaScript,
            cfg_.energiaMac);
    }
    medidores_.configurar(definiciones);

    QJsonObject ev;
    ev["experimento"] = cfg_.experimento;  // el nombre tal como se escribio (con tildes)
    ev["experimento_carpeta"] = corridas::nombreSeguro(cfg_.experimento);
    ev["corrida"] = numCorrida_;
    ev["dir_corrida"] = QDir::toNativeSeparators(dirCorrida_);
    ev["modelo"] = texto(modelo_.info.clave);
    ev["modelo_interno"] = modelo_.interno;
    ev["medidores"] = QJsonArray::fromStringList(medidores_.nombres());
    protocolo::emitir("inicio", ev);
    protocolo::diagnostico("Corrida " + QDir::toNativeSeparators(dirCorrida_)
                           + (medidores_.vacio() ? QString(" (sin medidores)") : " (medidores: " + medidores_.nombres().join(", ") + ")"));
    fase("preparando_datos");

    ConfigCorrida c;
    c.usarSintetico = cfg_.usarSintetico;
    c.rutaCsv = cfg_.rutaCsv;
    c.columnaObjetivo = columna;
    c.porcentajeTrain = cfg_.porcentajeTrain;
    c.semilla = cfg_.semilla;
    c.params = params;
    c.params.semilla = c.semilla;  // n_clases lo pone el hilo de trabajo al preparar los datos
    c.fn = dll_.funciones();
    c.rutaProgreso = QDir(dirCorrida_).filePath(ARCHIVO_PROGRESO);

    Trabajador* t = trabajador_;
    QMetaObject::invokeMethod(t, [t, c] { t->preparar(c); }, Qt::QueuedConnection);
}

void ExperimentManager::alPreparado(bool ok, const QString& error, const QJsonObject& resumen)
{
    if (!ok) { abortar("datos", error); return; }
    resumenDatos_ = resumen;
    QJsonObject ev;
    ev["datos"] = resumen;
    protocolo::emitir("datos_preparados", ev);
    if (cancelado_) { detenerMedicion(); return; }

    // t_start redondeado al siguiente segundo entero (rejilla comun de 1 s, D-007). Con
    // medidores hay un margen minimo para que ya esten midiendo en t_start.
    const qint64 margenMinimo = cfg_.medidorEnergia ? MARGEN_MINIMO_ENERGIA_MS : MARGEN_MINIMO_MEDIDORES_MS;
    margenEfectivoMs_ = medidores_.vacio() ? cfg_.margenInicioMs : std::max(cfg_.margenInicioMs, margenMinimo);
    const qint64 t = reloj::ahoraMs() + margenEfectivoMs_;
    tStartMs_ = ((t + 999) / 1000) * 1000;
    escribirMetadata("en_curso");
    QJsonObject evT;
    evT["t_start"] = tiempo(tStartMs_);
    protocolo::emitir("t_start", evT);

    // Se mide el proceso que contiene la DLL y entrena: este (D-010)
    QString errorMedidor;
    if (!medidores_.lanzar(tStartMs_, dirCorrida_, QCoreApplication::applicationPid(), &errorMedidor)) {
        abortar("medidor", errorMedidor);
        return;
    }
    // El lanzamiento espera a que cada medidor arranque: si eso consumio el margen, los
    // medidores empiezan en el siguiente punto de la rejilla (lo registran ellos) y aqui se anota
    if (!medidores_.vacio() && reloj::ahoraMs() > tStartMs_) {
        medidoresTarde_ = true;
        protocolo::diagnostico("Los medidores terminaron de arrancar despues de t_start");
    }

    if (margenEfectivoMs_ > 0) fase("margen_inicio");
    timerPre_.start(reloj::msHasta(tStartMs_));
    timerEntrenar_.start(reloj::msHasta(tStartMs_ + cfg_.fasePreviaMs));
}

void ExperimentManager::alLlegarTStart()
{
    if (tStartVerificado_ || deteniendo_) return;
    // Los medidores deben seguir vivos y, si avisan con un archivo, estar listos (energia_lista.flag)
    const QStringList faltan = medidores_.sinConfirmarListos();
    if (!faltan.isEmpty()) {
        abortar("medidor", "Medidores sin confirmar que estan listos al llegar t_start: " + faltan.join(", "));
        return;
    }
    tStartVerificado_ = true;
    if (cfg_.fasePreviaMs > 0) fase("fase_previa");
}

// ------------------------------------------------------------------------- entrenamiento
void ExperimentManager::lanzarEntrenamiento()
{
    if (deteniendo_) return;
    // QTimer puede adelantarse ~1 ms: el entrenamiento nunca empieza antes de lo previsto
    const qint64 objetivo = tStartMs_ + cfg_.fasePreviaMs;
    if (reloj::ahoraMs() < objetivo) {
        timerEntrenar_.start(std::max(1, reloj::msHasta(objetivo)));
        return;
    }
    // Con fase previa de 0 ms los dos temporizadores vencen a la vez y Qt no garantiza su orden:
    // el entrenamiento nunca empieza sin la comprobacion de t_start
    if (!tStartVerificado_) {
        alLlegarTStart();
        if (deteniendo_) return;
    }
    fase("entrenando");
    entrenando_ = true;
    Trabajador* t = trabajador_;
    QMetaObject::invokeMethod(t, [t] { t->ejecutar(); }, Qt::QueuedConnection);
}

void ExperimentManager::alProgreso(int faseDll, int stepIndex, int stepTotal, qint64 tsMs, const QJsonObject& metricas)
{
    QJsonObject ev;
    ev["momento"] = (faseDll == ENERGIA_FASE_START) ? "inicio_paso" : "fin_paso";
    ev["paso"] = stepIndex;
    ev["total"] = stepTotal;
    ev["unidad"] = texto(modelo_.info.unidad_paso);
    ev["t_callback"] = tiempo(tsMs);
    if (faseDll == ENERGIA_FASE_END) ev["metricas"] = metricas;
    protocolo::emitir("progreso", ev);
}

void ExperimentManager::alTerminar(int status, qint64 tAntesMs, qint64 tDespuesMs, qint64 modeloId, int pasosRegistrados)
{
    entrenando_ = false;
    status_ = status;
    tAntesMs_ = tAntesMs;
    tDespuesMs_ = tDespuesMs;
    modeloId_ = modeloId;
    pasosRegistrados_ = pasosRegistrados;

    QJsonObject ev;
    ev["status_dll"] = status;
    ev["status_texto"] = describirEstado(status);
    ev["t_antes"] = tiempo(tAntesMs);
    ev["t_despues"] = tiempo(tDespuesMs);
    ev["duracion_ms"] = tDespuesMs - tAntesMs;
    ev["pasos_registrados"] = pasosRegistrados;
    protocolo::emitir("entrenamiento_terminado", ev);

    if (status == ENERGIA_OK) {
        if (cancelado_) { detenerMedicion(); return; }
        if (cfg_.inferenciaActivada) iniciarFaseMedia();
        else iniciarFasePosterior();
        return;
    }
    if (status == ENERGIA_CANCELADO) {
        estadoFinal_ = "cancelada";
    } else {
        estadoFinal_ = "error";
        errorTipo_ = "dll";
        errorMensaje_ = "La DLL devolvio: " + describirEstado(status);
    }
    detenerMedicion();
}

void ExperimentManager::iniciarFasePosterior()
{
    if (cfg_.fasePosteriorMs > 0) {
        fasePosteriorIniciada_ = true;
        fase("fase_posterior");
    }
    // La fase posterior empieza en t_despues, o en t_inf_despues con la inferencia
    finFasePosteriorMs_ = (tInfDespuesMs_ > 0 ? tInfDespuesMs_ : tDespuesMs_) + cfg_.fasePosteriorMs;
    timerPost_.start(std::max(0, reloj::msHasta(finFasePosteriorMs_)));
}

void ExperimentManager::alVencerFasePosterior()
{
    if (deteniendo_) return;
    // QTimer puede adelantarse ~1 ms: la linea base posterior nunca termina antes de lo pedido
    if (cfg_.fasePosteriorMs > 0 && reloj::ahoraMs() < finFasePosteriorMs_) {
        timerPost_.start(std::max(1, reloj::msHasta(finFasePosteriorMs_)));
        return;
    }
    detenerMedicion();
}

// ------------------------------------------------------------------------- inferencia (D-014)
void ExperimentManager::iniciarFaseMedia()
{
    // Linea base intermedia: con 0 ms no hay; la inferencia empieza apenas termina el entrenamiento
    if (cfg_.faseMediaMs > 0) {
        faseMediaIniciada_ = true;
        fase("fase_media");
    }
    timerMedia_.start(static_cast<int>(cfg_.faseMediaMs));
}

void ExperimentManager::lanzarInferencia()
{
    if (deteniendo_) return;
    // QTimer puede adelantarse ~1 ms: la inferencia nunca empieza antes de lo previsto
    const qint64 objetivo = tDespuesMs_ + cfg_.faseMediaMs;
    if (reloj::ahoraMs() < objetivo) {
        timerMedia_.start(std::max(1, reloj::msHasta(objetivo)));
        return;
    }
    inferenciaIniciada_ = true;
    infiriendo_ = true;
    fase("inferencia");
    Trabajador* t = trabajador_;
    const qint64 id = modeloId_;
    const int lote = cfg_.inferenciaLote;
    const qint64 duracionMinMs = static_cast<qint64>(cfg_.inferenciaDuracionMinS) * 1000;
    const QString ruta = QDir(dirCorrida_).filePath(ARCHIVO_PROGRESO_INFERENCIA);
    QMetaObject::invokeMethod(t, [t, id, lote, duracionMinMs, ruta] { t->inferir(id, lote, duracionMinMs, ruta); },
                              Qt::QueuedConnection);
}

void ExperimentManager::alProgresoInferencia(int grupo, qint64 tsMs, qint64 pasadasGrupo, qint64 pasadas,
                                             qint64 predicciones)
{
    // Un evento por grupo de pasadas (como mucho uno por segundo)
    QJsonObject ev;
    ev["tarea"] = "inferencia";
    ev["momento"] = "fin_paso";
    ev["paso"] = grupo;
    ev["total"] = 0;
    ev["unidad"] = "grupo";
    ev["t_callback"] = tiempo(tsMs);
    ev["metricas"] = QJsonObject{{"pasadas", static_cast<double>(pasadasGrupo)}};
    ev["pasadas"] = pasadas;
    ev["predicciones"] = predicciones;
    protocolo::emitir("progreso", ev);
}

void ExperimentManager::alTerminarInferencia(int status, qint64 tInfAntesMs, qint64 tInfDespuesMs, qint64 nPredicciones,
                                             qint64 pasadas, int gruposRegistrados, qint64 nFilas)
{
    infiriendo_ = false;
    statusInferencia_ = status;
    tInfAntesMs_ = tInfAntesMs;
    tInfDespuesMs_ = tInfDespuesMs;
    nPredicciones_ = nPredicciones;
    pasadasInferencia_ = pasadas;
    gruposInferencia_ = gruposRegistrados;
    filasInferencia_ = nFilas;

    const QJsonObject inf = inferenciaJson();
    QJsonObject ev;
    ev["status_dll"] = status;
    ev["status_texto"] = describirEstado(status);
    ev["t_inf_antes"] = tiempo(tInfAntesMs);
    ev["t_inf_despues"] = tiempo(tInfDespuesMs);
    ev["duracion_ms"] = inf["duracion_ms"];
    ev["grupos_registrados"] = gruposRegistrados;
    ev["pasadas"] = pasadas;
    ev["n_predicciones"] = nPredicciones;
    ev["tiempo_por_prediccion_us"] = inf["tiempo_por_prediccion_us"];
    ev["motivo_sin_por_prediccion"] = inf["motivo_sin_por_prediccion"];
    protocolo::emitir("inferencia_terminada", ev);

    if (status == ENERGIA_OK) {
        if (cancelado_) { detenerMedicion(); return; }
        iniciarFasePosterior();
        return;
    }
    if (status == ENERGIA_CANCELADO && cancelado_) {
        estadoFinal_ = "cancelada";
    } else {
        // El entrenamiento esta completo: se evalua, no se guarda el modelo y se libera (cerrarModelo)
        estadoFinal_ = "error";
        errorTipo_ = "inferencia";
        errorMensaje_ = "inferir devolvio: " + describirEstado(status);
    }
    detenerMedicion();
}

// metadata.inferencia (D-014). tiempo_por_prediccion_us se calcula una sola vez, aqui: resultados.json
// lo copia de este objeto. Solo existe si inferir devolvio ENERGIA_OK.
QJsonObject ExperimentManager::inferenciaJson() const
{
    QJsonObject j;
    j["activada"] = cfg_.inferenciaActivada;
    if (!cfg_.inferenciaActivada) return j;
    const bool llamada = statusInferencia_ >= 0;
    auto entero = [llamada](qint64 v) { return llamada ? QJsonValue(v) : QJsonValue(QJsonValue::Null); };
    j["lote"] = cfg_.inferenciaLote;
    j["duracion_min_s"] = cfg_.inferenciaDuracionMinS;
    j["paso_min_ms"] = ENERGIA_INFERENCIA_PASO_MIN_MS;
    j["datos"] = "validacion";
    j["n_filas"] = llamada ? QJsonValue(filasInferencia_) : resumenDatos_["n_val"];
    j["status_dll"] = llamada ? QJsonValue(statusInferencia_) : QJsonValue(QJsonValue::Null);
    j["status_texto"] = llamada ? QJsonValue(describirEstado(statusInferencia_)) : QJsonValue(QJsonValue::Null);
    j["grupos_registrados"] = entero(gruposInferencia_);
    j["pasadas"] = entero(pasadasInferencia_);
    j["n_predicciones"] = entero(nPredicciones_);
    const qint64 duracion = tInfDespuesMs_ - tInfAntesMs_;
    j["duracion_ms"] = (tInfAntesMs_ > 0 && tInfDespuesMs_ > 0) ? QJsonValue(duracion) : QJsonValue(QJsonValue::Null);
    const bool ok = statusInferencia_ == ENERGIA_OK && nPredicciones_ > 0 && tInfAntesMs_ > 0 && tInfDespuesMs_ > 0;
    j["tiempo_por_prediccion_us"] = ok ? QJsonValue(static_cast<double>(duracion) * 1000.0 / static_cast<double>(nPredicciones_))
                                       : QJsonValue(QJsonValue::Null);
    j["motivo_sin_por_prediccion"] = !llamada || ok ? QJsonValue(QJsonValue::Null)
                                   : statusInferencia_ == ENERGIA_CANCELADO ? QJsonValue("inferencia cancelada")
                                                                            : QJsonValue("error de inferir");
    return j;
}

// ------------------------------------------------------------------------- cancelacion
void ExperimentManager::procesarOrden(const QString& orden)
{
    if (orden == "cancelar") cancelar();
    else protocolo::diagnostico("Orden desconocida ignorada: " + orden);
}

void ExperimentManager::cancelar()
{
    if (!corriendo_ || deteniendo_ || cancelado_) return;
    cancelado_ = true;
    estadoFinal_ = "cancelada";
    protocolo::emitir("cancelacion_solicitada");

    if (entrenando_) {
        trabajador_->solicitarCancelacion();  // la DLL se detiene en el proximo aviso del callback
        fase("cancelando_entrenamiento");
    } else if (infiriendo_) {
        trabajador_->solicitarCancelacion();  // inferir se detiene al terminar el grupo en curso
        fase("cancelando_inferencia");
    } else if (tStartMs_ == 0) {
        fase("cancelando");  // alPreparado lo resuelve al terminar la preparacion
    } else {
        detenerMedicion();
    }
}

// ------------------------------------------------------------------------- cierre
void ExperimentManager::detenerMedicion()
{
    if (deteniendo_) return;
    deteniendo_ = true;
    timerPre_.stop();
    timerEntrenar_.stop();
    timerMedia_.stop();
    timerPost_.stop();
    tStopMs_ = reloj::ahoraMs();
    // stop.flag y espera del cierre de los medidores; luego todosDetenidos -> cerrarModelo().
    // La evaluacion y el guardado quedan fuera de la ventana de medicion.
    if (!medidores_.vacio()) fase("deteniendo_medidores");
    medidores_.detener();
}

void ExperimentManager::cerrarModelo()
{
    if (cerrando_) return;
    cerrando_ = true;

    // Se evalua si el entrenamiento termino bien, aunque la corrida se haya cancelado despues
    // (durante la linea base intermedia, la inferencia o la linea base posterior) o inferir haya
    // fallado: la exactitud queda registrada. Las metricas del modelo (D-015) se calculan en las
    // mismas corridas, con la medicion ya detenida. Una corrida cancelada o con error de inferir no
    // guarda el modelo. El modelo se libera siempre (Trabajador::cerrarModelo).
    const bool evaluar = status_ == ENERGIA_OK && modeloId_ > 0;
    const bool guardar = cfg_.guardarModelo && evaluar && estadoFinal_ != "cancelada" && errorTipo_ != "inferencia";
    const QString ruta = QDir(dirCorrida_).filePath(ARCHIVO_MODELO);
    fase("cerrando_modelo");
    artefactoModelo_ = QJsonObject();
    artefactoModelo_["solicitado"] = cfg_.guardarModelo;
    artefactoModelo_["archivo"] = guardar ? QJsonValue(QString(ARCHIVO_MODELO)) : QJsonValue(QJsonValue::Null);

    Trabajador* t = trabajador_;
    const qint64 id = modeloId_;
    QMetaObject::invokeMethod(t, [t, id, evaluar, guardar, ruta] { t->cerrarModelo(id, evaluar, guardar, ruta); },
                              Qt::QueuedConnection);
}

void ExperimentManager::alCerrarModelo(int statusGuardar, int statusLiberar, const QJsonObject& evaluacion,
                                       const QJsonObject& metricas)
{
    evaluacion_ = evaluacion;
    if (evaluacion["ejecutada"].toBool()) {
        QJsonObject ev;
        ev["evaluacion"] = evaluacion;
        protocolo::emitir("evaluacion", ev);
        if (evaluacion["status"].toInt() != ENERGIA_OK)
            protocolo::diagnostico("evaluar: " + describirEstado(evaluacion["status"].toInt()));
    }
    if (!metricas.isEmpty()) escribirMetricasModelo(metricas);
    artefactoModelo_["guardado"] = (statusGuardar == ENERGIA_OK);
    if (statusGuardar >= 0) artefactoModelo_["status_guardar"] = statusGuardar;
    if (statusGuardar >= 0 && statusGuardar != ENERGIA_OK)
        protocolo::diagnostico("guardar_modelo: " + describirEstado(statusGuardar));
    // En el contrato v4 el artefacto no se puede volver a cargar en EnergIA (D-009); el Arbol y el
    // MLP se pueden usar fuera de EnergIA mediante la exportacion (D-013, FormatosModelo.h)
    artefactoModelo_["recargable_en_energia"] = false;
    artefactoModelo_["exportable"] =
        statusGuardar == ENERGIA_OK && modelos::formatoExportacion(texto(modelo_.info.clave)) != nullptr;

    if (modeloId_ > 0) liberacionModelo_ = (statusLiberar == ENERGIA_OK) ? "liberado" : "error_al_liberar";
    if (statusLiberar >= 0 && statusLiberar != ENERGIA_OK)
        protocolo::diagnostico("liberar_modelo: " + describirEstado(statusLiberar));
    modeloId_ = 0;
    finalizarCorrida();
}

// metricas_modelo.json (D-015). Se escribe siempre que se intento el calculo, aunque predecir falle en
// un conjunto o en los dos: ese conjunto queda con su status_dll, el motivo y las metricas en null. La
// corrida no cambia de estado; cada fallo deja un diagnostico con el codigo y el conjunto.
void ExperimentManager::escribirMetricasModelo(const QJsonObject& metricas)
{
    const QJsonArray clases = resumenDatos_["clases_originales"].toArray();
    QJsonObject raiz;
    raiz["version_formato"] = metricas::VERSION_FORMATO;
    raiz["tipo"] = "derivado";
    raiz["metodo"] = "predecir de la DLL; metricas como scikit-learn (zero_division=0, todas las clases)";
    raiz["lote_predecir"] = metricas::LOTE_PREDECIR;
    raiz["n_clases"] = clases.size();
    raiz["clases_originales"] = clases;
    for (const char* conjunto : {"validacion", "entrenamiento"}) {
        QJsonObject c = metricas[conjunto].toObject();
        const int st = c["status_dll"].toInt(ENERGIA_ERR_INTERNO);
        c["motivo"] = st == ENERGIA_OK ? QJsonValue(QJsonValue::Null)
                                       : QJsonValue("predecir devolvio: " + describirEstado(st));
        if (st == ENERGIA_OK) {
            // Etiquetas originales de cada clase (Trabajador solo conoce los indices)
            QJsonArray porClase;
            for (const QJsonValue& v : c["por_clase"].toArray()) {
                QJsonObject pc = v.toObject();
                const int i = pc["indice"].toInt();
                pc["etiqueta"] = i < clases.size() ? clases[i] : QJsonValue(QJsonValue::Null);
                porClase.append(pc);
            }
            c["por_clase"] = porClase;
        } else {
            for (const char* k : {"matriz_confusion", "exactitud", "por_clase", "macro", "ponderado"})
                c[k] = QJsonValue(QJsonValue::Null);
            protocolo::diagnostico(QString("predecir (%1): %2 (codigo %3)").arg(conjunto, describirEstado(st)).arg(st));
        }
        raiz[conjunto] = c;
    }

    // Consistencia: la exactitud de validacion debe coincidir con val_accuracy de evaluar
    const QJsonValue valAccuracy = evaluacion_["metricas"].toObject()["val_accuracy"];
    const QJsonValue exactitudVal = raiz["validacion"].toObject()["exactitud"];
    QJsonObject consistencia;
    consistencia["val_accuracy_evaluar"] = valAccuracy.isDouble() ? valAccuracy : QJsonValue(QJsonValue::Null);
    consistencia["exactitud_validacion"] = exactitudVal.isDouble() ? exactitudVal : QJsonValue(QJsonValue::Null);
    if (valAccuracy.isDouble() && exactitudVal.isDouble()) {
        const bool coincide = std::fabs(valAccuracy.toDouble() - exactitudVal.toDouble()) <= 1e-9;
        consistencia["coincide"] = coincide;
        if (!coincide)
            protocolo::diagnostico(QString("La exactitud de validacion de las metricas del modelo (%1) no coincide con "
                                           "val_accuracy de evaluar (%2)")
                                       .arg(exactitudVal.toDouble(), 0, 'g', 12)
                                       .arg(valAccuracy.toDouble(), 0, 'g', 12));
    } else {
        consistencia["coincide"] = QJsonValue(QJsonValue::Null);  // falto evaluar o predecir
    }
    raiz["consistencia"] = consistencia;

    QString error;
    artefactoMetricas_ = QJsonObject();
    if (!corridas::escribirJson(QDir(dirCorrida_).filePath(metricas::ARCHIVO), raiz, &error)) {
        protocolo::diagnostico(error);
        artefactoMetricas_["archivo"] = QJsonValue(QJsonValue::Null);
        artefactoMetricas_["error"] = error;
        return;
    }
    artefactoMetricas_["archivo"] = metricas::ARCHIVO;
    artefactoMetricas_["tipo"] = "derivado";
}

void ExperimentManager::abortar(const QString& tipo, const QString& motivo)
{
    protocolo::diagnostico("Corrida abortada: " + motivo);
    estadoFinal_ = "error";
    errorTipo_ = tipo;
    errorMensaje_ = motivo;
    detenerMedicion();
}

void ExperimentManager::fallarAntesDeEmpezar(const QString& tipo, const QString& motivo)
{
    protocolo::diagnostico("No se pudo iniciar la corrida: " + motivo);
    estadoFinal_ = "error";
    errorTipo_ = tipo;
    errorMensaje_ = motivo;
    QJsonObject ev;
    ev["estado"] = estadoFinal_;
    ev["dir_corrida"] = QJsonValue(QJsonValue::Null);
    ev["error"] = errorJson();
    protocolo::emitir("fin", ev);
    QMetaObject::invokeMethod(this, [this] { emit finalizado(SALIDA_ERROR); }, Qt::QueuedConnection);
}

void ExperimentManager::finalizarCorrida()
{
    if (finalizada_) return;
    finalizada_ = true;
    tFinMs_ = reloj::ahoraMs();

    // La DLL se libera al terminar este proceso, no con FreeLibrary (ver DllModelo.h): con
    // libtorch y varios hilos, la descarga bloqueaba la corrida para siempre
    dll_.soltarHastaSalir();
    dllDescargada_ = false;

    if (cfg_.medidorEnergia) calcularEnergia();
    escribirMetadata(estadoFinal_);
    corriendo_ = false;
    auditar();

    QJsonObject ev;
    ev["estado"] = estadoFinal_;
    ev["dir_corrida"] = QDir::toNativeSeparators(dirCorrida_);
    ev["error"] = errorJson().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(errorJson());
    ev["modelo_liberado"] = liberacionModelo_;
    ev["dll_descargada"] = dllDescargada_;
    ev["dll_descarga"] = "al_terminar_el_proceso";
    protocolo::emitir("fin", ev);

    const int codigo = estadoFinal_ == "completada" ? SALIDA_COMPLETADA
                     : estadoFinal_ == "cancelada" ? SALIDA_CANCELADA : SALIDA_ERROR;
    emit finalizado(codigo);
}

void ExperimentManager::calcularEnergia()
{
    // Dato derivado (D-008), calculado despues de detener los medidores y fuera de la medicion
    QJsonObject resultados;
    QString error;
    resultadosEnergia_ = QJsonObject();
    fases::Marcas marcas;
    marcas.tStartMs = tStartMs_;
    marcas.tAntesMs = tAntesMs_;
    marcas.tDespuesMs = tDespuesMs_;
    marcas.tStopMs = tStopMs_;
    marcas.posteriorIniciada = fasePosteriorIniciada_;
    marcas.origenPosterior = "metadata";
    marcas.tInfAntesMs = tInfAntesMs_;
    marcas.tInfDespuesMs = tInfDespuesMs_;
    marcas.mediaIniciada = faseMediaIniciada_;
    marcas.inferenciaIniciada = inferenciaIniciada_;
    if (!energia::resultadosCorrida(dirCorrida_, marcas, &resultados, &error, inferenciaJson())) {
        protocolo::diagnostico("Energia del experimento no calculada: " + error);
        resultadosEnergia_["archivo"] = QJsonValue(QJsonValue::Null);
        resultadosEnergia_["error"] = error;
        return;
    }
    if (!corridas::escribirJson(QDir(dirCorrida_).filePath(energia::ARCHIVO_RESULTADOS), resultados, &error)) {
        protocolo::diagnostico(error);
        resultadosEnergia_["archivo"] = QJsonValue(QJsonValue::Null);
        resultadosEnergia_["error"] = error;
        return;
    }
    resultadosEnergia_["archivo"] = energia::ARCHIVO_RESULTADOS;
    resultadosEnergia_["tipo"] = "derivado";
    QJsonObject ev;
    ev["energia_experimento"] = resultados["energia_experimento"].toObject()["ventanas"];
    ev["archivo"] = energia::ARCHIVO_RESULTADOS;
    protocolo::emitir("energia_experimento", ev);
}

void ExperimentManager::auditar()
{
    // Auditoria (D-012): informa, no bloquea. Despues de resultados.json y del metadata.json
    // final, con la medicion ya detenida.
    QJsonObject a;
    QString error;
    if (!auditoria::auditarYGuardar(dirCorrida_, &a, &error)) {
        protocolo::diagnostico("Auditoria no calculada: " + error);
        return;
    }
    QJsonArray resumen;
    for (const QJsonValue& v : a["comprobaciones"].toArray()) {
        const QJsonObject c = v.toObject();
        resumen.append(QJsonObject{{"id", c["id"]}, {"resultado", c["resultado"]}, {"motivo", c["motivo"]}});
    }
    QJsonObject ev;
    ev["veredicto"] = a["veredicto"];
    ev["version_reglas"] = a["version_reglas"];
    ev["comprobaciones"] = resumen;
    ev["archivo"] = auditoria::ARCHIVO;
    protocolo::emitir("auditoria", ev);
}

void ExperimentManager::fase(const QString& nombre)
{
    QJsonObject ev;
    ev["fase"] = nombre;
    protocolo::emitir("fase", ev);
}

QJsonObject ExperimentManager::errorJson() const
{
    if (errorMensaje_.isEmpty()) return QJsonObject();
    QJsonObject e;
    e["tipo"] = errorTipo_;
    e["mensaje"] = errorMensaje_;
    return e;
}

// ------------------------------------------------------------------------- metadata
void ExperimentManager::escribirMetadata(const QString& estado)
{
    QJsonObject modelo;
    modelo["clave"] = texto(modelo_.info.clave);
    modelo["nombre"] = texto(modelo_.info.nombre);
    modelo["libreria"] = texto(modelo_.info.libreria);
    modelo["unidad_paso"] = texto(modelo_.info.unidad_paso);
    // hilos: los que la DLL declara para el equipo (en el MLP, los automaticos de libtorch).
    // hilos_efectivos: los que uso la corrida (D2): el hiperparametro 'hilos' si existe y es > 0
    modelo["hilos"] = modelo_.info.hilos;
    modelo["hilos_efectivos"] = hilosEfectivos(hiperUsados_, modelo_.info.hilos);
    modelo["dll"] = QFileInfo(modelo_.ruta).fileName();
    modelo["interno"] = modelo_.interno;
    modelo["version_contrato"] = ENERGIA_API_VERSION;

    QJsonObject entrenamiento;
    entrenamiento["status_dll"] = status_;
    entrenamiento["status_texto"] = status_ >= 0 ? QJsonValue(describirEstado(status_)) : QJsonValue(QJsonValue::Null);
    entrenamiento["pasos_registrados"] = pasosRegistrados_;
    entrenamiento["modelo_liberado"] = liberacionModelo_;
    entrenamiento["dll_descargada"] = dllDescargada_;
    if (finalizada_) entrenamiento["dll_descarga"] = "al_terminar_el_proceso";

    QJsonObject tiempos;
    tiempos["t_creacion"] = tiempo(tCreacionMs_);
    tiempos["t_start"] = tiempo(tStartMs_);
    tiempos["t_antes"] = tiempo(tAntesMs_);
    tiempos["t_despues"] = tiempo(tDespuesMs_);
    tiempos["t_stop"] = tiempo(tStopMs_);
    tiempos["t_fin"] = tiempo(tFinMs_);
    tiempos["duracion_entrenamiento_ms"] = (tAntesMs_ > 0 && tDespuesMs_ > 0) ? QJsonValue(tDespuesMs_ - tAntesMs_)
                                                                               : QJsonValue(QJsonValue::Null);
    tiempos["margen_inicio_ms"] = margenEfectivoMs_;
    tiempos["margen_inicio_solicitado_ms"] = cfg_.margenInicioMs;
    tiempos["medidores_lanzados_despues_de_t_start"] = medidoresTarde_;
    tiempos["fase_previa_ms"] = cfg_.fasePreviaMs;
    tiempos["fase_posterior_ms"] = cfg_.fasePosteriorMs;
    // Si la linea base posterior llego a empezar: decide la fase base_post de la serie y su
    // ventana de energia (FasesCorrida.h). Una cancelacion durante el entrenamiento la salta.
    tiempos["fase_posterior_iniciada"] = fasePosteriorIniciada_;
    // Inferencia (D-014), con la misma logica: si la linea base intermedia y la inferencia llegaron
    // a empezar. Sin la inferencia activada, fase_media_ms es null y las marcas quedan en false/null.
    tiempos["t_inf_antes"] = tiempo(tInfAntesMs_);
    tiempos["t_inf_despues"] = tiempo(tInfDespuesMs_);
    tiempos["fase_media_ms"] = cfg_.inferenciaActivada ? QJsonValue(cfg_.faseMediaMs) : QJsonValue(QJsonValue::Null);
    tiempos["fase_media_iniciada"] = faseMediaIniciada_;
    tiempos["inferencia_iniciada"] = inferenciaIniciada_;

    QJsonObject artefactos;
    artefactos["config"] = ARCHIVO_CONFIG;
    artefactos["progreso"] = ARCHIVO_PROGRESO;
    artefactos["progreso_inferencia"] = inferenciaIniciada_ ? QJsonValue(ARCHIVO_PROGRESO_INFERENCIA) : QJsonValue(QJsonValue::Null);
    artefactos["modelo"] = artefactoModelo_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(artefactoModelo_);
    artefactos["resultados"] = resultadosEnergia_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(resultadosEnergia_);
    // Metricas del modelo (D-015): null si no se calcularon (sin entrenamiento completo)
    artefactos["metricas_modelo"] = artefactoMetricas_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(artefactoMetricas_);

    QJsonObject raiz;
    raiz["version_metadata"] = VERSION_METADATA;
    // El nombre del experimento se guarda tal como se escribio; la carpeta usa el nombre seguro
    // (corridas::nombreSeguro) y de ella sale id_corrida. Las corridas anteriores no tienen
    // experimento_carpeta: su 'experimento' ya era el nombre seguro.
    const QString carpeta = corridas::nombreSeguro(cfg_.experimento);
    raiz["experimento"] = cfg_.experimento;
    raiz["experimento_carpeta"] = carpeta;
    raiz["corrida"] = numCorrida_;
    raiz["id_corrida"] = carpeta + QString("/corrida_%1").arg(numCorrida_, 3, 10, QChar('0'));
    raiz["estado"] = estado;
    raiz["error"] = errorJson().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(errorJson());
    raiz["modelo"] = modelo;
    raiz["hiperparametros"] = hiperUsados_;
    raiz["datos"] = resumenDatos_;
    raiz["guardar_modelo"] = cfg_.guardarModelo;
    raiz["entrenamiento"] = entrenamiento;
    raiz["evaluacion"] = evaluacion_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(evaluacion_);
    raiz["inferencia"] = inferenciaJson();
    raiz["tiempos"] = tiempos;
    raiz["artefactos"] = artefactos;
    raiz["medidores"] = medidores_.resumen();  // sus metricas quedan en sus propios archivos
    raiz["protocolo_orquestador"] = protocolo::VERSION;

    QString error;
    if (!corridas::escribirJson(QDir(dirCorrida_).filePath(ARCHIVO_METADATA), raiz, &error))
        protocolo::diagnostico(error);
}
