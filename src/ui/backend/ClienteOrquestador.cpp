// ClienteOrquestador.cpp - ver ClienteOrquestador.h.
#include "ClienteOrquestador.h"

#include "DatosCsv.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QTemporaryFile>
#include <QTime>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int MAX_REGISTRO = 200;              // entradas del registro de eventos
constexpr int MAX_DIAGNOSTICO = 8192;          // bytes de stderr que se conservan
constexpr int MAX_LINEA = 1024 * 1024;         // una linea de stdout no puede ser mayor
const QByteArray ORDEN_CANCELAR = "{\"v\":1,\"orden\":\"cancelar\"}\n";

// Codigo de salida del orquestador segun el estado final (PROTOCOLO_ORQUESTADOR.md)
int codigoEsperado(const QString& estado)
{
    if (estado == ClienteOrquestador::ESTADO_COMPLETADA) return 0;
    if (estado == ClienteOrquestador::ESTADO_CANCELADA) return 5;
    return 6;
}

QString nativa(const QString& ruta) { return QDir::toNativeSeparators(ruta); }

} // namespace

ClienteOrquestador::ClienteOrquestador(const Opciones& opciones, QObject* parent)
    : QObject(parent), op_(opciones)
{
    limiteCatalogo_.setSingleShot(true);
    connect(&limiteCatalogo_, &QTimer::timeout, this, [this] {
        if (!procCatalogo_) return;
        errorCatalogo_ = QString("energia_orquestador --catálogo no respondió en %1 s.").arg(op_.limiteCatalogoMs / 1000);
        procCatalogo_->kill();  // alTerminarCatalogo informa el error
    });
    timerMetricas_.setInterval(1000);  // la rejilla del medidor es de 1 s (D-007)
    connect(&timerMetricas_, &QTimer::timeout, this, &ClienteOrquestador::leerMetricas);

    // Linea base por defecto guardada en una ejecucion anterior
    if (!op_.rutaPreferencias.isEmpty()) {
        QFile f(op_.rutaPreferencias);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonValue v = QJsonDocument::fromJson(f.readAll()).object()["linea_base_s"];
            if (v.isDouble() && v.toDouble() == std::floor(v.toDouble()) && v.toInt() >= 0 && v.toInt() <= LINEA_BASE_MAXIMA_S)
                lineaBaseS_ = v.toInt();
        }
    }
}

QString ClienteOrquestador::configurarCarpeta(const QString& tipo, const QUrl& carpeta)
{
    if (activa()) return "Espera a que termine el experimento antes de cambiar las carpetas.";
    if (tipo != "resultados" && tipo != "modelos" && tipo != "datasets")
        return "Tipo de carpeta desconocido.";
    if (tipo == "modelos" && procCatalogo_)
        return "Espera a que termine la búsqueda de modelos.";
    if (!carpeta.isLocalFile()) return "Selecciona una carpeta local del equipo.";
    const QFileInfo destino(carpeta.toLocalFile());
    if (!destino.isDir()) return "La carpeta seleccionada no existe.";
    if (tipo == "resultados" && !destino.isWritable())
        return "No tienes permiso para guardar resultados en esa carpeta.";
    const QString ruta = QDir::cleanPath(destino.absoluteFilePath());
    const QString actual = tipo == "resultados" ? op_.dirSalida
                          : tipo == "modelos" ? op_.dirModelos : op_.dirDatasets;
    if (QDir::cleanPath(actual) == ruta) return {};

    // Guardar primero: si falla la persistencia no se cambia la carpeta en memoria.
    if (!op_.rutaPreferencias.isEmpty()) {
        QJsonObject pref;
        QFile anterior(op_.rutaPreferencias);
        if (anterior.open(QIODevice::ReadOnly))
            pref = QJsonDocument::fromJson(anterior.readAll()).object();
        anterior.close();  // Windows no permite reemplazar un JSON aún abierto.
        const QString clave = tipo == "resultados" ? "dir_salida"
                            : tipo == "modelos" ? "dir_modelos" : "dir_datasets";
        pref[clave] = ruta;
        if (!QDir().mkpath(QFileInfo(op_.rutaPreferencias).absolutePath()))
            return "No se pudo preparar la carpeta de preferencias.";
        QSaveFile f(op_.rutaPreferencias);
        if (!f.open(QIODevice::WriteOnly))
            return "No se pudieron guardar las preferencias: " + f.errorString();
        f.write(QJsonDocument(pref).toJson(QJsonDocument::Indented));
        if (!f.commit()) return "No se pudieron guardar las preferencias: " + f.errorString();
    }
    if (tipo == "resultados") op_.dirSalida = ruta;
    else if (tipo == "modelos") op_.dirModelos = ruta;
    else op_.dirDatasets = ruta;
    emit carpetasCambiadas();
    if (tipo == "modelos") cargarCatalogo();
    return {};
}

