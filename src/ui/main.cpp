#include "ClienteOrquestador.h"
#include "ClienteSupabase.h"
#include "ColaSubidas.h"
#include "Historial.h"
#include "HistorialRemoto.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QSysInfo>

// Opciones de linea de comandos (todas opcionales):
//   --orquestador <ruta>   energia_orquestador.exe (por defecto, junto a ui.exe)
//   --dir-modelos <ruta>   carpeta de las DLL de modelos (por defecto, la del orquestador)
//   --dir-salida <ruta>    raiz de corridas/ (por defecto, Documentos/EnergIA)
//   --desarrollo           ofrece la DLL de prueba interna y los datos sinteticos
//   --laboratorio <ruta>   laboratorio.json (por defecto, junto a ui.exe): MAC del Atorch y rutas
int main(int argc, char *argv[]) {
    // Estilo que respeta la personalización y la paleta oscura común (components/PaletaOscura.qml,
    // que asignan Main.qml y VentanaAcceso.qml; el estilo nativo de Windows ignora ambas). Se puede
    // cambiar con QT_QUICK_CONTROLS_STYLE.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle("Fusion");
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("EnergIA");

    QCommandLineParser parser;
    const QCommandLineOption optOrquestador("orquestador", "Ruta de energia_orquestador.", "ruta");
    const QCommandLineOption optDirModelos("dir-modelos", "Carpeta de las DLL de modelos.", "ruta");
    const QCommandLineOption optDirSalida("dir-salida", "Carpeta raiz de las corridas.", "ruta");
    const QCommandLineOption optDesarrollo("desarrollo", "Modo de desarrollo: DLL de prueba y datos sinteticos.");
    const QCommandLineOption optLaboratorio("laboratorio", "Configuracion del laboratorio (JSON).", "ruta");
    parser.addOptions({optOrquestador, optDirModelos, optDirSalida, optDesarrollo, optLaboratorio});
    if (!parser.parse(app.arguments()))
        qWarning("%s", qPrintable(parser.errorText()));

    ClienteOrquestador::Opciones opciones;
#ifdef Q_OS_WIN
    const QString nombreOrquestador = "energia_orquestador.exe";
#else
    const QString nombreOrquestador = "energia_orquestador";
#endif
    opciones.rutaOrquestador = parser.isSet(optOrquestador)
        ? QFileInfo(parser.value(optOrquestador)).absoluteFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(nombreOrquestador);
    const QString rutaPreferencias =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("preferencias.json");
    QJsonObject preferencias;
    QFile fPreferencias(rutaPreferencias);
    if (fPreferencias.open(QIODevice::ReadOnly))
        preferencias = QJsonDocument::fromJson(fPreferencias.readAll()).object();
    auto carpetaPreferida = [&preferencias](const char* clave, const QString& defecto) {
        const QString ruta = preferencias.value(QLatin1String(clave)).toString();
        return QFileInfo(ruta).isDir() ? QDir::cleanPath(QFileInfo(ruta).absoluteFilePath()) : defecto;
    };
    opciones.dirModelos = parser.isSet(optDirModelos)
        ? QFileInfo(parser.value(optDirModelos)).absoluteFilePath()
        : carpetaPreferida("dir_modelos", QFileInfo(opciones.rutaOrquestador).absolutePath());
    opciones.dirSalida = parser.isSet(optDirSalida)
        ? QFileInfo(parser.value(optDirSalida)).absoluteFilePath()
        : carpetaPreferida("dir_salida", QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("EnergIA"));
    opciones.dirDatasets = carpetaPreferida("dir_datasets",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    opciones.modoDesarrollo = parser.isSet(optDesarrollo);
    QFile laboratorio(parser.isSet(optLaboratorio) ? parser.value(optLaboratorio)
                                                  : QDir(QCoreApplication::applicationDirPath()).filePath("laboratorio.json"));
    if (laboratorio.open(QIODevice::ReadOnly)) {
        opciones.laboratorio = QJsonDocument::fromJson(laboratorio.readAll()).object().toVariantMap();
        // Rutas relativas: desde la carpeta de laboratorio.json (una instalacion copiada con
        // scripts/desplegar_windows.bat lleva el medidor en medidores\ junto a ui.exe)
        const QDir dirLab = QFileInfo(laboratorio.fileName()).absoluteDir();
        for (const char* clave : {"python_medidor_energia", "script_medidor_energia"}) {
            const QString ruta = opciones.laboratorio.value(clave).toString();
            if (!ruta.isEmpty() && QFileInfo(ruta).isRelative())
                opciones.laboratorio[clave] = QDir::cleanPath(dirLab.absoluteFilePath(ruta));
        }
    }

    // Linea base por defecto de las corridas nuevas (pagina Configuracion), entre ejecuciones
    opciones.rutaPreferencias = rutaPreferencias;
    ClienteOrquestador cliente(opciones);
    Historial historial(opciones.dirSalida);
    QObject::connect(&cliente, &ClienteOrquestador::carpetasCambiadas, &historial, [&] {
        if (historial.dirSalida() != cliente.dirSalida()) historial.establecerDirSalida(cliente.dirSalida());
    });

    // Sesion de la base de datos (D-011): el refresh_token se guarda en la carpeta de datos
    ClienteSupabase::Opciones opSesion;
    opSesion.rutaSesion = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("sesion.json");
    ClienteSupabase sesion(opSesion);

    // Subida a la base al terminar cada corrida, con cola de pendientes (D-011)
    ColaSubidas::Opciones opCola;
    opCola.rutaCola = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("subidas.json");
    opCola.rutaHuellas = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("huellas_datasets.json");
    // La exportacion del modelo (D-013) reutiliza las huellas de la cola para las corridas viejas
    historial.setRutaHuellas(opCola.rutaHuellas);
    opCola.equipo = QSysInfo::machineHostName();
    // Modo de subida (D-012): en el mismo preferencias.json que la linea base por defecto
    opCola.rutaPreferencias = opciones.rutaPreferencias;
    ColaSubidas subidas(opCola, &sesion);
    QObject::connect(&cliente, &ClienteOrquestador::corridaTerminada, &subidas, &ColaSubidas::encolar);
    // Corrida activa: la cola se pausa (no arma paquetes ni envia) hasta que la corrida termine
    QObject::connect(&cliente, &ClienteOrquestador::estadoCambiado, &subidas, [&] { subidas.setCorridaActiva(cliente.activa()); });
    HistorialRemoto remoto(&sesion);
    // Lo que esta en la base marca las carpetas locales como subidas (Dashboard, Resultados),
    // aunque la cola no lo sepa (otra instalacion, cola borrada)
    QObject::connect(&remoto, &HistorialRemoto::cambiado, &subidas, [&] { subidas.sincronizarRemotas(remoto.corridas()); });
    // Las corridas propias que la cola tiene como subidas y ya no estan en la base (borradas por
    // fuera de la aplicacion) vuelven a "Solo local" (D3)
    QObject::connect(&remoto, &HistorialRemoto::listaCargada, &subidas, &ColaSubidas::depurarSubidas);
    // Migracion 008 (D-014): la lista de la base dice si se aplico; la cola sube las corridas con
    // inferencia que esperaban
    QObject::connect(&remoto, &HistorialRemoto::migracion008, &subidas, &ColaSubidas::setMigracion008);
    QObject::connect(&remoto, &HistorialRemoto::migracion009, &subidas, &ColaSubidas::setMigracion009);
    QObject::connect(&sesion, &ClienteSupabase::sesionActivada, &remoto, [&] {
        if (remoto.estado() != "cargando" && remoto.corridas().isEmpty()) remoto.recargar();
    });
    QObject::connect(&subidas, &ColaSubidas::corridaSubida, &remoto, [&] { remoto.recargar(); });
    QObject::connect(&remoto, &HistorialRemoto::corridaBorrada, &subidas, [&](qint64 id, const QString& error) {
        if (error.isEmpty()) subidas.olvidarRemota(id);
    });

    // Primero la ventana de acceso; Main se crea cuando la sesion permite entrar y se destruye
    // al cerrar sesion (el orquestador y el historial siguen vivos: una corrida no se pierde)
    QQmlApplicationEngine engine;
    QQmlComponent compAcceso(&engine, "EnergIA", "VentanaAcceso");
    QQmlComponent compMain(&engine, "EnergIA", "Main");
    QObject* acceso = compAcceso.createWithInitialProperties({{"sesion", QVariant::fromValue(&sesion)}});
    if (!acceso) {
        qWarning("%s", qPrintable(compAcceso.errorString()));
        return -1;
    }
    QPointer<QObject> ventanaMain;
    auto abrirMain = [&] {
        if (ventanaMain) return;
        ventanaMain = compMain.createWithInitialProperties({{"cliente", QVariant::fromValue(&cliente)},
                                                            {"historial", QVariant::fromValue(&historial)},
                                                            {"sesion", QVariant::fromValue(&sesion)},
                                                            {"subidas", QVariant::fromValue(&subidas)},
                                                            {"remoto", QVariant::fromValue(&remoto)}});
        if (!ventanaMain) {
            qWarning("%s", qPrintable(compMain.errorString()));
            QCoreApplication::exit(-1);
            return;
        }
        acceso->setProperty("visible", false);
    };
    QObject::connect(&sesion, &ClienteSupabase::sesionCambiada, &app, [&] {
        if (sesion.puedeEntrar()) abrirMain();
    });
    QObject::connect(&sesion, &ClienteSupabase::sesionCerrada, &app, [&] {
        acceso->setProperty("visible", true);
        if (ventanaMain) ventanaMain->deleteLater();
    });
    sesion.restaurarSesion();

    const int codigo = app.exec();
    delete ventanaMain.data();
    delete acceso;
    return codigo;
}
