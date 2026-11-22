// test_historial.cpp - Historial de corridas y resumen de mediciones (F9).
//
// Arma dos corridas de prueba con archivos escritos a mano (DATOS INVENTADOS, con el formato de
// metadata.json, sistema_metrics.csv, energia_atorch.csv y resultados.json) y comprueba el
// listado, las estadisticas por ventana, las series y la exportacion de la rejilla.

#include "Historial.h"
#include "ResumenCorrida.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <cstdio>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

bool cerca(double a, double b) { return std::fabs(a - b) < 1e-6; }

void escribir(const QString& ruta, const QByteArray& contenido)
{
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(contenido);
}

QString ts(int segundo) { return QString("2026-10-01T10:00:%1.000Z").arg(segundo, 2, 10, QChar('0')); }

// Corrida: t_start 10:00:00, entrenamiento 10:00:02 - 10:00:06, t_stop 10:00:08
QString crearCorrida(const QDir& raiz, const QString& experimento, int numero, const QString& tCreacion, bool conMedidores)
{
    const QString dir = raiz.filePath(QString("corridas/%1/corrida_%2").arg(experimento).arg(numero, 3, 10, QChar('0')));
    QDir().mkpath(dir);
    escribir(dir + "/metadata.json", QString(R"json({
      "version_metadata": 1, "experimento": "%1", "corrida": %2, "id_corrida": "%1/corrida_%3",
      "estado": "completada", "error": null,
      "modelo": {"clave": "prueba", "nombre": "Prueba (simulado)", "interno": true, "unidad_paso": "paso"},
      "hiperparametros": {"pasos": 4}, "datos": {"origen": "sintetico", "n_train": 1200, "n_val": 300},
      "entrenamiento": {"pasos_registrados": 4}, "evaluacion": {"metricas": {"val_accuracy": 0.8}},
      "tiempos": {"t_creacion": "%4", "t_start": "%5", "t_antes": "%6", "t_despues": "%7", "t_stop": "%8",
                  "t_fin": "%8", "duracion_entrenamiento_ms": 4000},
      "medidores": %9
    })json").arg(experimento).arg(numero).arg(numero, 3, 10, QChar('0')).arg(tCreacion, ts(0), ts(2), ts(6), ts(8),
                                            conMedidores ? R"([{"nombre":"sistema"},{"nombre":"energia"}])" : "[]").toUtf8());
    if (!conMedidores) return dir;

    escribir(dir + "/sistema_info.json", R"({"ram_total_bytes": 8000000000})");
    // 8 filas: CPU del proceso 1 s de 8 s (12,5 %) dentro del entrenamiento, 0 fuera
    QByteArray s = "timestamp,timestamp_lectura,intervalo_ms,cpu_sis_ocupado_s,cpu_sis_total_s,ram_sis_usada_bytes,"
                   "rendimiento_cpu_pct,orq_cpu_s,orq_ws_bytes,orq_privado_bytes,med_cpu_s,med_ws_bytes,med_privado_bytes\n";
    for (int i = 1; i <= 8; ++i) {
        const bool entrena = i >= 3 && i <= 6;  // intervalos (2,3] .. (5,6]
        s += QString("%1,%1,1000.0,%2,8.0000,4000000000,,%3,%4,1,0.0000,1,1\n")
                 .arg(ts(i)).arg(entrena ? "2.0000" : "0.8000").arg(entrena ? "1.0000" : "0.0000")
                 .arg(entrena ? 209715200 : 104857600).toUtf8();
    }
    escribir(dir + "/sistema_metrics.csv", s);
    // Rejilla de energia: 40 W durante el entrenamiento, 20 W fuera; segundo 7 sin trama
    QByteArray e = "timestamp,timestamp_trama,conectado,voltage_v,current_a,power_w,energy_kwh,raw_hex\n";
    for (int i = 0; i <= 8; ++i) {
        if (i == 7) { e += QString("%1,,0,,,,,\n").arg(ts(i)).toUtf8(); continue; }
        const bool entrena = i >= 2 && i <= 6;
        e += QString("%1,%1,1,220.0,%2,%3,1.00,ab%4\n").arg(ts(i)).arg(entrena ? "0.182" : "0.091")
                 .arg(entrena ? "40.0" : "20.0").arg(i).toUtf8();
    }
    escribir(dir + "/energia_atorch.csv", e);
    escribir(dir + "/resultados.json", R"({"version_resultados": 1, "energia_experimento": {"metodo": "trapecio_tramas_unicas",
        "version_metodo": 1, "ventanas": {"entrenamiento": {"energia_j": 160.0, "energia_wh": 0.0444444, "cobertura": 1.0,
        "duracion_s": 4.0, "huecos": []}}}})");
    return dir;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QDir raiz(tmp.path());
    const QString dirA = crearCorrida(raiz, "exp_a", 1, "2026-10-01T09:59:50.000Z", true);
    crearCorrida(raiz, "exp_b", 1, "2026-10-01T11:00:00.000Z", false);  // mas reciente, sin medidores
    QDir().mkpath(raiz.filePath("corridas/exp_b/corrida_002"));          // carpeta sin metadata: se ignora

    // 1. Listado
    Historial h(tmp.path());
    h.recargar();
    const QVariantList lista = h.corridas();
    comprobar(lista.size() == 2, QString("2 corridas con metadata.json (%1)").arg(lista.size()));
    comprobar(lista.value(0).toMap().value("id").toString() == "exp_b/corrida_001", "la mas reciente primero");
    const QVariantMap a = lista.value(1).toMap();
    comprobar(a.value("modelo_nombre").toString() == "Prueba (simulado)" && a.value("estado").toString() == "completada"
                  && cerca(a.value("duracion_entrenamiento_s").toDouble(), 4.0),
              "resumen: modelo, estado y duracion del entrenamiento");
    comprobar(a.value("medidores").toStringList() == QStringList({"sistema", "energia"}), "resumen: medidores de la corrida");
    comprobar(cerca(a.value("energia_experimento").toMap().value("entrenamiento").toMap().value("energia_wh").toDouble(), 0.0444444),
              "resumen: energia del experimento de resultados.json");
    comprobar(!lista.value(0).toMap().contains("energia_experimento"), "sin resultados.json no se inventa energia");

    // 2. Estadisticas por ventana
    const QVariantMap d = h.detalle(dirA);
    const QVariantMap ent = d.value("ventanas").toMap().value("entrenamiento").toMap();
    const QVariantMap sisEnt = ent.value("sistema").toMap();
    const QVariantMap cpuProc = sisEnt.value("cpu_proceso_pct").toMap();
    comprobar(cpuProc.value("n").toInt() == 4 && cerca(cpuProc.value("media").toDouble(), 12.5),
              QString("entrenamiento: 4 intervalos completos con CPU del proceso 12,5 % (n=%1, media=%2)")
                  .arg(cpuProc.value("n").toInt()).arg(cpuProc.value("media").toDouble()));
    comprobar(cerca(sisEnt.value("cpu_sistema_pct").toMap().value("media").toDouble(), 25.0)
                  && cerca(sisEnt.value("ram_sistema_pct").toMap().value("media").toDouble(), 50.0)
                  && cerca(sisEnt.value("ram_proceso_mb").toMap().value("max").toDouble(), 200.0),
              "entrenamiento: CPU del sistema 25 %, RAM del sistema 50 %, RAM del proceso 200 MB");
    const QVariantMap eneEnt = ent.value("energia").toMap();
    comprobar(eneEnt.value("filas").toInt() == 5 && eneEnt.value("filas_con_trama").toInt() == 5
                  && cerca(eneEnt.value("potencia_w").toMap().value("media").toDouble(), 40.0),
              "entrenamiento: 5 filas de energia a 40 W");
    const QVariantMap med = d.value("ventanas").toMap().value("medicion").toMap();
    const QVariantMap eneMed = med.value("energia").toMap();
    comprobar(eneMed.value("filas").toInt() == 9 && eneMed.value("filas_con_trama").toInt() == 8
                  && cerca(eneMed.value("potencia_w").toMap().value("min").toDouble(), 20.0)
                  && cerca(eneMed.value("potencia_w").toMap().value("max").toDouble(), 40.0),
              "medicion: 9 filas, 8 con trama (una sin conexion), potencia de 20 a 40 W");
    comprobar(med.value("sistema").toMap().value("cpu_proceso_pct").toMap().value("n").toInt() == 8,
              "medicion: los 8 intervalos del monitor de software");
    // Metricas del modelo (D-015): solo en el detalle; sin el archivo, vacias ("no disponible")
    comprobar(d.value("metricas_modelo").toMap().isEmpty() && !a.contains("metricas_modelo"),
              "sin metricas_modelo.json: detalle con metricas_modelo vacio; la lista no las lee");
    escribir(dirA + "/metricas_modelo.json", R"({"version_formato": 1, "clases_originales": [0, 1],
        "validacion": {"status_dll": 0, "exactitud": 0.75, "matriz_confusion": [[2, 1], [0, 1]]}})");
    const QVariantMap mm = h.detalle(dirA).value("metricas_modelo").toMap();
    comprobar(mm.value("version_formato").toInt() == 1 && cerca(mm.value("validacion").toMap().value("exactitud").toDouble(), 0.75)
                  && mm.value("validacion").toMap().value("matriz_confusion").toList().size() == 2,
              "con metricas_modelo.json: el detalle trae el archivo completo");
    QFile::remove(dirA + "/metricas_modelo.json");

    // 3. Series
    const QVariantMap ss = d.value("series_sistema").toMap();
    comprobar(ss.value("t_s").toList().size() == 8 && cerca(ss.value("t_s").toList().value(0).toDouble(), 1.0),
              "serie de sistema: 8 puntos desde t_start + 1 s");
    const QVariantList pot = d.value("series_energia").toMap().value("potencia_w").toList();
    comprobar(pot.size() == 9 && !pot.value(7).isValid(), "serie de energia: el segundo sin trama queda vacio, no se interpola");
    comprobar(cerca(d.value("t_antes_rel_s").toDouble(), 2.0) && cerca(d.value("t_despues_rel_s").toDouble(), 6.0),
              "t_antes y t_despues relativos a t_start");

    // 4. Exportacion de la rejilla
    const QString destino = tmp.filePath("exportado.csv");
    const QString error = h.exportarCsv(dirA, destino);
    comprobar(error.isEmpty(), "exportacion sin error " + error);
    QFile f(destino);
    QStringList lineas;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) lineas << in.readLine();
    }
    comprobar(lineas.size() == 10, QString("encabezado + 9 puntos de la rejilla unidos por timestamp (%1 lineas)").arg(lineas.size()));
    // Columnas fase, paso_desde y paso_hasta despues de en_entrenamiento (D-011). Esta corrida
    // no tiene progreso.csv: los pasos van vacios. Sin la marca fase_posterior_iniciada ni
    // fase_posterior_ms (corrida anterior a la linea base), lo que sigue a t_despues no tiene fase.
    comprobar(lineas.value(0).startsWith("timestamp,t_rel_s,en_entrenamiento,fase,paso_desde,paso_hasta,cpu_proceso_pct"),
              "encabezado de la exportacion");
    comprobar(lineas.value(1).startsWith(ts(0) + ",0.000,0,,,,,,,,20.0"),
              "t_start: solo energia (el monitor de software empieza en t_start + 1 s); su intervalo es anterior a t_start: sin fase");
    comprobar(lineas.value(2).startsWith(ts(1) + ",1.000,0,base_pre,,,"), "segundo 1: linea base previa");
    comprobar(lineas.value(4).startsWith(ts(3) + ",3.000,1,entrenamiento,,,12.500,25.000,200.000,50.000,40.0,220.0,0.182"),
              "segundo 3: ambos medidores en la misma fila y dentro del entrenamiento");
    comprobar(lineas.value(8).startsWith(ts(7) + ",7.000,0,,,,0.000,10.000,100.000,50.000,,,,"),
              "segundo 7: sin trama de energia, columnas vacias; sin fase posterior, sin fase");
    comprobar(!h.exportarCsv(raiz.filePath("corridas/exp_b/corrida_001"), tmp.filePath("x.csv")).isEmpty(),
              "corrida sin medidores: la exportacion informa que no hay datos");

    // 4b. Pasos de la serie con inferencia (D-014): las filas posteriores a t_inf_antes usan los
    // grupos de progreso_inferencia.csv; las demas, los pasos de progreso.csv
    {
        auto conInferencia = [&](const QString& nombre, const QByteArray& progreso, const QByteArray& grupos) {
            const QString dir = raiz.filePath(nombre);
            QDir().mkpath(dir);
            QByteArray s = "timestamp,timestamp_lectura,intervalo_ms,cpu_sis_ocupado_s,cpu_sis_total_s,ram_sis_usada_bytes,"
                           "rendimiento_cpu_pct,orq_cpu_s,orq_ws_bytes,orq_privado_bytes,med_cpu_s,med_ws_bytes,med_privado_bytes\n";
            for (int i = 1; i <= 9; ++i) s += QString("%1,%1,1000.0,1.0,8.0,4000000000,,0.5,1,1,0.0,1,1\n").arg(ts(i)).toUtf8();
            escribir(dir + "/sistema_metrics.csv", s);
            const QByteArray enc = "timestamp_inicio,timestamp_fin,step_index,step_total,metrica_1_nombre,metrica_1_valor,"
                                   "metrica_2_nombre,metrica_2_valor,metrica_3_nombre,metrica_3_valor,metrica_4_nombre,metrica_4_valor\n";
            escribir(dir + "/progreso.csv", enc + progreso);
            escribir(dir + "/progreso_inferencia.csv", enc + grupos);
            return dir;
        };
        auto marca = [](const char* texto) { return QDateTime::fromString(texto, Qt::ISODateWithMs).toMSecsSinceEpoch(); };
        auto pasos = [](const QList<analisis::FilaRejilla>& filas) {
            QStringList r;
            for (const auto& f : filas)
                r << (f.fase.isEmpty() ? "-" : f.fase) + ":" + (f.pasoDesde < 0 ? "" : QString("%1-%2").arg(f.pasoDesde).arg(f.pasoHasta));
            return r.join(' ');
        };
        // t_antes 1, t_despues 3, t_inf_antes 4 (base_media 1 s), t_inf_despues 7, t_stop 9
        const QString dirI = conInferencia("con_inferencia",
            QString("%1,%2,0,2,train_loss,1,,,,,,\n%2,%3,1,2,train_loss,0.5,,,,,,\n").arg(ts(1), ts(2), ts(3)).toUtf8(),
            QString("%1,%2,0,0,pasadas,10,predicciones,3000,,,,\n%2,%3,1,0,pasadas,10,predicciones,3000,,,,\n"
                    "%3,%4,2,0,pasadas,10,predicciones,3000,,,,\n").arg(ts(4), ts(5), ts(6), ts(7)).toUtf8());
        fases::Marcas mi;
        mi.tStartMs = marca("2026-10-01T10:00:00.000Z"); mi.tAntesMs = marca("2026-10-01T10:00:01.000Z");
        mi.tDespuesMs = marca("2026-10-01T10:00:03.000Z"); mi.tInfAntesMs = marca("2026-10-01T10:00:04.000Z");
        mi.tInfDespuesMs = marca("2026-10-01T10:00:07.000Z"); mi.tStopMs = marca("2026-10-01T10:00:09.000Z");
        mi.mediaIniciada = mi.inferenciaIniciada = mi.posteriorIniciada = true;
        QList<analisis::FilaRejilla> filas;
        QString err;
        comprobar(analisis::rejilla(dirI, mi, &filas, &err), "rejilla de una corrida con inferencia " + err);
        comprobar(pasos(filas) == "base_pre:0-0 entrenamiento:0-1 entrenamiento:1-1 base_media: inferencia:0-1 inferencia:1-2 "
                                  "inferencia:2-2 base_post: base_post:",
                  "fases y pasos: entrenamiento con progreso.csv, inferencia con los grupos (" + pasos(filas) + ")");
        // base_media de menos de 1 s: la fila que cruza t_despues (3,2) y t_inf_antes (3,6) muestra
        // solo los grupos de la inferencia; el ultimo paso del entrenamiento queda en progreso.csv
        const QString dirC = conInferencia("media_corta",
            QString("%1,%2,0,2,train_loss,1,,,,,,\n%2,2026-10-01T10:00:03.200Z,1,2,train_loss,0.5,,,,,,\n").arg(ts(1), ts(2)).toUtf8(),
            QString("2026-10-01T10:00:03.600Z,2026-10-01T10:00:04.600Z,0,0,pasadas,10,predicciones,3000,,,,\n").toUtf8());
        fases::Marcas mc = mi;
        mc.tDespuesMs = marca("2026-10-01T10:00:03.200Z"); mc.tInfAntesMs = marca("2026-10-01T10:00:03.600Z");
        mc.tInfDespuesMs = marca("2026-10-01T10:00:04.600Z"); mc.tStopMs = marca("2026-10-01T10:00:06.000Z");
        // Detalle local (Resultados): metadata.inferencia y estadisticas de la ventana de inferencia
        escribir(dirI + "/metadata.json", QString(R"({"estado": "completada", "modelo": {}, "medidores": [{"nombre": "sistema"}],
            "inferencia": {"activada": true, "lote": 65536, "duracion_min_s": 3, "n_predicciones": 9000,
                           "tiempo_por_prediccion_us": 333.3, "motivo_sin_por_prediccion": null},
            "tiempos": {"t_start": "%1", "t_antes": "%2", "t_despues": "%3", "t_inf_antes": "%4", "t_inf_despues": "%5",
                        "t_stop": "%6"}})").arg(ts(0), ts(1), ts(3), ts(4), ts(7), ts(9)).toUtf8());
        const QVariantMap di = h.detalle(dirI);
        const QVariantMap cpuInf = di.value("ventanas").toMap().value("inferencia").toMap().value("sistema").toMap()
                                       .value("cpu_proceso_pct").toMap();
        comprobar(di.value("inferencia").toMap().value("n_predicciones").toInt() == 9000 && cpuInf.value("n").toInt() == 3
                      && cerca(di.value("t_inf_antes_rel_s").toDouble(), 4.0) && cerca(di.value("t_inf_despues_rel_s").toDouble(), 7.0),
                  QString("detalle con inferencia: metadata.inferencia, 3 intervalos del monitor en [t_inf_antes, t_inf_despues] (n=%1) "
                          "y marcas relativas").arg(cpuInf.value("n").toInt()));
        comprobar(Historial::resumenDesdeJson(QJsonDocument::fromJson(R"({"modelo": {}})").object(), {}, "x")
                          .value("inferencia").toMap().value("activada") == QVariant(false),
                  "corrida anterior a la inferencia: inferencia.activada = false");
        const bool okC = analisis::rejilla(dirC, mc, &filas, &err);
        comprobar(okC && filas.size() >= 4 && filas[3].fase == "transicion"
                      && filas[3].pasoDesde == 0 && filas[3].pasoHasta == 0 && filas[2].pasoHasta == 1,
                  "base_media < 1 s: la fila de transicion usa solo los grupos de la inferencia (" + pasos(filas) + ")");
    }

    // 5. Hilos que uso la corrida (D2): metadata.modelo.hilos_efectivos; en corridas anteriores,
    // el hiperparametro 'hilos' si es > 0 y si no los automaticos (modelo.hilos)
    auto hilos = [](const char* json) {
        return Historial::resumenDesdeJson(QJsonDocument::fromJson(json).object(), {}, "x").value("hilos_efectivos").toInt();
    };
    comprobar(hilos(R"({"modelo": {"hilos": 8, "hilos_efectivos": 2}, "hiperparametros": {"hilos": 2}})") == 2,
              "hilos_efectivos de metadata.json (D2)");
    comprobar(hilos(R"({"modelo": {"hilos": 8}, "hiperparametros": {"hilos": 4}})") == 4,
              "corrida anterior a hilos_efectivos: el hiperparametro hilos > 0");
    comprobar(hilos(R"({"modelo": {"hilos": 8}, "hiperparametros": {"hilos": 0}})") == 8,
              "corrida anterior con hilos = 0: los automaticos de la DLL");

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "HISTORIAL OK" : "HISTORIAL CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
