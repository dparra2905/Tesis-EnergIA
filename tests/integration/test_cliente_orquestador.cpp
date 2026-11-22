// test_cliente_orquestador.cpp - ClienteOrquestador contra el energia_orquestador real (F4).
//
// Es el camino que recorre la interfaz, sin la parte grafica: catalogo, corrida completa,
// cancelacion, detencion forzada, orquestador terminado desde fuera, configuracion invalida,
// fallo al iniciar y cierre de la interfaz durante una corrida (D-010).

#include "ClienteOrquestador.h"
#include "Historial.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    std::fflush(stdout);
    if (!condicion) ++g_fallos;
}

// Procesa eventos hasta que se cumpla la condicion o venza el plazo
bool esperar(const std::function<bool()>& condicion, int ms)
{
    QElapsedTimer reloj;
    reloj.start();
    while (!condicion()) {
        if (reloj.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

bool terminada(const ClienteOrquestador& c)
{
    return !c.activa() && (c.estado() == "completada" || c.estado() == "cancelada" || c.estado() == "error");
}

QJsonObject leerMetadata(const QString& dirCorrida)
{
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QVariantMap seleccion(int pasos, int msPorPaso)
{
    QVariantMap hiper;
    hiper["pasos"] = pasos;
    hiper["ms_por_paso"] = msPorPaso;
    QVariantMap datos;
    datos["origen"] = "sintetico";
    QVariantMap s;
    s["experimento"] = "prueba_f4";
    s["linea_base_s"] = 0;  // sin linea base: la prueba no necesita esperar 40 s por corrida
    s["modelo"] = QString::fromUtf8(ENERGIA_DLL_PRUEBA);
    s["modelo_nombre"] = "Prueba (simulado)";
    s["hiperparametros"] = hiper;
    s["datos"] = datos;
    return s;
}

int contarConfigsTemporales()
{
    return QDir::temp().entryList({"energia_config_*.json"}, QDir::Files).size();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString orquestador = QString::fromUtf8(ENERGIA_ORQUESTADOR);
    const QString dirModelos = QFileInfo(QString::fromUtf8(ENERGIA_DLL_PRUEBA)).absolutePath();
    QTemporaryDir salida;

    auto opciones = [&](bool desarrollo) {
        ClienteOrquestador::Opciones o;
        o.rutaOrquestador = orquestador;
        o.dirModelos = dirModelos;
        o.dirSalida = salida.path();
        o.modoDesarrollo = desarrollo;
        return o;
    };
    const int configsAntes = contarConfigsTemporales();

    // Carpetas elegidas desde Configuración: efecto inmediato, validación y persistencia.
    {
        QTemporaryDir prefs;
        comprobar(prefs.isValid(), "directorio temporal para preferencias");
        QDir raiz(prefs.path());
        comprobar(raiz.mkpath("resultados") && raiz.mkpath("modelos") && raiz.mkpath("datasets"),
                  "tres carpetas de prueba disponibles");
        ClienteOrquestador::Opciones op = opciones(true);
        op.dirDatasets = raiz.filePath("resultados");
        op.rutaPreferencias = raiz.filePath("preferencias.json");
        ClienteOrquestador c(op);
        Historial h(op.dirSalida);
        QObject::connect(&c, &ClienteOrquestador::carpetasCambiadas, &h, [&] {
            if (h.dirSalida() != c.dirSalida()) h.establecerDirSalida(c.dirSalida());
        });
        const QString rutaResultado = raiz.filePath("resultados");
        comprobar(c.configurarCarpeta("resultados", QUrl::fromLocalFile(rutaResultado)).isEmpty(),
                  "carpeta de resultados elegida");
        comprobar(c.dirSalida() == rutaResultado && h.dirSalida() == rutaResultado,
                  "cliente e historial usan la nueva carpeta sin reiniciar");
        const QString rutaDatasets = raiz.filePath("datasets");
        comprobar(c.configurarCarpeta("datasets", QUrl::fromLocalFile(rutaDatasets)).isEmpty()
                  && c.dirDatasets() == rutaDatasets, "carpeta de datasets guardada");
        const QString rutaModelos = raiz.filePath("modelos");
        comprobar(c.configurarCarpeta("modelos", QUrl::fromLocalFile(rutaModelos)).isEmpty()
                  && c.dirModelos() == rutaModelos, "carpeta de modelos guardada y catálogo recargado");
        comprobar(!c.configurarCarpeta("resultados", QUrl::fromLocalFile(raiz.filePath("no_existe"))).isEmpty()
                  && c.dirSalida() == rutaResultado, "se rechazan rutas inexistentes sin perder la anterior");
        comprobar(!c.configurarCarpeta("otro", QUrl::fromLocalFile(rutaDatasets)).isEmpty(),
                  "se rechazan tipos de carpeta desconocidos");
        QFile f(op.rutaPreferencias);
        comprobar(f.open(QIODevice::ReadOnly), "preferencias guardadas en disco");
        const QJsonObject guardadas = QJsonDocument::fromJson(f.readAll()).object();
        comprobar(guardadas["dir_salida"].toString() == rutaResultado
                  && guardadas["dir_modelos"].toString() == rutaModelos
                  && guardadas["dir_datasets"].toString() == rutaDatasets,
                  "las tres carpetas persisten en preferencias.json");
    }

    // ------------------------------------------------------------ 1. catalogo
    std::printf("\n== 1. Catalogo\n");
    {
        ClienteOrquestador c(opciones(true));
        c.cargarCatalogo();
        comprobar(esperar([&] { return c.estadoCatalogo() != "cargando"; }, 30000), "el catalogo responde");
        comprobar(c.estadoCatalogo() == "listo", "modo de desarrollo: catalogo listo (" + c.errorCatalogo() + ")");
        bool hayPrueba = false;
        for (const QVariant& m : c.modelos())
            if (m.toMap().value("clave").toString() == "prueba" && m.toMap().value("hiperparametros").toList().size() == 5)
                hayPrueba = true;
        comprobar(hayPrueba, "modo de desarrollo: el modelo 'prueba' aparece con sus 5 hiperparametros");
    }
    {
        ClienteOrquestador c(opciones(false));
        c.cargarCatalogo();
        esperar([&] { return c.estadoCatalogo() != "cargando"; }, 30000);
        // En bin/ puede haber ademas modelos reales (compilar_dlls.bat u obtener_modelos.bat):
        // lo que se exige es que la DLL de prueba no aparezca
        bool apareceLaPrueba = false;
        for (const QVariant& m : c.modelos())
            if (m.toMap().value("clave").toString() == "prueba") apareceLaPrueba = true;
        comprobar(c.estadoCatalogo() == "listo" && !apareceLaPrueba && c.modelosInternosOcultos() == 1,
                  QString("modo normal: la DLL de prueba interna queda oculta (D-009) (%1 modelo(s) reales visibles)")
                      .arg(c.modelos().size()));
    }

    // ------------------------------------------------------------ 2. corrida completa
    std::printf("\n== 2. Corrida completa (3 pasos de 50 ms)\n");
    {
        ClienteOrquestador c(opciones(true));
        int exitos = 0;
        QObject::connect(&c, &ClienteOrquestador::notificacion, [&](const QString& tipo, const QString&, const QString&) {
            if (tipo == "success") ++exitos;
        });
        comprobar(c.iniciar(seleccion(3, 50)).isEmpty(), "iniciar() lanza el orquestador");
        comprobar(c.activa(), "la corrida queda activa");
        comprobar(esperar([&] { return terminada(c); }, 60000), "la corrida termina");
        comprobar(c.estado() == "completada", "estado completada (" + c.estado() + ")");
        comprobar(c.pasosCompletados() == 3 && c.totalPasos() == 3 && c.unidadPaso() == "paso", "progreso 3 / 3 paso");
        const QString dir = c.corrida().value("dir_corrida").toString();
        comprobar(!dir.isEmpty() && QDir(dir).exists(), "la carpeta de la corrida existe: " + dir);
        comprobar(leerMetadata(dir)["estado"].toString() == "completada", "metadata.json en estado completada");
        comprobar(!c.corrida().value("t_start").toString().isEmpty() && !c.corrida().value("t_fin").toString().isEmpty(),
                  "t_start y t_fin recibidos del orquestador");
        comprobar(c.corrida().value("evaluacion").toMap().value("ejecutada").toBool(), "evaluacion recibida");
        comprobar(exitos == 1, "una notificacion de exito");
        comprobar(!c.registro().isEmpty(), "el registro de eventos tiene entradas");
        comprobar(contarConfigsTemporales() == configsAntes, "se borro la configuracion temporal");
    }

    // ------------------------------------------------------------ 3. cancelacion
    std::printf("\n== 3. Cancelacion cooperativa\n");
    {
        ClienteOrquestador c(opciones(true));
        c.iniciar(seleccion(50, 200));
        comprobar(esperar([&] { return c.pasosCompletados() >= 1; }, 30000), "la corrida avanza al menos un paso");
        c.cancelar();
        comprobar(c.estado() == "cancelando", "estado cancelando tras la orden");
        comprobar(esperar([&] { return terminada(c); }, 30000), "la corrida termina tras cancelar");
        comprobar(c.estado() == "cancelada" && c.pasosCompletados() < 50, "estado cancelada antes del final");
    }

    // ------------------------------------------------------------ 4. detencion forzada
    std::printf("\n== 4. Detencion forzada\n");
    {
        ClienteOrquestador c(opciones(true));
        QVariantMap s = seleccion(50, 200);
        s["medidores"] = QVariantMap{{"sistema", true}, {"energia", false}};
        c.iniciar(s);
        esperar([&] { return c.pasosCompletados() >= 1; }, 30000);
        const QString dir = c.corrida().value("dir_corrida").toString();
        c.cancelar();
        c.forzarDetencion();
        comprobar(esperar([&] { return terminada(c); }, 30000), "el proceso termina");
        comprobar(c.estado() == "error" && c.corrida().value("error").toMap().value("tipo").toString() == "detencion_forzada",
                  "estado error de tipo detencion_forzada");
        comprobar(leerMetadata(dir)["estado"].toString() == "en_curso",
                  "metadata.json queda en_curso: la corrida forzada se reconoce como incompleta");
        // Nadie crea stop.flag: system_meter vigila el PID del orquestador y se cierra solo (D-010)
        auto motivoMedidor = [&] {
            QFile f(QDir(dir).filePath("sistema_info.json"));
            if (!f.open(QIODevice::ReadOnly)) return QString();
            return QJsonDocument::fromJson(f.readAll()).object()["motivo_fin"].toString();
        };
        const bool cerrado = esperar([&] { return motivoMedidor() == "orquestador_terminado"; }, 5000);
        comprobar(cerrado, "system_meter se cierra al morir el orquestador (motivo_fin " + motivoMedidor() + ")");
        comprobar(!QFile::exists(QDir(dir).filePath("stop.flag")), "la interfaz no escribe stop.flag");
    }

    // ------------------------------------------------------------ 5. orquestador terminado desde fuera
    std::printf("\n== 5. Orquestador terminado desde fuera\n");
    {
        ClienteOrquestador c(opciones(true));
        int errores = 0;
        QObject::connect(&c, &ClienteOrquestador::notificacion, [&](const QString& tipo, const QString&, const QString&) {
            if (tipo == "error") ++errores;
        });
        c.iniciar(seleccion(50, 200));
        esperar([&] { return c.pasosCompletados() >= 1; }, 30000);
        const qint64 pid = c.pidEjecucion();
        comprobar(pid > 0, "PID del proceso de ejecucion");
        QProcess::execute("taskkill", {"/F", "/PID", QString::number(pid)});
        comprobar(esperar([&] { return terminada(c); }, 30000), "el cliente detecta el fin del proceso");
        comprobar(c.estado() == "error" && c.corrida().value("error").toMap().value("tipo").toString() == "proceso",
                  "estado error de tipo proceso: " + c.corrida().value("error").toMap().value("mensaje").toString());
        comprobar(errores == 1, "una notificacion de error");
    }

    // ------------------------------------------------------------ 6. configuracion invalida
    std::printf("\n== 6. Configuracion invalida\n");
    {
        ClienteOrquestador c(opciones(true));
        c.iniciar(seleccion(0, 50));  // pasos fuera de rango (1..1000)
        comprobar(esperar([&] { return terminada(c); }, 30000), "termina");
        comprobar(c.estado() == "error"
                      && c.corrida().value("error").toMap().value("tipo").toString() == "configuracion"
                      && c.corrida().value("dir_corrida").toString().isEmpty(),
                  "error de configuracion sin carpeta de corrida: "
                      + c.corrida().value("error").toMap().value("mensaje").toString());
    }

    // ------------------------------------------------------------ 7. fallos al lanzar
    std::printf("\n== 7. Programa que no es el orquestador / no se puede iniciar\n");
    {
        ClienteOrquestador::Opciones o = opciones(true);
        o.rutaOrquestador = QString::fromUtf8(ENERGIA_CMAKE);  // existe, pero no emite el protocolo
        ClienteOrquestador c(o);
        comprobar(c.iniciar(seleccion(3, 50)).isEmpty(), "se lanza un programa que no es el orquestador");
        comprobar(esperar([&] { return terminada(c); }, 30000), "termina");
        comprobar(c.estado() == "error" && c.corrida().value("error").toMap().value("tipo").toString() == "proceso",
                  "terminar sin el evento 'fin' es un error: " + c.corrida().value("error").toMap().value("mensaje").toString());
    }
    {
        QTemporaryDir tmp;
        const QString falso = tmp.filePath("no_ejecutable.txt");
        QFile f(falso);
        comprobar(f.open(QIODevice::WriteOnly), "se crea un archivo que no es un programa");
        f.write("no es un programa");
        f.close();
        ClienteOrquestador::Opciones o = opciones(true);
        o.rutaOrquestador = falso;
        ClienteOrquestador c(o);
        c.iniciar(seleccion(3, 50));
        comprobar(esperar([&] { return terminada(c); }, 30000), "termina");
        comprobar(c.estado() == "error", "un archivo que no se puede ejecutar: error ("
                                             + c.corrida().value("error").toMap().value("mensaje").toString() + ")");
    }

    // ------------------------------------------------------------ 8. cierre de la interfaz (D-010)
    std::printf("\n== 8. La interfaz se cierra durante la corrida (D-010)\n");
    {
        auto* c = new ClienteOrquestador(opciones(true));
        c->iniciar(seleccion(10, 150));
        comprobar(esperar([&] { return c->pasosCompletados() >= 1; }, 30000), "la corrida avanza");
        const QString dir = c->corrida().value("dir_corrida").toString();
        const qint64 pid = c->pidEjecucion();
        delete c;  // como al cerrar ui.exe
        comprobar(esperar([&] { return leerMetadata(dir)["estado"].toString() == "completada"; }, 60000),
                  "la corrida termina completada aunque el cliente ya no exista");
        comprobar(QFile::exists(QDir(dir).filePath("progreso.csv")), "progreso.csv escrito");
        comprobar(esperar([&] {
                      QProcess p;
                      p.start("tasklist", {"/FI", QString("PID eq %1").arg(pid), "/FO", "CSV", "/NH"});
                      p.waitForFinished(10000);
                      return !QString::fromLocal8Bit(p.readAllStandardOutput()).contains("energia_orquestador");
                  }, 15000),
                  "el proceso del orquestador termina por si solo");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "CLIENTE + ORQUESTADOR OK" : "CLIENTE + ORQUESTADOR CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
