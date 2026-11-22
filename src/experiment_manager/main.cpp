// main.cpp - energia_orquestador.exe: proceso de ejecucion sin interfaz (D-010).
//
// Solo usa Qt Core. Comandos:
//
//   energia_orquestador --catalogo --dir-modelos <ruta>
//       Escribe el catalogo de modelos en JSON por stdout.
//       Salida: 0 bien (aunque haya DLL descartadas), 2 argumentos invalidos,
//       3 el directorio de modelos no existe.
//
//   energia_orquestador --ejecutar <config.json>
//       Ejecuta una corrida. Eventos JSON Lines por stdout, ordenes JSON Lines por stdin,
//       diagnostico por stderr (docs/sdd/PROTOCOLO_ORQUESTADOR.md).
//       Salida: 0 completada, 5 cancelada, 6 error, 2 argumentos invalidos.
//
//   energia_orquestador --postprocesar <carpeta de la corrida>
//       Recalcula resultados.json (energia del experimento, D-008) con los archivos del
//       monitor fisico de energia y los tiempos de metadata.json; lo escribe en stdout. Despues
//       recalcula auditoria.json (D-012), tambien si no hay datos del medidor.
//       Salida: 0 bien, 3 sin metadata.json, 6 sin datos del medidor o error al escribir.

#include "Auditoria.h"
#include "CatalogoModelos.h"
#include "ConfigExperimento.h"
#include "Corridas.h"
#include "EnergiaExperimento.h"
#include "ExperimentManager.h"
#include "Protocolo.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QJsonDocument>

#include <cstdio>

namespace {

void error(const QString& mensaje)
{
    std::fprintf(stderr, "%s\n", mensaje.toUtf8().constData());
}

int comandoCatalogo(const QString& dirModelos)
{
    CatalogoModelos catalogo;
    if (!catalogo.explorar(dirModelos)) {
        error("El directorio de modelos no existe: " + QDir::toNativeSeparators(dirModelos));
        return 3;
    }
    for (const DllDescartada& d : catalogo.descartadas())
        error(QString("DLL ignorada (%1): %2").arg(QDir::toNativeSeparators(d.ruta), d.motivo));

    const QByteArray json = QJsonDocument(catalogo.aJson()).toJson(QJsonDocument::Indented);
    std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
    std::fflush(stdout);
    return 0;
}

// Recalcula resultados.json (energia del experimento, D-008) de una corrida ya terminada
int comandoPostprocesar(const QString& dirCorrida)
{
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        error("No se pudo leer metadata.json en " + QDir::toNativeSeparators(dirCorrida));
        return 3;
    }
    // Marcas de la linea de tiempo; en corridas viejas, la de la fase posterior se deduce. La
    // ventana de inferencia toma n_predicciones y el tiempo por prediccion de metadata.inferencia
    const QJsonObject metadata = QJsonDocument::fromJson(f.readAll()).object();
    const fases::Marcas marcas = fases::desdeMetadata(metadata);
    QJsonObject resultados;
    QString motivo;
    int codigo = 0;
    if (!energia::resultadosCorrida(dirCorrida, marcas, &resultados, &motivo, metadata["inferencia"].toObject())) {
        error(motivo);
        codigo = 6;
    } else if (!corridas::escribirJson(QDir(dirCorrida).filePath(energia::ARCHIVO_RESULTADOS), resultados, &motivo)) {
        error(motivo);
        codigo = 6;
    } else {
        const QByteArray json = QJsonDocument(resultados).toJson(QJsonDocument::Indented);
        std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
        std::fflush(stdout);
    }
    // La auditoria (D-012) usa resultados.json: se recalcula despues, haya o no datos de energia
    QJsonObject a;
    if (!auditoria::auditarYGuardar(dirCorrida, &a, &motivo)) {
        error(motivo);
        return 6;
    }
    error("auditoria.json: veredicto " + a["veredicto"].toString());
    return codigo;
}

