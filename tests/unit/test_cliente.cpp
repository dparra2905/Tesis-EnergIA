// test_cliente.cpp - Pruebas de ClienteOrquestador sin lanzar procesos (F4).
//
// Comprueba la interpretacion de los eventos JSON Lines del orquestador (lineas partidas,
// lineas invalidas, progreso, fin), la construccion de config.json desde la seleccion de la
// interfaz y la lectura del encabezado de un CSV.

#include "ClienteOrquestador.h"
#include "LectorMetricasEnergia.h"
#include "LectorMetricasSistema.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

ClienteOrquestador::Opciones opciones(bool desarrollo)
{
    ClienteOrquestador::Opciones o;
    o.rutaOrquestador = "C:/no/existe/energia_orquestador.exe";
    o.dirModelos = "C:/no/existe";
    o.dirSalida = "C:/salida/energia";
    o.modoDesarrollo = desarrollo;
    return o;
}

QVariantMap seleccionValida()
{
    QVariantMap hiper;
    hiper["pasos"] = 5.0;      // desde QML los numeros llegan como double
    hiper["curva"] = "lineal";
    hiper["capas"] = "64,32";
    QVariantMap datos;
    datos["origen"] = "sintetico";
    QVariantMap s;
    s["experimento"] = "  prueba  ";
    s["modelo"] = "C:/bin/EnergIA_Prueba.dll";
    s["hiperparametros"] = hiper;
    s["datos"] = datos;
    s["porcentaje_entrenamiento"] = 70;
    s["semilla"] = 7.0;
    s["guardar_modelo"] = true;
    return s;
}