void ClienteOrquestador::setLineaBaseSegundos(int segundos)
{
    segundos = std::clamp(segundos, 0, static_cast<int>(LINEA_BASE_MAXIMA_S));
    if (segundos == lineaBaseS_) return;
    lineaBaseS_ = segundos;
    emit lineaBaseCambiada();
    if (op_.rutaPreferencias.isEmpty()) return;
    QDir().mkpath(QFileInfo(op_.rutaPreferencias).absolutePath());
    QJsonObject pref;
    QFile previo(op_.rutaPreferencias);
    if (previo.open(QIODevice::ReadOnly)) pref = QJsonDocument::fromJson(previo.readAll()).object();
    previo.close();
    pref["linea_base_s"] = lineaBaseS_;
    QSaveFile f(op_.rutaPreferencias);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(pref).toJson(QJsonDocument::Indented));
        if (f.commit()) return;
    }
    emit notificacion("warning", "No se pudo guardar la línea base por defecto", QDir::toNativeSeparators(op_.rutaPreferencias));
}

ClienteOrquestador::~ClienteOrquestador()
{
    if (procCatalogo_) {
        procCatalogo_->disconnect(this);
        procCatalogo_->kill();
        procCatalogo_->waitForFinished(1000);
    }
    if (proceso_ && proceso_->state() != QProcess::NotRunning) {
        // D-010: cerrar la interfaz no detiene la corrida. Se cierran los canales, como ocurre
        // cuando ui.exe termina (stdin cerrado: el orquestador continua; stdout/stderr sin
        // lector: sus escrituras fallan sin detenerlo), y se suelta el QProcess sin destruirlo,
        // porque su destructor terminaria el proceso del orquestador.
        proceso_->disconnect(this);
        proceso_->closeWriteChannel();
        proceso_->closeReadChannel(QProcess::StandardOutput);
        proceso_->closeReadChannel(QProcess::StandardError);
        proceso_->setParent(nullptr);
        proceso_ = nullptr;
    }
}

bool ClienteOrquestador::orquestadorEncontrado() const
{
    return !op_.rutaOrquestador.isEmpty() && QFileInfo(op_.rutaOrquestador).isFile();
}

bool ClienteOrquestador::activa() const
{
    return proceso_ != nullptr;
}

// ------------------------------------------------------------------------------ catalogo

void ClienteOrquestador::cargarCatalogo()
{
    if (procCatalogo_) return;  // ya se esta cargando

    auto fallar = [this](const QString& mensaje) {
        estadoCatalogo_ = "error";
        errorCatalogo_ = mensaje;
        modelos_.clear();
        descartadas_.clear();
        internosOcultos_ = 0;
        emit catalogoCambiado();
        emit notificacion("warning", "No se pudo cargar el catálogo de modelos", mensaje);
    };
    if (!orquestadorEncontrado()) {
        fallar("No se encontró energia_orquestador en " + nativa(op_.rutaOrquestador) + ".");
        return;
    }
    if (!QDir(op_.dirModelos).exists()) {
        fallar("El directorio de modelos no existe: " + nativa(op_.dirModelos) + ".");
        return;
    }

    procCatalogo_ = new QProcess(this);
    connect(procCatalogo_, &QProcess::finished, this, &ClienteOrquestador::alTerminarCatalogo);
    connect(procCatalogo_, &QProcess::errorOccurred, this, [this, fallar](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !procCatalogo_) return;  // el resto llega por finished
        const QString motivo = procCatalogo_->errorString();
        limiteCatalogo_.stop();
        procCatalogo_->deleteLater();
        procCatalogo_ = nullptr;
        fallar("No se pudo ejecutar energia_orquestador: " + motivo);
    });

    estadoCatalogo_ = "cargando";
    errorCatalogo_.clear();
    emit catalogoCambiado();
    limiteCatalogo_.start(op_.limiteCatalogoMs);
    procCatalogo_->start(op_.rutaOrquestador, {"--catalogo", "--dir-modelos", op_.dirModelos});
}