int comandoEjecutar(QCoreApplication& app, const QString& rutaConfig)
{
    protocolo::inicializar();

    ExperimentManager manager;
    QObject::connect(&manager, &ExperimentManager::finalizado, &app, [&app](int codigo) { app.exit(codigo); });

    // Ordenes por stdin. El lector vive hasta que termine el proceso (su hilo puede seguir
    // bloqueado leyendo); si stdin se cierra la corrida continua (D-010).
    auto* lector = new protocolo::LectorOrdenes;
    QObject::connect(lector, &protocolo::LectorOrdenes::ordenRecibida, &manager,
                     [&manager](const QString& orden, const QJsonObject&) { manager.procesarOrden(orden); });
    QObject::connect(lector, &protocolo::LectorOrdenes::entradaCerrada, &manager,
                     [] { protocolo::diagnostico("stdin cerrado; la corrida continua"); });
    lector->iniciar();

    ConfigExperimento cfg;
    QString motivo;
    if (!config::cargar(rutaConfig, &cfg, &motivo)) {
        protocolo::diagnostico("Configuracion invalida: " + motivo);
        QJsonObject err;
        err["tipo"] = "configuracion";
        err["mensaje"] = motivo;
        QJsonObject ev;
        ev["estado"] = "error";
        ev["dir_corrida"] = QJsonValue(QJsonValue::Null);
        ev["error"] = err;
        protocolo::emitir("fin", ev);
        return ExperimentManager::SALIDA_ERROR;
    }

    QMetaObject::invokeMethod(&manager, [&manager, cfg] { manager.iniciar(cfg); }, Qt::QueuedConnection);
    return app.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("energia_orquestador");

    QCommandLineParser parser;
    parser.setApplicationDescription("Proceso de ejecucion de EnergIA (sin interfaz).");
    const QCommandLineOption optAyuda = parser.addHelpOption();
    const QCommandLineOption optCatalogo("catalogo", "Describe los modelos disponibles en JSON.");
    const QCommandLineOption optDirModelos("dir-modelos", "Directorio donde buscar las DLL de modelos.", "ruta");
    const QCommandLineOption optEjecutar("ejecutar", "Ejecuta una corrida descrita en un config.json.", "config");
    const QCommandLineOption optPostprocesar("postprocesar", "Recalcula resultados.json de una corrida.", "carpeta");
    parser.addOption(optPostprocesar);
    parser.addOption(optCatalogo);
    parser.addOption(optDirModelos);
    parser.addOption(optEjecutar);
    // parse() en lugar de process(): process() sale con codigo 1 ante un argumento invalido y
    // el protocolo reserva el 2 para argumentos invalidos
    if (!parser.parse(app.arguments())) {
        error(parser.errorText());
        return 2;
    }
    if (parser.isSet(optAyuda)) parser.showHelp(0);

    if (int(parser.isSet(optCatalogo)) + int(parser.isSet(optEjecutar)) + int(parser.isSet(optPostprocesar)) > 1) {
        error("Usa solo un comando: --catalogo, --ejecutar o --postprocesar");
        return 2;
    }
    if (parser.isSet(optPostprocesar)) {
        if (parser.value(optPostprocesar).isEmpty()) { error("--postprocesar requiere la carpeta de la corrida"); return 2; }
        return comandoPostprocesar(parser.value(optPostprocesar));
    }
    if (parser.isSet(optEjecutar)) {
        if (parser.value(optEjecutar).isEmpty()) { error("--ejecutar requiere la ruta de config.json"); return 2; }
        return comandoEjecutar(app, parser.value(optEjecutar));
    }
    if (parser.isSet(optCatalogo)) {
        if (!parser.isSet(optDirModelos) || parser.value(optDirModelos).isEmpty()) {
            error("--catalogo requiere --dir-modelos <ruta>");
            return 2;
        }
        return comandoCatalogo(parser.value(optDirModelos));
    }
    error("Falta el comando. Uso: energia_orquestador --catalogo --dir-modelos <ruta> | --ejecutar <config.json>"
          " | --postprocesar <carpeta>");
    return 2;
}