QByteArray evento(const QByteArray& json)
{
    return json + '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // ---------------------------------------------------------------- 1. config.json
    {
        ClienteOrquestador c(opciones(true));
        QString error;
        const QJsonObject cfg = c.construirConfig(seleccionValida(), &error);
        comprobar(error.isEmpty(), "seleccion valida: sin error (" + error + ")");
        comprobar(cfg["version_config"].toInt() == 1, "config: version_config 1");
        comprobar(cfg["experimento"].toString() == "prueba", "config: nombre del experimento sin espacios");
        comprobar(cfg["dir_salida"].toString() == QDir::toNativeSeparators("C:/salida/energia"), "config: dir_salida del cliente");
        const QJsonObject modelo = cfg["modelo"].toObject();
        comprobar(modelo["dll"].toString() == QDir::toNativeSeparators("C:/bin/EnergIA_Prueba.dll"), "config: ruta de la DLL");
        const QJsonObject h = modelo["hiperparametros"].toObject();
        comprobar(h["pasos"].toDouble() == 5 && h["curva"].toString() == "lineal" && h["capas"].toString() == "64,32",
                  "config: hiperparametros tal como los envia la interfaz");
        comprobar(cfg["datos"].toObject()["origen"].toString() == "sintetico", "config: datos sinteticos en modo de desarrollo");
        comprobar(cfg["particion"].toObject()["porcentaje_entrenamiento"].toInt() == 70
                      && cfg["particion"].toObject()["semilla"].toInteger() == 7,
                  "config: particion 70 % y semilla 7");
        comprobar(cfg["guardar_modelo"].toBool(), "config: guardar_modelo");
        const QJsonObject tiempos = cfg["tiempos"].toObject();
        comprobar(tiempos["fase_previa_ms"].toInteger() == 20000 && tiempos["fase_posterior_ms"].toInteger() == 20000,
                  "config: linea base de 20 s antes y despues del entrenamiento");
        comprobar(c.lineaBaseSegundos() == 20, "linea base por defecto de las corridas nuevas: 20 s");
        QVariantMap sinLinea = seleccionValida();
        sinLinea["linea_base_s"] = 0;
        QString e2;
        const QJsonObject cfg0 = c.construirConfig(sinLinea, &e2);
        comprobar(cfg0["tiempos"].toObject()["fase_previa_ms"].toInteger() == 0
                      && cfg0["tiempos"].toObject()["fase_posterior_ms"].toInteger() == 0,
                  "config: linea_base_s = 0 la quita en las dos fases");
        QVariantMap propia = seleccionValida();
        propia["linea_base_s"] = 45.0;  // desde QML llega como double
        const QJsonObject cfg45 = c.construirConfig(propia, &e2);
        comprobar(e2.isEmpty() && cfg45["tiempos"].toObject()["fase_previa_ms"].toInteger() == 45000
                      && cfg45["tiempos"].toObject()["fase_posterior_ms"].toInteger() == 45000,
                  "config: la linea base de la corrida (45 s) va a las dos fases");
        c.setLineaBaseSegundos(30);
        const QJsonObject cfg30 = c.construirConfig(seleccionValida(), &e2);
        comprobar(cfg30["tiempos"].toObject()["fase_previa_ms"].toInteger() == 30000,
                  "config: sin linea_base_s se usa el valor por defecto de Configuracion (30 s)");
        for (const QVariant& malo : {QVariant(-1), QVariant(3601), QVariant(2.5), QVariant("veinte")}) {
            QVariantMap m = seleccionValida();
            m["linea_base_s"] = malo;
            QString e3;
            c.construirConfig(m, &e3);
            comprobar(!e3.isEmpty(), "linea base invalida (" + malo.toString() + "): " + e3);
        }
        QVariantMap tope = seleccionValida();
        tope["linea_base_s"] = 3600;
        QString e4;
        const QJsonObject cfgTope = c.construirConfig(tope, &e4);
        comprobar(e4.isEmpty() && cfgTope["tiempos"].toObject()["fase_previa_ms"].toInteger() == 3600000,
                  "linea base de 3600 s (el tope de cada fase en el orquestador) aceptada");
        // Inferencia (D-014): sin activarla, el config.json es el de siempre
        comprobar(!cfg.contains("inferencia") && !tiempos.contains("fase_media_ms"),
                  "config sin inferencia: ni seccion 'inferencia' ni tiempos.fase_media_ms");
        QVariantMap conInf = seleccionValida();
        conInf["linea_base_s"] = 25;
        conInf["inferencia"] = QVariantMap{{"activada", true}, {"duracion_min_s", 90.0}, {"lote", 1.0}};
        QString e5;
        const QJsonObject cfgInf = c.construirConfig(conInf, &e5);
        const QJsonObject inf = cfgInf["inferencia"].toObject();
        comprobar(e5.isEmpty() && inf["activada"].toBool() && inf["duracion_min_s"].toInt() == 90 && inf["lote"].toInt() == 1
                      && cfgInf["tiempos"].toObject()["fase_media_ms"].toInteger() == 25000,
                  "config con inferencia: seccion inferencia y fase_media_ms = la linea base de la corrida");
        QVariantMap porDefecto = seleccionValida();
        porDefecto["inferencia"] = QVariantMap{{"activada", true}};
        const QJsonObject cfgDef = c.construirConfig(porDefecto, &e5);
        comprobar(cfgDef["inferencia"].toObject()["duracion_min_s"].toInt() == 60 && cfgDef["inferencia"].toObject()["lote"].toInt() == 65536,
                  "config con inferencia sin valores: 60 s y lote de 65.536 filas");
        for (const auto& malo : {QVariantMap{{"activada", true}, {"duracion_min_s", 0}}, QVariantMap{{"activada", true}, {"duracion_min_s", 3601}},
                                 QVariantMap{{"activada", true}, {"lote", 0}}, QVariantMap{{"activada", true}, {"lote", 1000001}},
                                 QVariantMap{{"activada", true}, {"lote", 2.5}}}) {
            QVariantMap m = seleccionValida();
            m["inferencia"] = malo;
            QString e6;
            c.construirConfig(m, &e6);
            comprobar(!e6.isEmpty(), "inferencia invalida: " + e6);
        }
    }
    // ---------------------------------------------------------------- 1b. linea base guardada
    {
        QTemporaryDir tmp;
        ClienteOrquestador::Opciones o = opciones(true);
        o.rutaPreferencias = tmp.filePath("datos/preferencias.json");
        {
            ClienteOrquestador c(o);
            comprobar(c.lineaBaseSegundos() == 20, "sin preferencias guardadas: 20 s");
            int avisos = 0;
            QObject::connect(&c, &ClienteOrquestador::lineaBaseCambiada, [&] { ++avisos; });
            c.setLineaBaseSegundos(12);
            c.setLineaBaseSegundos(5000);  // se recorta al tope
            comprobar(avisos == 2 && c.lineaBaseSegundos() == 3600, "el valor se recorta a 0..3600 y avisa el cambio");
            c.setLineaBaseSegundos(12);
        }
        ClienteOrquestador otra(o);
        comprobar(otra.lineaBaseSegundos() == 12, "la linea base por defecto se conserva entre ejecuciones (12 s)");
        QFile f(o.rutaPreferencias);
        comprobar(f.open(QIODevice::WriteOnly) && f.write("{\"linea_base_s\": -3}") > 0, "preferencias manipuladas");
        f.close();
        ClienteOrquestador tercera(o);
        comprobar(tercera.lineaBaseSegundos() == 20, "un valor guardado fuera de rango se ignora (20 s)");
    }
    {
        ClienteOrquestador c(opciones(false));
        QString error;
        c.construirConfig(seleccionValida(), &error);
        comprobar(!error.isEmpty(), "sin modo de desarrollo se rechazan los datos sinteticos: " + error);

        QVariantMap s = seleccionValida();
        QVariantMap datos;
        datos["origen"] = "csv";
        datos["ruta"] = "C:/datos/iris.csv";
        s["datos"] = datos;
        error.clear();
        c.construirConfig(s, &error);
        comprobar(!error.isEmpty(), "CSV sin columna objetivo: rechazado (" + error + ")");

        datos["columna_objetivo"] = "especie";
        s["datos"] = datos;
        error.clear();
        const QJsonObject cfg = c.construirConfig(s, &error);
        comprobar(error.isEmpty() && cfg["datos"].toObject()["columna_objetivo"].toString() == "especie"
                      && cfg["datos"].toObject()["ruta"].toString() == QDir::toNativeSeparators("C:/datos/iris.csv"),
                  "CSV con ruta y columna objetivo: aceptado");

        s["experimento"] = "   ";
        error.clear();
        c.construirConfig(s, &error);
        comprobar(!error.isEmpty(), "nombre de experimento vacio: rechazado");

        s = seleccionValida();
        s.remove("modelo");
        error.clear();
        c.construirConfig(s, &error);
        comprobar(!error.isEmpty(), "sin modelo: rechazado");
    }
    {
        ClienteOrquestador c(opciones(true));
        comprobar(!c.iniciar(seleccionValida()).isEmpty() && !c.activa() && c.estado() == "inactivo",
                  "iniciar() con el orquestador inexistente devuelve un motivo y no cambia de estado");
    }

    // ---------------------------------------------------------------- 2. eventos
    {
        ClienteOrquestador c(opciones(true));
        int notifExito = 0, notifError = 0;
        QObject::connect(&c, &ClienteOrquestador::notificacion, [&](const QString& tipo, const QString&, const QString&) {
            if (tipo == "success") ++notifExito;
            if (tipo == "error") ++notifError;
        });

        // Linea partida en dos fragmentos
        const QByteArray inicio = evento(R"({"v":1,"evento":"inicio","t":"2026-10-01T10:00:00.000Z","experimento":"prueba","corrida":3,"dir_corrida":"C:\\salida\\corridas\\prueba\\corrida_003","modelo":"prueba","modelo_interno":true})");
        c.procesarSalida(inicio.left(40));
        comprobar(c.corrida().value("corrida").toInt() == 0, "linea incompleta: todavia no se procesa");
        c.procesarSalida(inicio.mid(40));
        comprobar(c.corrida().value("corrida").toInt() == 3 && c.corrida().value("modelo_interno").toBool()
                      && c.corrida().value("dir_corrida").toString().endsWith("corrida_003"),
                  "evento 'inicio' reconstruido a partir de dos fragmentos");

        c.procesarSalida("esto no es json\n" + evento(R"({"v":2,"evento":"fase","fase":"x"})"));
        comprobar(c.fase().isEmpty(), "lineas invalidas y de otra version del protocolo: ignoradas");
        comprobar(c.registro().size() >= 3, "las lineas ignoradas quedan anotadas en el registro");

        c.procesarSalida(evento(R"({"v":1,"evento":"fase","t":"x","fase":"entrenando"})")
                         + evento(R"({"v":1,"evento":"t_start","t":"x","t_start":"2026-10-01T10:00:01.000Z"})")
                         + evento(R"({"v":1,"evento":"progreso","t":"x","momento":"inicio_paso","paso":0,"total":5,"unidad":"paso","t_callback":"x"})"));
        comprobar(c.fase() == "entrenando" && c.textoFase(c.fase()) == "Entrenando", "evento 'fase'");
        comprobar(c.corrida().value("t_start").toString() == "2026-10-01T10:00:01.000Z", "evento 't_start'");
        comprobar(c.pasoActual() == 1 && c.pasosCompletados() == 0 && c.totalPasos() == 5 && c.unidadPaso() == "paso",
                  "inicio_paso: paso en curso 1 de 5, ninguno completado");

        c.procesarSalida(evento(R"({"v":1,"evento":"progreso","t":"x","momento":"fin_paso","paso":0,"total":5,"unidad":"paso","t_callback":"x","metricas":{"train_loss":0.75}})"));
        comprobar(c.pasosCompletados() == 1 && c.metricasPaso().value("train_loss").toDouble() == 0.75,
                  "fin_paso: 1 paso completado y su train_loss");

        // Inferencia (D-014): fases, progreso por grupo (sin tocar el del entrenamiento) y fin
        c.procesarSalida(evento(R"({"v":1,"evento":"fase","t":"x","fase":"inferencia"})")
                         + evento(R"({"v":1,"evento":"progreso","t":"x","tarea":"inferencia","momento":"fin_paso","paso":2,"total":0,"unidad":"grupo","t_callback":"x","metricas":{"pasadas":400},"pasadas":1200,"predicciones":360000})"));
        comprobar(c.textoFase("fase_media") == QString::fromUtf8("Línea base intermedia") && c.textoFase(c.fase()) == "Inferencia"
                      && c.textoFase("cancelando_inferencia") == "Cancelando la inferencia",
                  "fases de la inferencia con su texto");
        comprobar(c.progresoInferencia().value("grupos").toInt() == 3 && c.progresoInferencia().value("pasadas").toLongLong() == 1200
                      && c.progresoInferencia().value("predicciones").toLongLong() == 360000 && c.pasosCompletados() == 1
                      && c.totalPasos() == 5,
                  "progreso de la inferencia: grupo 3, pasadas y predicciones acumuladas; el del entrenamiento no cambia");
        c.procesarSalida(evento(R"({"v":1,"evento":"inferencia_terminada","t":"x","status_dll":0,"status_texto":"ok","t_inf_antes":"a","t_inf_despues":"b","duracion_ms":3000,"grupos_registrados":3,"pasadas":1200,"n_predicciones":360000,"tiempo_por_prediccion_us":8.33,"motivo_sin_por_prediccion":null})"));
        comprobar(c.corrida().value("inferencia").toMap().value("n_predicciones").toLongLong() == 360000
                      && c.corrida().value("inferencia").toMap().value("tiempo_por_prediccion_us").toDouble() == 8.33,
                  "evento 'inferencia_terminada' en la corrida");

        // Auditoria (D-012): el veredicto y las comprobaciones quedan en la corrida
        c.procesarSalida(evento(R"({"v":1,"evento":"auditoria","t":"x","veredicto":"con_advertencias","version_reglas":1,"archivo":"auditoria.json","comprobaciones":[{"id":"actividad_ajena","resultado":"advertencia","motivo":"10 de 66 minutos"}]})"));
        const QVariantMap aud = c.corrida().value("auditoria").toMap();
        comprobar(aud.value("veredicto").toString() == "con_advertencias" && aud.value("version_reglas").toInt() == 1
                      && aud.value("comprobaciones").toList().size() == 1
                      && c.registro().first().toMap().value("mensaje").toString() == QString::fromUtf8("Auditoría de la corrida: con advertencias"),
                  "evento 'auditoria': veredicto y comprobaciones en la corrida y en el registro");

        c.procesarSalida(evento(R"({"v":1,"evento":"evaluacion","t":"x","evaluacion":{"ejecutada":true,"status":0,"metricas":{"val_accuracy":0.8}}})")
                         + evento(R"({"v":1,"evento":"fin","t":"2026-10-01T10:00:09.000Z","estado":"completada","dir_corrida":"C:\\salida\\corridas\\prueba\\corrida_003","error":null,"modelo_liberado":"liberado","dll_descargada":true})"));
        comprobar(c.estado() == "completada" && c.fase().isEmpty(), "evento 'fin' completada: estado completada");
        comprobar(c.corrida().value("evaluacion").toMap().value("metricas").toMap().value("val_accuracy").toDouble() == 0.8,
                  "evaluacion conservada en la corrida");
        comprobar(c.corrida().value("t_fin").toString() == "2026-10-01T10:00:09.000Z", "t_fin tomado del evento 'fin'");
        comprobar(notifExito == 1 && notifError == 0, "una notificacion de exito");
        comprobar(!c.activa(), "sin proceso: la corrida no esta activa");

        c.descartarResultado();
        comprobar(c.estado() == "inactivo" && c.corrida().isEmpty() && c.registro().isEmpty() && c.totalPasos() == 0,
                  "descartarResultado() vuelve al estado inicial");

        c.procesarSalida(evento(R"({"v":1,"evento":"fin","t":"x","estado":"error","dir_corrida":null,"error":{"tipo":"configuracion","mensaje":"hiperparametro fuera de rango"}})"));
        comprobar(c.estado() == "error"
                      && c.corrida().value("error").toMap().value("mensaje").toString() == "hiperparametro fuera de rango"
                      && notifError == 1,
                  "evento 'fin' con error: estado error, mensaje y notificacion");

        c.descartarResultado();
        c.procesarSalida(evento(R"({"v":1,"evento":"cancelacion_solicitada","t":"x"})")
                         + evento(R"({"v":1,"evento":"fin","t":"x","estado":"cancelada","dir_corrida":"C:\\c","error":null})"));
        comprobar(c.estado() == "cancelada", "evento 'fin' cancelada");

        c.descartarResultado();
        c.procesarSalida(evento(R"({"v":1,"evento":"evento_futuro","t":"x"})"));
        comprobar(c.estado() == "inactivo" && c.registro().isEmpty(), "eventos desconocidos: ignorados sin error");
    }

    // ---------------------------------------------------------------- 3. encabezado CSV
    {
        QTemporaryDir tmp;
        const QString ruta = tmp.filePath("datos.csv");
        QFile f(ruta);
        comprobar(f.open(QIODevice::WriteOnly), "se crea un CSV temporal");
        f.write("x1;x2;especie\n1,5;2;0\n");
        f.close();
        ClienteOrquestador c(opciones(true));
        const QVariantMap r = c.encabezadoCsv(ruta);
        comprobar(r.value("ok").toBool() && r.value("columnas").toStringList() == QStringList({"x1", "x2", "especie"})
                      && r.value("separador").toString() == ";",
                  "encabezado del CSV: 3 columnas con separador ';'");
        const QVariantMap r2 = c.encabezadoCsv(tmp.filePath("no_existe.csv"));
        comprobar(!r2.value("ok").toBool() && !r2.value("error").toString().isEmpty(), "CSV inexistente: error");
    }

    // ---------------------------------------------------------------- 4. metricas del monitor de software
    {
        QTemporaryDir tmp;
        QFile info(tmp.filePath("sistema_info.json"));
        comprobar(info.open(QIODevice::WriteOnly), "se crea sistema_info.json de prueba");
        info.write(R"({"ram_total_bytes": 8589934592})");
        info.close();
        QFile csv(tmp.filePath("sistema_metrics.csv"));
        comprobar(csv.open(QIODevice::WriteOnly), "se crea sistema_metrics.csv de prueba");
        csv.write("timestamp,timestamp_lectura,intervalo_ms,cpu_sis_ocupado_s,cpu_sis_total_s,ram_sis_usada_bytes,"
                  "rendimiento_cpu_pct,orq_cpu_s,orq_ws_bytes,orq_privado_bytes,med_cpu_s,med_ws_bytes,med_privado_bytes\n"
                  "2026-10-01T10:00:01.000Z,2026-10-01T10:00:01.001Z,1000.000,2.0000,8.0000,4294967296,,1.0000,104857600,1,0.0000,1,1\n"
                  "2026-10-01T10:00:02.000Z,2026-10-01T10:00:02.000Z,1000.0");  // fila a medio escribir
        csv.flush();

        LectorMetricasSistema lector;
        lector.iniciar(tmp.path());
        QVariantList filas = lector.actualizar();
        comprobar(filas.size() == 1, QString("solo se leen filas completas (%1)").arg(filas.size()));
        const QVariantMap f = filas.value(0).toMap();
        comprobar(qAbs(f.value("cpu_sistema_pct").toDouble() - 25.0) < 1e-9, "CPU del sistema: 2 s de 8 s = 25 %");
        comprobar(qAbs(f.value("cpu_proceso_pct").toDouble() - 12.5) < 1e-9, "CPU del proceso: 1 s de 8 s = 12,5 % del equipo");
        comprobar(qAbs(f.value("cpu_proceso_nucleos").toDouble() - 1.0) < 1e-9, "CPU del proceso: 1 nucleo en promedio");
        comprobar(qAbs(f.value("ram_sistema_pct").toDouble() - 50.0) < 1e-9, "RAM del sistema: 4 de 8 GiB = 50 %");
        comprobar(qAbs(f.value("ram_proceso_mb").toDouble() - 100.0) < 1e-9, "RAM del proceso: 100 MiB");

        csv.write("00,4.0000,8.0000,4294967296,,,,,0.0000,1,1\n");  // se completa la fila; proceso terminado
        csv.close();
        filas = lector.actualizar();
        const QVariantMap g = filas.value(0).toMap();
        comprobar(filas.size() == 1 && g.value("timestamp").toString() == "2026-10-01T10:00:02.000Z",
                  "la fila se lee al completarse, sin repetir la anterior");
        comprobar(qAbs(g.value("cpu_sistema_pct").toDouble() - 50.0) < 1e-9 && !g.contains("cpu_proceso_pct")
                      && !g.contains("ram_proceso_mb"),
                  "columnas del proceso vacias: esos valores quedan ausentes");
        comprobar(lector.actualizar().isEmpty(), "sin filas nuevas: lista vacia");
    }

    // ---------------------------------------------------------------- 5. monitor fisico de energia (F6)
    {
        QTemporaryDir tmp;
        QFile csv(tmp.filePath("energia_atorch.csv"));
        comprobar(csv.open(QIODevice::WriteOnly), "se crea energia_atorch.csv de prueba");
        csv.write("timestamp,timestamp_trama,conectado,voltage_v,current_a,power_w,energy_kwh,raw_hex\n"
                  "2026-10-01T10:00:00.000Z,,0,,,,,\n"
                  "2026-10-01T10:00:01.000Z,2026-10-01T10:00:00.812Z,1,220.4,0.187,41.2,12.34,ff55aa\n");
        csv.close();
        LectorMetricasEnergia lector;
        lector.iniciar(tmp.path());
        const QVariantList filas = lector.actualizar();
        comprobar(filas.size() == 2, QString("2 filas de la rejilla (%1)").arg(filas.size()));
        const QVariantMap sin = filas.value(0).toMap();
        const QVariantMap con = filas.value(1).toMap();
        comprobar(!sin.value("conectado").toBool() && !sin.contains("potencia_w"), "fila sin conexion: sin valores");
        comprobar(con.value("conectado").toBool() && qAbs(con.value("potencia_w").toDouble() - 41.2) < 1e-9
                      && qAbs(con.value("voltaje_v").toDouble() - 220.4) < 1e-9 && qAbs(con.value("corriente_a").toDouble() - 0.187) < 1e-9,
                  "fila con trama: potencia, voltaje y corriente");
        comprobar(!con.contains("energy_kwh") && !con.contains("energia"),
                  "el contador del medidor no se expone como energia (D-008)");

        ClienteOrquestador::Opciones o = opciones(true);
        o.laboratorio["atorch_mac"] = "00:00:00:19:EC:2C";
        o.laboratorio["python_medidor_energia"] = "C:/lab/venv/Scripts/python.exe";
        ClienteOrquestador c(o);
        QVariantMap s = seleccionValida();
        QVariantMap med;
        med["sistema"] = true;
        med["energia"] = true;
        s["medidores"] = med;
        QString error;
        const QJsonObject cfg = c.construirConfig(s, &error);
        comprobar(cfg["medidores"].toObject()["energia"].toBool()
                      && cfg["energia_ble"].toObject()["mac"].toString() == "00:00:00:19:EC:2C"
                      && cfg["energia_ble"].toObject()["python"].toString() == QDir::toNativeSeparators("C:/lab/venv/Scripts/python.exe")
                      && !cfg["energia_ble"].toObject().contains("script"),
                  "config.json: energia_ble con la MAC y el Python del laboratorio (sin script: valor por defecto)");
        med["energia"] = false;
        s["medidores"] = med;
        comprobar(!c.construirConfig(s, &error).contains("energia_ble"), "sin monitor fisico no se envia energia_ble");
        const QVariantMap lab = c.medidorEnergiaLaboratorio();
        comprobar(lab.value("mac").toString() == "00:00:00:19:EC:2C" && !lab.value("python_existe").toBool()
                      && lab.value("script").toString().endsWith("energia_ble.py"),
                  "medidorEnergiaLaboratorio: MAC, Python inexistente y script por defecto");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "CLIENTE OK" : "CLIENTE CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