void ClienteOrquestador::alTerminarCatalogo(int codigo, QProcess::ExitStatus status)
{
    if (!procCatalogo_) return;
    limiteCatalogo_.stop();
    const QByteArray salida = procCatalogo_->readAllStandardOutput();
    const QString errores = QString::fromUtf8(procCatalogo_->readAllStandardError()).trimmed();
    procCatalogo_->deleteLater();
    procCatalogo_ = nullptr;

    modelos_.clear();
    descartadas_.clear();
    internosOcultos_ = 0;

    QString problema;
    QJsonObject raiz;
    if (!errorCatalogo_.isEmpty()) {
        problema = errorCatalogo_;  // limite de tiempo
    } else if (status != QProcess::NormalExit) {
        problema = "energia_orquestador --catálogo terminó de forma anormal.";
    } else if (codigo != 0) {
        problema = QString("energia_orquestador --catálogo terminó con código %1").arg(codigo)
                   + (errores.isEmpty() ? "." : ": " + errores);
    } else {
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(salida, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
            problema = "La respuesta del catálogo no es un JSON válido.";
        else
            raiz = doc.object();
    }

    if (!problema.isEmpty()) {
        estadoCatalogo_ = "error";
        errorCatalogo_ = problema;
        emit catalogoCambiado();
        emit notificacion("warning", "No se pudo cargar el catálogo de modelos", problema);
        return;
    }

    for (const QJsonValue& v : raiz["modelos"].toArray()) {
        const QJsonObject m = v.toObject();
        // La DLL de prueba es una herramienta interna (D-009): solo se ofrece en modo de desarrollo
        if (m["interno"].toBool() && !op_.modoDesarrollo) {
            ++internosOcultos_;
            continue;
        }
        modelos_.append(m.toVariantMap());
    }
    descartadas_ = raiz["descartadas"].toArray().toVariantList();
    estadoCatalogo_ = "listo";
    errorCatalogo_.clear();
    emit catalogoCambiado();

    if (!descartadas_.isEmpty())
        emit notificacion("warning", "DLL de modelos ignoradas",
                          QString("%1 DLL del directorio de modelos no cumplen el contrato v4 y se ignoraron.")
                              .arg(descartadas_.size()));
}

// ------------------------------------------------------------------------------ corrida

QJsonObject ClienteOrquestador::construirConfig(const QVariantMap& seleccion, QString* error) const
{
    const QString experimento = seleccion.value("experimento").toString().trimmed();
    if (experimento.isEmpty()) { *error = "Indica un nombre para el experimento."; return {}; }

    const QString dll = seleccion.value("modelo").toString();
    if (dll.isEmpty()) { *error = "Selecciona un tipo de modelo."; return {}; }

    QJsonObject modelo;
    modelo["dll"] = nativa(dll);
    modelo["hiperparametros"] = QJsonObject::fromVariantMap(seleccion.value("hiperparametros").toMap());

    const QVariantMap selDatos = seleccion.value("datos").toMap();
    const QString origen = selDatos.value("origen").toString();
    QJsonObject datos;
    if (origen == "csv") {
        const QString ruta = selDatos.value("ruta").toString();
        const QString columna = selDatos.value("columna_objetivo").toString();
        if (ruta.isEmpty()) { *error = "Selecciona el archivo CSV de datos."; return {}; }
        if (columna.isEmpty()) { *error = "Selecciona la columna objetivo del CSV."; return {}; }
        datos["origen"] = "csv";
        datos["ruta"] = nativa(ruta);
        datos["columna_objetivo"] = columna;
    } else if (origen == "sintetico") {
        // Datos de prueba: no son un dataset real; solo en modo de desarrollo
        if (!op_.modoDesarrollo) { *error = "Los datos sintéticos solo están disponibles en modo de desarrollo."; return {}; }
        datos["origen"] = "sintetico";
    } else {
        *error = "Selecciona el origen de los datos.";
        return {};
    }

    QJsonObject particion;
    particion["porcentaje_entrenamiento"] = seleccion.value("porcentaje_entrenamiento", 80).toInt();
    particion["semilla"] = seleccion.value("semilla", 42).toLongLong();

    QJsonObject cfg;
    cfg["version_config"] = 1;
    cfg["experimento"] = experimento;
    cfg["dir_salida"] = nativa(op_.dirSalida);
    cfg["modelo"] = modelo;
    cfg["datos"] = datos;
    cfg["particion"] = particion;
    cfg["guardar_modelo"] = seleccion.value("guardar_modelo", false).toBool();
    // Linea base: el mismo valor antes y despues del entrenamiento (ver LINEA_BASE_DEFECTO_S)
    const QVariant vLinea = seleccion.value("linea_base_s", lineaBaseS_);
    bool okLinea = false;
    const double lineaBase = vLinea.toDouble(&okLinea);
    if (!okLinea || lineaBase < 0 || lineaBase > LINEA_BASE_MAXIMA_S || lineaBase != std::floor(lineaBase)) {
        *error = QString("La línea base debe ser un número entero de segundos entre 0 y %1.").arg(LINEA_BASE_MAXIMA_S);
        return {};
    }
    const qint64 lineaBaseMs = static_cast<qint64>(lineaBase) * 1000;
    cfg["tiempos"] = QJsonObject{{"fase_previa_ms", lineaBaseMs}, {"fase_posterior_ms", lineaBaseMs}};
    // Inferencia (D-014), opcional: la linea base intermedia dura lo mismo que la linea base
    const QVariantMap inf = seleccion.value("inferencia").toMap();
    if (inf.value("activada").toBool()) {
        auto entero = [&inf](const char* clave, int defecto, int minimo, int maximo, int* destino) {
            bool ok = false;
            const double v = inf.value(clave, defecto).toDouble(&ok);
            if (!ok || v < minimo || v > maximo || v != std::floor(v)) return false;
            *destino = static_cast<int>(v);
            return true;
        };
        int duracion = 0, lote = 0;
        if (!entero("duracion_min_s", INFERENCIA_DURACION_DEFECTO_S, 1, INFERENCIA_DURACION_MAXIMA_S, &duracion)) {
            *error = QString("La duración mínima de la inferencia debe ser un número entero de segundos entre 1 y %1.")
                         .arg(INFERENCIA_DURACION_MAXIMA_S);
            return {};
        }
        if (!entero("lote", INFERENCIA_LOTE_DEFECTO, 1, INFERENCIA_LOTE_MAXIMO, &lote)) {
            *error = QString("El lote de inferencia debe ser un número entero de filas entre 1 y %1.").arg(INFERENCIA_LOTE_MAXIMO);
            return {};
        }
        cfg["inferencia"] = QJsonObject{{"activada", true}, {"lote", lote}, {"duracion_min_s", duracion}};
        QJsonObject tiempos = cfg["tiempos"].toObject();
        tiempos["fase_media_ms"] = lineaBaseMs;
        cfg["tiempos"] = tiempos;
    }
    if (seleccion.contains("medidores"))
        cfg["medidores"] = QJsonObject::fromVariantMap(seleccion.value("medidores").toMap());
    if (seleccion.value("medidores").toMap().value("energia").toBool()) {
        // Datos del laboratorio (config/laboratorio.json); vacios: valores por defecto del orquestador
        QJsonObject ble;
        const QString mac = op_.laboratorio.value("atorch_mac").toString();
        const QString python = op_.laboratorio.value("python_medidor_energia").toString();
        const QString script = op_.laboratorio.value("script_medidor_energia").toString();
        if (!mac.isEmpty()) ble["mac"] = mac;
        if (!python.isEmpty()) ble["python"] = nativa(python);
        if (!script.isEmpty()) ble["script"] = nativa(script);
        if (!ble.isEmpty()) cfg["energia_ble"] = ble;
    }
    return cfg;
}

QString ClienteOrquestador::iniciar(const QVariantMap& seleccion)
{
    if (activa()) return "Ya hay una corrida en curso.";
    if (!orquestadorEncontrado())
        return "No se encontró energia_orquestador en " + nativa(op_.rutaOrquestador) + ".";

    QString error;
    const QJsonObject cfg = construirConfig(seleccion, &error);
    if (!error.isEmpty()) return error;
    if (op_.dirSalida.isEmpty() || !QDir().mkpath(op_.dirSalida))
        return "No se pudo crear el directorio de salida: " + nativa(op_.dirSalida) + ".";

    // El orquestador lee la configuracion de un archivo y la copia a la carpeta de la corrida.
    // El QTemporaryFile se destruye antes de lanzar el proceso: si el lanzamiento falla de
    // inmediato, el archivo ya no debe estar en uso para poder borrarlo.
    QString rutaConfig;
    {
        QTemporaryFile tmp(QDir::temp().filePath("energia_config_XXXXXX.json"));
        tmp.setAutoRemove(false);
        if (!tmp.open()) return "No se pudo escribir la configuración temporal: " + tmp.errorString();
        tmp.write(QJsonDocument(cfg).toJson(QJsonDocument::Indented));
        rutaConfig = tmp.fileName();
    }

    reiniciarCorrida();
    rutaConfig_ = rutaConfig;
    corrida_["experimento"] = cfg["experimento"].toString();
    corrida_["modelo_nombre"] = seleccion.value("modelo_nombre").toString();
    corrida_["modelo_dll"] = nativa(seleccion.value("modelo").toString());
    corrida_["datos_descripcion"] = seleccion.value("datos_descripcion").toString();
    corrida_["guardar_modelo"] = cfg["guardar_modelo"].toBool();
    corrida_["linea_base_s"] = cfg["tiempos"].toObject()["fase_previa_ms"].toInteger() / 1000;
    corrida_["inferencia"] = cfg.contains("inferencia") ? cfg["inferencia"].toObject().toVariantMap()
                                                        : QVariantMap{{"activada", false}};
    corrida_["solicitada"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    emit corridaCambiada();

    proceso_ = new QProcess(this);
    proceso_->setProgram(op_.rutaOrquestador);
    proceso_->setArguments({"--ejecutar", rutaConfig_});
    connect(proceso_, &QProcess::readyReadStandardOutput, this, [this] {
        if (proceso_) procesarSalida(proceso_->readAllStandardOutput());
    });
    connect(proceso_, &QProcess::readyReadStandardError, this, [this] {
        if (!proceso_) return;
        diagnostico_ += proceso_->readAllStandardError();
        if (diagnostico_.size() > MAX_DIAGNOSTICO) diagnostico_ = diagnostico_.right(MAX_DIAGNOSTICO);
    });
    connect(proceso_, &QProcess::finished, this, &ClienteOrquestador::alTerminarCorrida);
    connect(proceso_, &QProcess::errorOccurred, this, &ClienteOrquestador::alFallarCorrida);

    ponerEstado(ESTADO_INICIANDO);
    anotar("info", "Iniciando el proceso de ejecución (energia_orquestador)");
    proceso_->start();
    return {};
}

void ClienteOrquestador::cancelar()
{
    if (!proceso_ || proceso_->state() != QProcess::Running) return;
    if (estado_ != ESTADO_INICIANDO && estado_ != ESTADO_EN_CURSO) return;
    proceso_->write(ORDEN_CANCELAR);
    ponerEstado(ESTADO_CANCELANDO);
    anotar("warning", "Orden de cancelación enviada");
}

void ClienteOrquestador::forzarDetencion()
{
    if (!proceso_ || proceso_->state() == QProcess::NotRunning) return;
    forzado_ = true;
    anotar("warning", "Deteniendo el proceso de ejecución de forma forzada");
    proceso_->kill();  // alTerminarCorrida informa el resultado
}

void ClienteOrquestador::descartarResultado()
{
    if (activa()) return;
    reiniciarCorrida();
    ponerEstado(ESTADO_INACTIVO);
}

void ClienteOrquestador::procesarSalida(const QByteArray& datos)
{
    pendiente_ += datos;
    qsizetype fin;
    while ((fin = pendiente_.indexOf('\n')) >= 0) {
        const QByteArray linea = pendiente_.left(fin).trimmed();
        pendiente_.remove(0, fin + 1);
        if (linea.isEmpty()) continue;

        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(linea, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            anotar("warning", "Se ignoró una línea de salida que no es un evento del protocolo");
            continue;
        }
        const QJsonObject ev = doc.object();
        if (ev["v"].toInt() != 1 || ev["evento"].toString().isEmpty()) {
            anotar("warning", "Se ignoró un evento de una versión de protocolo desconocida");
            continue;
        }
        procesarEvento(ev);
    }
    if (pendiente_.size() > MAX_LINEA) {
        pendiente_.clear();
        anotar("warning", "Se descartó una línea de salida demasiado larga");
    }
}

void ClienteOrquestador::procesarEvento(const QJsonObject& ev)
{
    const QString nombre = ev["evento"].toString();

    if (nombre == "inicio") {
        corrida_["experimento"] = ev["experimento"].toString();
        corrida_["corrida"] = ev["corrida"].toInt();
        corrida_["dir_corrida"] = ev["dir_corrida"].toString();
        corrida_["modelo_clave"] = ev["modelo"].toString();
        corrida_["modelo_interno"] = ev["modelo_interno"].toBool();
        emit corridaCambiada();
        medidoresCorrida_.clear();
        for (const QJsonValue& m : ev["medidores"].toArray()) {
            QVariantMap e;
            e["nombre"] = m.toString();
            e["estado"] = "pendiente";
            medidoresCorrida_.append(e);
        }
        emit metricasCambiadas();
        if (estado_ == ESTADO_INICIANDO) ponerEstado(ESTADO_EN_CURSO);
        anotar("info", QString("Corrida %1 del experimento '%2' creada")
                           .arg(ev["corrida"].toInt(), 3, 10, QChar('0'))
                           .arg(ev["experimento"].toString()));
    } else if (nombre == "fase") {
        fase_ = ev["fase"].toString();
        emit estadoCambiado();
        anotar("info", "Fase: " + textoFase(fase_));
    } else if (nombre == "datos_preparados") {
        const QJsonObject d = ev["datos"].toObject();
        corrida_["datos"] = d.toVariantMap();
        emit corridaCambiada();
        QString detalle;
        if (d.contains("n_train"))
            detalle = QString(" (%1 filas de entrenamiento, %2 de validación)")
                          .arg(d["n_train"].toVariant().toLongLong())
                          .arg(d["n_val"].toVariant().toLongLong());
        anotar("info", "Datos preparados" + detalle);
    } else if (nombre == "t_start") {
        corrida_["t_start"] = ev["t_start"].toString();
        emit corridaCambiada();
        const QDateTime t = QDateTime::fromString(ev["t_start"].toString(), Qt::ISODateWithMs);
        anotar("info", "Instante común de inicio (t_start): "
                           + (t.isValid() ? t.toLocalTime().toString("HH:mm:ss") : ev["t_start"].toString()));
    } else if (nombre == "progreso" && ev["tarea"].toString() == "inferencia") {
        // Un evento por grupo de pasadas (D-014): pasadas y predicciones acumuladas
        progresoInferencia_["grupos"] = ev["paso"].toInt() + 1;
        progresoInferencia_["pasadas"] = ev["pasadas"].toVariant();
        progresoInferencia_["predicciones"] = ev["predicciones"].toVariant();
        emit progresoCambiado();
    } else if (nombre == "progreso") {
        totalPasos_ = ev["total"].toInt();
        unidadPaso_ = ev["unidad"].toString();
        const int paso = ev["paso"].toInt();
        if (ev["momento"].toString() == "inicio_paso") {
            pasoActual_ = paso + 1;
        } else {
            pasoActual_ = paso + 1;
            pasosCompletados_ = paso + 1;
            metricasPaso_ = ev["metricas"].toObject().toVariantMap();
        }
        emit progresoCambiado();
    } else if (nombre == "entrenamiento_terminado") {
        QVariantMap e;
        for (const char* clave : {"status_dll", "status_texto", "t_antes", "t_despues", "duracion_ms", "pasos_registrados"})
            e[clave] = ev[clave].toVariant();
        corrida_["entrenamiento"] = e;
        emit corridaCambiada();
        anotar("info", QString("Entrenamiento terminado: %1 (%2 s, %3 pasos)")
                           .arg(ev["status_texto"].toString())
                           .arg(ev["duracion_ms"].toDouble() / 1000.0, 0, 'f', 1)
                           .arg(ev["pasos_registrados"].toInt()));
    } else if (nombre == "inferencia_terminada") {
        QVariantMap inf = corrida_.value("inferencia").toMap();
        for (auto it = ev.begin(); it != ev.end(); ++it)
            if (it.key() != "v" && it.key() != "evento" && it.key() != "t") inf[it.key()] = it.value().toVariant();
        corrida_["inferencia"] = inf;
        emit corridaCambiada();
        anotar(ev["status_dll"].toInt() == 0 ? "info" : "warning",
               QString("Inferencia terminada: %1 (%2 s, %3 pasadas, %4 predicciones)")
                   .arg(ev["status_texto"].toString())
                   .arg(ev["duracion_ms"].toDouble() / 1000.0, 0, 'f', 1)
                   .arg(ev["pasadas"].toVariant().toLongLong())
                   .arg(ev["n_predicciones"].toVariant().toLongLong()));
    } else if (nombre == "cancelacion_solicitada") {
        if (estado_ == ESTADO_INICIANDO || estado_ == ESTADO_EN_CURSO) ponerEstado(ESTADO_CANCELANDO);
        anotar("warning", "El orquestador recibió la cancelación; esperando a que el modelo se detenga");
    } else if (nombre == "evaluacion") {
        const QJsonObject e = ev["evaluacion"].toObject();
        corrida_["evaluacion"] = e.toVariantMap();
        emit corridaCambiada();
        QStringList metricas;
        const QJsonObject m = e["metricas"].toObject();
        for (auto it = m.begin(); it != m.end(); ++it)
            metricas << QString("%1 = %2").arg(it.key()).arg(it.value().toDouble(), 0, 'g', 4);
        anotar("info", "Evaluación en validación" + (metricas.isEmpty() ? QString() : ": " + metricas.join(", ")));
    } else if (nombre == "medidor") {
        const QString medidor = ev["nombre"].toString();
        const QString estadoMedidor = ev["estado"].toString();
        ponerEstadoMedidor(medidor, estadoMedidor);
        if (estadoMedidor == "lanzado") {
            if (medidor == "sistema") lector_.iniciar(corrida_.value("dir_corrida").toString());
            if (medidor == "energia") lectorEnergia_.iniciar(corrida_.value("dir_corrida").toString());
            timerMetricas_.start();
        }
        if (estadoMedidor == "termino_antes") {
            anotar("warning", QString("El medidor '%1' terminó antes de tiempo").arg(medidor));
            emit notificacion("warning", "Medidor detenido", QString("El medidor '%1' terminó antes de tiempo.").arg(medidor));
        } else if (estadoMedidor == "forzado") {
            anotar("warning", QString("El medidor '%1' no respondió a stop.flag y se cerró a la fuerza").arg(medidor));
        } else {
            anotar("info", QString("Medidor '%1': %2").arg(medidor, estadoMedidor));
        }
        if (estadoMedidor != "lanzado") leerMetricas();  // ultimas filas del medidor
    } else if (nombre == "energia_experimento") {
        const QVariantMap ventanas = ev["energia_experimento"].toObject().toVariantMap();
        corrida_["energia_experimento"] = ventanas;
        emit corridaCambiada();
        const QVariantMap ent = ventanas.value("entrenamiento").toMap();
        if (!ent.isEmpty())
            anotar("info", QString("Energía del experimento (entrenamiento): %1 Wh, cobertura %2 %")
                               .arg(ent.value("energia_wh").toDouble(), 0, 'f', 4)
                               .arg(100.0 * ent.value("cobertura").toDouble(), 0, 'f', 1));
    } else if (nombre == "auditoria") {
        // Auditoria de la corrida (D-012): informa; no bloquea la corrida ni la subida
        QVariantMap a;
        a["veredicto"] = ev["veredicto"].toString();
        a["version_reglas"] = ev["version_reglas"].toInt();
        a["comprobaciones"] = ev["comprobaciones"].toArray().toVariantList();
        corrida_["auditoria"] = a;
        emit corridaCambiada();
        const QString v = ev["veredicto"].toString();
        anotar(v == "valida" ? "success" : v == "no_valida" ? "error" : "warning",
               "Auditoría de la corrida: " + QString(v == "valida" ? "válida" : v == "no_valida" ? "no válida" : "con advertencias"));
    } else if (nombre == "fin") {
        finRecibido_ = true;
        if (timerMetricas_.isActive()) { leerMetricas(); timerMetricas_.stop(); }
        fase_.clear();
        if (!ev["dir_corrida"].isNull()) corrida_["dir_corrida"] = ev["dir_corrida"].toString();
        corrida_["t_fin"] = ev["t"].toString();
        corrida_["error"] = ev["error"].isObject() ? QVariant(ev["error"].toObject().toVariantMap()) : QVariant();
        corrida_["modelo_liberado"] = ev["modelo_liberado"].toString();
        corrida_["dll_descargada"] = ev["dll_descargada"].toBool();
        emit corridaCambiada();

        const QString estadoFinal = ev["estado"].toString();
        const QString experimento = corrida_.value("experimento").toString();
        if (estadoFinal == "completada") {
            ponerEstado(ESTADO_COMPLETADA);
            anotar("success", "Corrida completada");
            emit notificacion("success", "Experimento completado",
                              QString("La corrida de '%1' terminó correctamente.").arg(experimento));
        } else if (estadoFinal == "cancelada") {
            ponerEstado(ESTADO_CANCELADA);
            anotar("warning", "Corrida cancelada");
            emit notificacion("info", "Experimento cancelado",
                              QString("La corrida de '%1' se canceló.").arg(experimento));
        } else {
            const QJsonObject err = ev["error"].toObject();
            const QString mensaje = err["mensaje"].toString().isEmpty() ? "Error sin detalle." : err["mensaje"].toString();
            ponerEstado(ESTADO_ERROR);
            anotar("error", "La corrida terminó con error: " + mensaje);
            emit notificacion("error", "El experimento terminó con error", mensaje);
        }
    }
    // Los eventos desconocidos se ignoran: el protocolo puede crecer sin romper la interfaz
}

void ClienteOrquestador::alTerminarCorrida(int codigo, QProcess::ExitStatus status)
{
    if (!proceso_) return;
    procesarSalida(proceso_->readAllStandardOutput());
    if (!pendiente_.isEmpty()) procesarSalida("\n");  // ultima linea sin salto
    diagnostico_ += proceso_->readAllStandardError();

    if (!finRecibido_) {
        if (forzado_) {
            cerrarConError("detencion_forzada",
                           "Se detuvo el proceso de ejecución a petición del usuario. La corrida quedó incompleta.");
        } else {
            const QString motivo = (status != QProcess::NormalExit)
                ? QString("terminó de forma anormal")
                : QString("terminó con código %1 sin informar el final de la corrida").arg(codigo);
            const QString diag = ultimoDiagnostico();
            cerrarConError("proceso", "El proceso de ejecución (energia_orquestador) " + motivo + "."
                                          + (diag.isEmpty() ? QString() : " Último diagnóstico: " + diag));
        }
    } else if (status != QProcess::NormalExit || codigo != codigoEsperado(estado_)) {
        anotar("warning", QString("El orquestador terminó con código %1; se esperaba %2")
                              .arg(codigo).arg(codigoEsperado(estado_)));
    }

    if (timerMetricas_.isActive()) { leerMetricas(); timerMetricas_.stop(); }
    proceso_->deleteLater();
    proceso_ = nullptr;
    borrarConfigTemporal();
    emit estadoCambiado();
    const QString dir = corrida_.value("dir_corrida").toString();
    if (!dir.isEmpty()) emit corridaTerminada(dir);
}

void ClienteOrquestador::alFallarCorrida(QProcess::ProcessError error)
{
    if (!proceso_) return;
    if (error == QProcess::FailedToStart) {
        // No hay 'finished': el proceso nunca existio
        const QString motivo = proceso_->errorString();
        proceso_->deleteLater();
        proceso_ = nullptr;
        borrarConfigTemporal();
        cerrarConError("proceso", "No se pudo iniciar energia_orquestador: " + motivo);
        emit estadoCambiado();
    } else if (error == QProcess::WriteError) {
        anotar("warning", "No se pudo enviar la orden al orquestador: " + proceso_->errorString());
    }
    // Crashed y el resto se informan en alTerminarCorrida
}

void ClienteOrquestador::cerrarConError(const QString& tipo, const QString& mensaje)
{
    QVariantMap err;
    err["tipo"] = tipo;
    err["mensaje"] = mensaje;
    corrida_["error"] = err;
    fase_.clear();
    emit corridaCambiada();
    ponerEstado(ESTADO_ERROR);
    anotar("error", mensaje);
    emit notificacion("error", "El experimento no terminó correctamente", mensaje);
}

void ClienteOrquestador::ponerEstado(const QString& estado)
{
    if (estado_ == estado) return;
    estado_ = estado;
    emit estadoCambiado();
}

void ClienteOrquestador::anotar(const QString& tipo, const QString& mensaje)
{
    QVariantMap e;
    e["hora"] = QTime::currentTime().toString("HH:mm:ss");
    e["tipo"] = tipo;
    e["mensaje"] = mensaje;
    registro_.prepend(e);  // el mas reciente primero
    while (registro_.size() > MAX_REGISTRO) registro_.removeLast();
    emit registroCambiado();
}

void ClienteOrquestador::reiniciarCorrida()
{
    pendiente_.clear();
    diagnostico_.clear();
    fase_.clear();
    corrida_.clear();
    pasosCompletados_ = 0;
    pasoActual_ = 0;
    totalPasos_ = 0;
    unidadPaso_.clear();
    metricasPaso_.clear();
    progresoInferencia_.clear();
    registro_.clear();
    finRecibido_ = false;
    forzado_ = false;
    timerMetricas_.stop();
    lector_.reiniciar();
    lectorEnergia_.reiniciar();
    medidoresCorrida_.clear();
    metricasSistema_.clear();
    historialSistema_.clear();
    metricasEnergia_.clear();
    historialEnergia_.clear();
    emit metricasCambiadas();
    emit corridaCambiada();
    emit progresoCambiado();
    emit registroCambiado();
}

void ClienteOrquestador::borrarConfigTemporal()
{
    if (!rutaConfig_.isEmpty()) QFile::remove(rutaConfig_);
    rutaConfig_.clear();
}

QString ClienteOrquestador::ultimoDiagnostico() const
{
    const QList<QByteArray> lineas = diagnostico_.split('\n');
    for (auto it = lineas.crbegin(); it != lineas.crend(); ++it) {
        QString l = QString::fromUtf8(*it).trimmed();
        if (l.isEmpty()) continue;
        if (l.startsWith("[energia_orquestador ")) {
            const qsizetype cierre = l.indexOf(']');
            if (cierre > 0) l = l.mid(cierre + 1).trimmed();
        }
        return l;
    }
    return {};
}

void ClienteOrquestador::leerMetricas()
{
    bool cambio = false;
    if (lectorEnergia_.activo()) {
        for (const QVariant& v : lectorEnergia_.actualizar()) {
            const QVariantMap fila = v.toMap();
            for (const char* serie : {"potencia_w", "voltaje_v", "corriente_a"}) {
                if (!fila.contains(serie)) continue;
                QVariantList h = historialEnergia_.value(serie).toList();
                h.append(fila.value(serie));
                while (h.size() > MAX_HISTORIAL) h.removeFirst();
                historialEnergia_[serie] = h;
            }
            metricasEnergia_ = fila;
            cambio = true;
        }
    }
    const QVariantList filas = lector_.activo() ? lector_.actualizar() : QVariantList();
    if (filas.isEmpty()) {
        if (cambio) emit metricasCambiadas();
        return;
    }
    for (const QVariant& v : filas) {
        const QVariantMap fila = v.toMap();
        for (const char* serie : {"cpu_proceso_pct", "cpu_sistema_pct", "ram_proceso_mb", "ram_sistema_pct"}) {
            if (!fila.contains(serie)) continue;
            QVariantList h = historialSistema_.value(serie).toList();
            h.append(fila.value(serie));
            while (h.size() > MAX_HISTORIAL) h.removeFirst();
            historialSistema_[serie] = h;
        }
        metricasSistema_ = fila;
    }
    emit metricasCambiadas();
}

void ClienteOrquestador::ponerEstadoMedidor(const QString& nombre, const QString& estado)
{
    for (QVariant& v : medidoresCorrida_) {
        QVariantMap m = v.toMap();
        if (m.value("nombre").toString() == nombre) {
            m["estado"] = estado;
            v = m;
            emit metricasCambiadas();
            return;
        }
    }
    QVariantMap m;
    m["nombre"] = nombre;
    m["estado"] = estado;
    medidoresCorrida_.append(m);
    emit metricasCambiadas();
}

QVariantMap ClienteOrquestador::medidorEnergiaLaboratorio() const
{
    const QDir dirMed(QString::fromUtf8(ENERGIA_DIR_MEDIDORES));
#ifdef Q_OS_WIN
    const QString pythonDefecto = dirMed.filePath("venv/Scripts/python.exe");
#else
    const QString pythonDefecto = dirMed.filePath("venv/bin/python");
#endif
    QString python = op_.laboratorio.value("python_medidor_energia").toString();
    QString script = op_.laboratorio.value("script_medidor_energia").toString();
    if (python.isEmpty()) python = pythonDefecto;
    if (script.isEmpty()) script = dirMed.filePath("energia_ble.py");
    QVariantMap r;
    r["mac"] = op_.laboratorio.value("atorch_mac").toString();
    r["python"] = nativa(python);
    r["python_existe"] = QFileInfo(python).isFile();
    r["script"] = nativa(script);
    r["script_existe"] = QFileInfo(script).isFile();
    return r;
}

QVariantMap ClienteOrquestador::disponibilidadMonitores() const
{
#ifdef Q_OS_WIN
    const QString medidorSistema = "system_meter.exe";
#else
    const QString medidorSistema = "system_meter";
#endif
    const QVariantMap lab = medidorEnergiaLaboratorio();
    QVariantMap r;
    r["sistema"] = QFileInfo(QFileInfo(op_.rutaOrquestador).absoluteDir().filePath(medidorSistema)).isFile();
    r["energia"] = lab.value("python_existe").toBool() && lab.value("script_existe").toBool();
    return r;
}

QVariantMap ClienteOrquestador::infoSistema() const
{
    QVariantMap r;
    r["sistema_operativo"] = QSysInfo::prettyProductName();
    r["arquitectura"] = QSysInfo::currentCpuArchitecture();
    r["nucleos_logicos"] = QThread::idealThreadCount();
    r["version_qt"] = QString::fromLatin1(qVersion());
#ifdef Q_OS_WIN
    const QSettings cpu("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
    r["cpu"] = cpu.value("ProcessorNameString").toString().trimmed();
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) r["memoria_total_gb"] = static_cast<double>(m.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
#endif
    return r;
}

// ------------------------------------------------------------------------------ utilidades

QVariantMap ClienteOrquestador::encabezadoCsv(const QString& ruta) const
{
    QVariantMap r;
    QStringList columnas;
    char separador = ',';
    QString error;
    const bool ok = datos::leerEncabezado(ruta, &columnas, &separador, &error);
    r["ok"] = ok;
    r["columnas"] = columnas;
    r["separador"] = (separador == '\t') ? QString("tabulación") : QString(QChar(separador));
    r["error"] = error;
    return r;
}

QString ClienteOrquestador::textoFase(const QString& fase) const
{
    if (fase == "preparando_datos") return "Preparando datos";
    if (fase == "margen_inicio") return "Margen de inicio";
    if (fase == "fase_previa") return "Línea base (antes del entrenamiento)";
    if (fase == "entrenando") return "Entrenando";
    if (fase == "fase_posterior") return "Línea base (después del entrenamiento)";
    if (fase == "fase_media") return "Línea base intermedia";
    if (fase == "inferencia") return "Inferencia";
    if (fase == "cancelando_inferencia") return "Cancelando la inferencia";
    if (fase == "cancelando") return "Cancelando";
    if (fase == "cancelando_entrenamiento") return "Cancelando el entrenamiento";
    if (fase == "cerrando_modelo") return "Cerrando el modelo (evaluación y liberación)";
    if (fase == "deteniendo_medidores") return "Deteniendo los medidores";
    return fase;
}

QString ClienteOrquestador::rutaLocal(const QUrl& url) const
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
