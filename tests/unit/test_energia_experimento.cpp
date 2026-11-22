// test_energia_experimento.cpp - Pruebas del calculo de la energia del experimento (D-008, F7).
//
// Todas las tramas son DATOS INVENTADOS para la prueba (no provienen del Atorch). Verifican el
// metodo: tramas repetidas en la rejilla que no se cuentan dos veces, jitter en la llegada,
// huecos mayores que el umbral sin interpolar, bordes de la ventana y el caso de referencia del
// plan: 40 W constantes durante 60 s = 2400 J.

#include "EnergiaExperimento.h"
#include "FasesCorrida.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

using energia::Trama;

namespace {

int g_fallos = 0;
constexpr qint64 T0 = 1790000000000LL;  // instante arbitrario en ms UTC

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

bool cerca(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

Trama trama(qint64 ms, double w, double kwh = -1)
{
    Trama t;
    t.tMs = T0 + ms;
    t.potenciaW = w;
    t.contadorKwh = kwh;
    t.rawHex = QString("ff55%1").arg(ms);
    return t;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    // 1. Caso de referencia: 40 W durante 60 s, una trama por segundo
    {
        QList<Trama> t;
        for (int s = 0; s <= 60; ++s) t << trama(s * 1000, 40.0);
        const auto r = energia::integrar(energia::tramasUnicas(t), T0, T0 + 60000);
        comprobar(cerca(r.energiaJ, 2400.0), QString("40 W x 60 s = 2400 J (%1 J)").arg(r.energiaJ, 0, 'f', 6));
        comprobar(cerca(r.cobertura, 1.0) && r.huecos.isEmpty(), "cobertura completa, sin huecos");
        comprobar(r.tramasUsadas == 61, QString("61 tramas en la ventana (%1)").arg(r.tramasUsadas));
    }

    // 2. Tramas repetidas en la rejilla (misma trama en varias filas) no se cuentan dos veces
    {
        QList<Trama> t;
        for (int s = 0; s <= 10; ++s) { t << trama(s * 1000, 40.0); t << trama(s * 1000, 40.0); }
        int repetidas = 0;
        const auto unicas = energia::tramasUnicas(t, &repetidas);
        comprobar(unicas.size() == 11 && repetidas == 11, QString("11 repetidas eliminadas (%1)").arg(repetidas));
        const auto r = energia::integrar(unicas, T0, T0 + 10000);
        comprobar(cerca(r.energiaJ, 400.0), QString("con repeticiones: 400 J, no el doble (%1 J)").arg(r.energiaJ));
    }

    // 3. Jitter: llegadas irregulares (0,8 a 1,2 s) con potencia constante
    {
        QList<Trama> t;
        qint64 ms = 0;
        const int pasos[] = {800, 1200, 950, 1050, 1100, 900, 1000, 1000, 1150, 850};
        t << trama(0, 25.0);
        for (int p : pasos) { ms += p; t << trama(ms, 25.0); }
        const auto r = energia::integrar(energia::tramasUnicas(t), T0, T0 + ms);
        comprobar(cerca(r.energiaJ, 25.0 * ms / 1000.0), QString("jitter: 25 W x %1 s exactos (%2 J)").arg(ms / 1000.0).arg(r.energiaJ));
    }

    // 4. Rampa lineal: el trapecio es exacto
    {
        QList<Trama> t;
        for (int s = 0; s <= 10; ++s) t << trama(s * 1000, 10.0 + 2.0 * s);  // 10 W -> 30 W
        const auto r = energia::integrar(energia::tramasUnicas(t), T0, T0 + 10000);
        comprobar(cerca(r.energiaJ, 200.0), QString("rampa 10->30 W en 10 s = 200 J (%1 J)").arg(r.energiaJ));
    }

    // 5. Hueco mayor que el umbral (3 s): no se integra ni se interpola
    {
        QList<Trama> t;
        for (int s = 0; s <= 10; ++s) t << trama(s * 1000, 40.0);
        for (int s = 15; s <= 20; ++s) t << trama(s * 1000, 40.0);  // falta de 10 s a 15 s
        const auto r = energia::integrar(energia::tramasUnicas(t), T0, T0 + 20000);
        comprobar(cerca(r.energiaJ, 40.0 * 15), QString("hueco de 5 s excluido: 600 J (%1 J)").arg(r.energiaJ));
        comprobar(cerca(r.cobertura, 0.75), QString("cobertura 15 de 20 s = 0,75 (%1)").arg(r.cobertura));
        comprobar(r.huecos.size() == 1 && r.huecos[0].tipo == "entre_tramas"
                      && r.huecos[0].inicioMs == T0 + 10000 && r.huecos[0].finMs == T0 + 15000,
                  "el hueco queda registrado con su inicio y fin");
        // Un salto de 3 s exactos todavia se integra (umbral inclusivo)
        QList<Trama> u{trama(0, 40.0), trama(3000, 40.0)};
        comprobar(cerca(energia::integrar(u, T0, T0 + 3000).energiaJ, 120.0), "salto de 3 s exactos: se integra");
    }

    // 6. Bordes de la ventana: recorte dentro de un segmento con interpolacion lineal
    {
        QList<Trama> t;
        for (int s = 0; s <= 10; ++s) t << trama(s * 1000, 10.0 * s);  // P(t) = 10 t
        const auto r = energia::integrar(energia::tramasUnicas(t), T0 + 2500, T0 + 7500);
        // integral de 10 t entre 2,5 y 7,5 s = 5 * (7,5^2 - 2,5^2) = 250 J
        comprobar(cerca(r.energiaJ, 250.0), QString("ventana a mitad de segmento: 250 J (%1 J)").arg(r.energiaJ));
        comprobar(cerca(r.cobertura, 1.0), "los bordes quedan cubiertos por los segmentos que los atraviesan");

        const auto r2 = energia::integrar(energia::tramasUnicas(t), T0 - 2000, T0 + 4000);
        comprobar(cerca(r2.energiaJ, 80.0) && cerca(r2.cobertura, 4.0 / 6.0)
                      && r2.huecos.size() == 1 && r2.huecos[0].tipo == "borde_inicial",
                  QString("antes de la primera trama: borde inicial sin datos (%1 J, cobertura %2)").arg(r2.energiaJ).arg(r2.cobertura));
        const auto r3 = energia::integrar(energia::tramasUnicas(t), T0 + 8000, T0 + 12000);
        comprobar(r3.huecos.size() == 1 && r3.huecos[0].tipo == "borde_final" && cerca(r3.cobertura, 0.5),
                  "despues de la ultima trama: borde final sin datos");
    }

    // 7. Sin tramas en la ventana
    {
        QList<Trama> t{trama(0, 40.0), trama(1000, 40.0)};
        const auto r = energia::integrar(t, T0 + 60000, T0 + 70000);
        comprobar(r.energiaJ == 0.0 && r.cobertura == 0.0 && r.huecos.size() == 1 && r.huecos[0].tipo == "sin_tramas",
                  "ventana sin tramas: 0 J, cobertura 0 y un hueco 'sin_tramas'");
    }

    // 8. Contador del medidor: solo diagnostico
    {
        QList<Trama> t{trama(0, 40.0, 12.34), trama(1000, 40.0, 12.34), trama(2000, 40.0, 12.35)};
        const QJsonObject c = energia::contadorMedidor(t, T0, T0 + 2000);
        comprobar(cerca(c["inicial_kwh"].toDouble(), 12.34) && cerca(c["final_kwh"].toDouble(), 12.35)
                      && cerca(c["diferencia_kwh"].toDouble(), 0.01, 1e-9),
                  "contador del medidor: inicial, final y diferencia");
        comprobar(c["descripcion"].toString().contains("no es la energia del experimento"), "el contador se declara diagnostico");
    }

    // 9. Lectura del CSV de la rejilla (energia_atorch.csv): filas vacias ignoradas, repetidas eliminadas
    {
        QTemporaryDir tmp;
        QFile f(tmp.filePath("energia_atorch.csv"));
        comprobar(f.open(QIODevice::WriteOnly), "se crea un energia_atorch.csv de prueba");
        f.write("timestamp,timestamp_trama,conectado,voltage_v,current_a,power_w,energy_kwh,raw_hex\n"
                "2026-10-01T10:00:00.000Z,,0,,,,,\n"
                "2026-10-01T10:00:01.000Z,2026-10-01T10:00:00.800Z,1,220.0,0.180,40.0,12.34,ff55aa\n"
                "2026-10-01T10:00:02.000Z,2026-10-01T10:00:01.810Z,1,220.0,0.180,40.0,12.34,ff55bb\n"
                "2026-10-01T10:00:03.000Z,2026-10-01T10:00:01.810Z,1,220.0,0.180,40.0,12.34,ff55bb\n"
                "2026-10-01T10:00:04.000Z,2026-10-01T10:00:03.790Z,1,220.0,0.180,40.0,12.34,ff55cc\n");
        f.close();
        QList<Trama> leidas;
        QString error;
        const bool leido = energia::leerTramasCsv(f.fileName(), &leidas, &error);
        comprobar(leido && leidas.size() == 4, QString("4 filas con trama leidas (%1) %2").arg(leidas.size()).arg(error));
        int rep = 0;
        const auto unicas = energia::tramasUnicas(leidas, &rep);
        comprobar(unicas.size() == 3 && rep == 1, "la trama repetida en dos filas de la rejilla se cuenta una vez");
        const auto r = energia::integrar(unicas, unicas.first().tMs, unicas.last().tMs);
        comprobar(cerca(r.energiaJ, 40.0 * 2.99), QString("40 W entre 0,800 y 3,790 s = 119,6 J (%1 J)").arg(r.energiaJ));
        const QJsonObject j = energia::aJson(r);
        comprobar(j["inicio"].toString() == "2026-10-01T10:00:00.800Z" && cerca(j["energia_wh"].toDouble(), 119.6 / 3600.0),
                  "JSON del resultado con la ventana en ISO UTC y la energia en J y Wh");
    }

    // 10. Resultados de una corrida: ventanas de entrenamiento y medicion, fuente de respaldo
    {
        QTemporaryDir tmp;
        QJsonObject r;
        QString error;
        auto marcas = [](qint64 s, qint64 a, qint64 d, qint64 p, bool post) {
            fases::Marcas m;
            m.tStartMs = s; m.tAntesMs = a; m.tDespuesMs = d; m.tStopMs = p; m.posteriorIniciada = post;
            return m;
        };
        const bool hay = energia::resultadosCorrida(tmp.path(), marcas(T0, T0 + 1000, T0 + 2000, T0 + 3000, true), &r, &error);
        comprobar(!hay && !error.isEmpty(), "sin archivos del medidor: no hay resultados (" + error + ")");

        QFile f(tmp.filePath("energia_atorch.csv"));
        comprobar(f.open(QIODevice::WriteOnly), "se crea un energia_atorch.csv de prueba");
        f.write("timestamp,timestamp_trama,conectado,voltage_v,current_a,power_w,energy_kwh,raw_hex\n");
        for (int s = 0; s <= 20; ++s) {
            const QString ts = QDateTime::fromMSecsSinceEpoch(T0 + s * 1000, QTimeZone::UTC).toString(Qt::ISODateWithMs);
            // Cada trama aparece en dos filas de la rejilla
            for (int k = 0; k < 2; ++k)
                f.write(QString("x,%1,1,220.0,0.182,50.0,1.00,ab%2\n").arg(ts).arg(s).toUtf8());
        }
        f.close();
        comprobar(energia::resultadosCorrida(tmp.path(), marcas(T0, T0 + 5000, T0 + 15000, T0 + 20000, true), &r, &error),
                  "resultados con energia_atorch.csv como respaldo");
        const QJsonObject e = r["energia_experimento"].toObject();
        const QJsonObject v = e["ventanas"].toObject();
        comprobar(e["fuente"].toString() == "energia_atorch.csv" && e["tramas_repetidas_descartadas"].toInt() == 21,
                  "fuente de respaldo y 21 repeticiones de la rejilla descartadas");
        comprobar(cerca(v["entrenamiento"].toObject()["energia_j"].toDouble(), 500.0)
                      && cerca(v["medicion"].toObject()["energia_j"].toDouble(), 1000.0),
                  "50 W: 500 J en 10 s de entrenamiento y 1000 J en 20 s de medicion");
        comprobar(e["metodo"].toString() == "trapecio_tramas_unicas" && e["version_metodo"].toInt() == 1
                      && e["validez"].toString().startsWith("sin evaluar") && r["version_resultados"].toInt() == 3,
                  "metodo, version (resultados v3) y validez sin evaluar registrados (D-008)");
        // Linea base: energia en bruto de cada fase, mismo metodo
        comprobar(cerca(v["base_pre"].toObject()["energia_j"].toDouble(), 250.0)
                      && cerca(v["base_post"].toObject()["energia_j"].toDouble(), 250.0)
                      && cerca(v["base_pre"].toObject()["cobertura"].toDouble(), 1.0)
                      && v["base_pre"].toObject()["inicio"].toString() == v["medicion"].toObject()["inicio"].toString(),
                  "base_pre [t_start, t_antes] y base_post [t_despues, t_stop]: 250 J cada una (50 W x 5 s), en bruto");
        comprobar(e["linea_base"].toString().startsWith("medida") && e["linea_base"].toString().contains("no se aplica")
                      && e["fase_posterior_iniciada"].toObject()["valor"].toBool()
                      && e["fase_posterior_iniciada"].toObject()["origen"].toString() == "metadata",
                  "linea base medida sin correccion; fase_posterior_iniciada con su origen");

        // Cancelada durante el entrenamiento: la fase posterior no empezo, sin base_post
        energia::resultadosCorrida(tmp.path(), marcas(T0, T0 + 5000, T0 + 9000, T0 + 9020, false), &r, &error);
        const QJsonObject v2 = r["energia_experimento"].toObject()["ventanas"].toObject();
        comprobar(v2.contains("base_pre") && !v2.contains("base_post"), "fase posterior sin empezar: base_post no existe");
        // Cancelada antes de entrenar: base_pre parcial hasta t_stop, sin entrenamiento
        energia::resultadosCorrida(tmp.path(), marcas(T0, 0, 0, T0 + 3000, false), &r, &error);
        const QJsonObject v3 = r["energia_experimento"].toObject()["ventanas"].toObject();
        comprobar(cerca(v3["base_pre"].toObject()["energia_j"].toDouble(), 150.0) && !v3.contains("entrenamiento")
                      && !v3.contains("base_post"),
                  "sin entrenamiento: base_pre parcial [t_start, t_stop] = 150 J; ni entrenamiento ni base_post");
        // Sin t_stop (detencion forzada): ninguna ventana
        energia::resultadosCorrida(tmp.path(), marcas(T0, 0, 0, 0, false), &r, &error);
        comprobar(r["energia_experimento"].toObject()["ventanas"].toObject().isEmpty(), "sin t_stop: ninguna ventana");

        // Inferencia (D-014), 50 W: t_antes 2 s, t_despues 6 s, t_inf_antes 9 s, t_inf_despues 15 s,
        // t_stop 20 s. base_media 3 s = 150 J; inferencia 6 s = 300 J; base_post [15, 20] = 250 J
        fases::Marcas mi = marcas(T0, T0 + 2000, T0 + 6000, T0 + 20000, true);
        mi.tInfAntesMs = T0 + 9000;
        mi.tInfDespuesMs = T0 + 15000;
        mi.mediaIniciada = true;
        mi.inferenciaIniciada = true;
        const QJsonObject infOk{{"status_dll", 0}, {"n_predicciones", 1000000}, {"tiempo_por_prediccion_us", 6.0},
                                {"motivo_sin_por_prediccion", QJsonValue::Null}};
        comprobar(energia::resultadosCorrida(tmp.path(), mi, &r, &error, infOk), "resultados de una corrida con inferencia");
        const QJsonObject vi = r["energia_experimento"].toObject()["ventanas"].toObject();
        const QJsonObject vInf = vi["inferencia"].toObject();
        comprobar(cerca(vi["base_media"].toObject()["energia_j"].toDouble(), 150.0)
                      && cerca(vInf["energia_j"].toDouble(), 300.0) && cerca(vi["base_post"].toObject()["energia_j"].toDouble(), 250.0)
                      && cerca(vi["medicion"].toObject()["energia_j"].toDouble(), 1000.0),
                  "base_media [t_despues, t_inf_antes] = 150 J, inferencia = 300 J, base_post [t_inf_despues, t_stop] = 250 J; "
                  "medicion sigue de t_start a t_stop (1000 J)");
        comprobar(vInf["n_predicciones"].toInteger() == 1000000 && cerca(vInf["energia_por_prediccion_j"].toDouble(), 300.0 / 1e6)
                      && vInf["tiempo_por_prediccion_us"].toDouble() == 6.0 && vInf["motivo_sin_por_prediccion"].isNull(),
                  "inferencia OK: energia por prediccion = 300 J / 1e6 = 0,0003 J; tiempo por prediccion copiado de metadata");
        QJsonObject infCancelada = infOk;
        infCancelada["status_dll"] = 4;
        infCancelada["tiempo_por_prediccion_us"] = QJsonValue::Null;
        infCancelada["motivo_sin_por_prediccion"] = "inferencia cancelada";
        energia::resultadosCorrida(tmp.path(), mi, &r, &error, infCancelada);
        const QJsonObject vc = r["energia_experimento"].toObject()["ventanas"].toObject()["inferencia"].toObject();
        comprobar(cerca(vc["energia_j"].toDouble(), 300.0) && vc["energia_por_prediccion_j"].isNull()
                      && vc["tiempo_por_prediccion_us"].isNull() && vc["motivo_sin_por_prediccion"].toString() == "inferencia cancelada"
                      && vc["n_predicciones"].toInteger() == 1000000,
                  "inferencia cancelada: la ventana existe, por prediccion null con el motivo");
        QJsonObject infError = infCancelada;
        infError["status_dll"] = 3;
        infError["motivo_sin_por_prediccion"] = "error de inferir";
        energia::resultadosCorrida(tmp.path(), mi, &r, &error, infError);
        comprobar(r["energia_experimento"].toObject()["ventanas"].toObject()["inferencia"].toObject()["motivo_sin_por_prediccion"]
                          .toString() == "error de inferir",
                  "error de inferir: motivo 'error de inferir'");
        // Cancelada en base_media: base_media parcial hasta t_stop; sin inferencia ni base_post
        fases::Marcas enMedia = marcas(T0, T0 + 2000, T0 + 6000, T0 + 8000, false);
        enMedia.mediaIniciada = true;
        energia::resultadosCorrida(tmp.path(), enMedia, &r, &error, QJsonObject{{"status_dll", QJsonValue::Null}});
        const QJsonObject vm = r["energia_experimento"].toObject()["ventanas"].toObject();
        comprobar(cerca(vm["base_media"].toObject()["energia_j"].toDouble(), 100.0) && !vm.contains("inferencia")
                      && !vm.contains("base_post"),
                  "cancelada en base_media: base_media parcial [t_despues, t_stop] = 100 J; sin inferencia ni base_post");
    }

    // ------------------------------------------------------------ fases de la linea de tiempo
    {
        std::printf("\n== Fases de la corrida (FasesCorrida.h)\n");
        auto t = [](double s) { return static_cast<qint64>(1'800'000'000'000LL + s * 1000.0); };
        auto lista = [&](const fases::Marcas& m, int desde, int hasta) {
            QStringList r;
            for (int s = desde; s <= hasta; ++s) {
                const QString f = fases::faseFila(m, t(s));
                r << (f.isEmpty() ? "-" : f);
            }
            return r.join(',');
        };
        // Completada: t_start 0, t_antes 2,3, t_despues 5,6, t_stop 8, fase posterior empezada
        fases::Marcas m;
        m.tStartMs = t(0); m.tAntesMs = t(2.3); m.tDespuesMs = t(5.6); m.tStopMs = t(8); m.posteriorIniciada = true;
        comprobar(lista(m, 0, 9) == "-,base_pre,base_pre,transicion,entrenamiento,entrenamiento,transicion,base_post,base_post,-",
                  "completada: " + lista(m, 0, 9));
        // La misma, sin fase posterior: lo que sigue a t_despues no tiene fase (solo la transicion)
        fases::Marcas sinPost = m;
        sinPost.posteriorIniciada = false;
        comprobar(lista(sinPost, 5, 9) == "entrenamiento,transicion,-,-,-",
                  "fase posterior sin empezar: sin base_post (" + lista(sinPost, 5, 9) + ")");
        comprobar(!fases::basePost(sinPost).existe && fases::basePost(m).existe, "ventana base_post solo si la fase empezo");
        // Marca justo en el punto de la rejilla: el intervalo (t-1, t] no la cruza
        fases::Marcas exacta = m;
        exacta.tAntesMs = t(2);
        comprobar(lista(exacta, 2, 3) == "base_pre,entrenamiento", "t_antes en un punto de la rejilla: sin transicion");
        // Cancelada antes de entrenar: todo [t_start, t_stop] es base_pre (parcial)
        fases::Marcas antes;
        antes.tStartMs = t(0); antes.tStopMs = t(3.5);
        comprobar(lista(antes, 0, 4) == "-,base_pre,base_pre,base_pre,-", "sin t_antes: base_pre hasta t_stop (" + lista(antes, 0, 4) + ")");
        comprobar(fases::basePre(antes).existe && fases::basePre(antes).hastaMs == t(3.5) && !fases::entrenamiento(antes).existe,
                  "ventana base_pre parcial hasta t_stop; sin entrenamiento");
        // t_antes sin t_despues: lo que sigue a t_antes es entrenamiento
        fases::Marcas sinDespues;
        sinDespues.tStartMs = t(0); sinDespues.tAntesMs = t(2); sinDespues.tStopMs = t(5);
        comprobar(lista(sinDespues, 2, 6) == "base_pre,entrenamiento,entrenamiento,entrenamiento,-",
                  "sin t_despues: entrenamiento hasta t_stop (" + lista(sinDespues, 2, 6) + ")");
        // Detencion forzada (sin t_stop): ninguna fila tiene fase
        fases::Marcas forzada;
        forzada.tStartMs = t(0); forzada.tAntesMs = t(2);
        comprobar(lista(forzada, 0, 5) == "-,-,-,-,-,-", "sin t_stop: todas las filas sin fase");

        // Inferencia (D-014): t_inf_antes 7,2, t_inf_despues 10,4, t_stop 12,5, todas las fases empezadas
        fases::Marcas inf = m;
        inf.tInfAntesMs = t(7.2); inf.tInfDespuesMs = t(10.4); inf.tStopMs = t(12.5);
        inf.mediaIniciada = true; inf.inferenciaIniciada = true;
        comprobar(lista(inf, 0, 13) == "-,base_pre,base_pre,transicion,entrenamiento,entrenamiento,transicion,base_media,"
                                       "transicion,inferencia,inferencia,transicion,base_post,-",
                  "con inferencia: " + lista(inf, 0, 13));
        comprobar(fases::basePost(inf).desdeMs == t(10.4) && fases::baseMedia(inf).hastaMs == t(7.2)
                      && fases::inferencia(inf).desdeMs == t(7.2) && fases::inferencia(inf).hastaMs == t(10.4),
                  "ventanas: base_media [t_despues, t_inf_antes], inferencia [t_inf_antes, t_inf_despues], base_post desde t_inf_despues");
        // Cancelada en base_media: base_media hasta t_stop, sin inferencia
        fases::Marcas enMedia = m;
        enMedia.tStopMs = t(7.0); enMedia.posteriorIniciada = false; enMedia.mediaIniciada = true;
        comprobar(lista(enMedia, 6, 8) == "transicion,base_media,-" && !fases::inferencia(enMedia).existe,
                  "cancelada en base_media: " + lista(enMedia, 6, 8));
        // Cancelada en la inferencia: lo que sigue a t_inf_despues no tiene fase
        fases::Marcas enInf = inf;
        enInf.tInfDespuesMs = t(9.1); enInf.tStopMs = t(9.15); enInf.posteriorIniciada = false;
        comprobar(lista(enInf, 8, 11) == "transicion,inferencia,transicion,-" && !fases::basePost(enInf).existe,
                  "cancelada en la inferencia: " + lista(enInf, 8, 11));
        // fase_media_ms = 0: sin base intermedia; la inferencia empieza junto a t_despues
        fases::Marcas sinMedia = inf;
        sinMedia.mediaIniciada = false; sinMedia.tInfAntesMs = t(5.602);
        comprobar(lista(sinMedia, 5, 7) == "entrenamiento,transicion,inferencia" && !fases::baseMedia(sinMedia).existe,
                  "fase_media_ms 0: sin base_media (" + lista(sinMedia, 5, 7) + ")");
        // Las marcas de la inferencia se leen de metadata.json
        const fases::Marcas deMeta = fases::desdeMetadata(QJsonDocument::fromJson(R"({"tiempos": {
            "t_start": "2026-10-01T10:00:00.000Z", "t_inf_antes": "2026-10-01T10:00:40.000Z",
            "t_inf_despues": "2026-10-01T10:01:40.500Z", "fase_media_iniciada": true, "inferencia_iniciada": true}})").object());
        comprobar(deMeta.mediaIniciada && deMeta.inferenciaIniciada && deMeta.tInfAntesMs > 0
                      && deMeta.tInfDespuesMs - deMeta.tInfAntesMs == 60500,
                  "desdeMetadata lee t_inf_antes, t_inf_despues, fase_media_iniciada e inferencia_iniciada");
        comprobar(!fases::desdeMetadata(QJsonObject()).inferenciaIniciada, "corrida sin la marca: inferencia_iniciada = false");

        // Marca leida o deducida de metadata.json
        auto meta = [](const QString& extra, int status, int posteriorMs, const QString& tStop) {
            return QJsonDocument::fromJson(QString(R"({"entrenamiento": {"status_dll": %1}, "tiempos": {
                "t_start": "2026-10-01T10:00:00.000Z", "t_antes": "2026-10-01T10:00:20.000Z",
                "t_despues": "2026-10-01T10:00:30.000Z", "t_stop": "%2", "fase_posterior_ms": %3 %4}})")
                                               .arg(status).arg(tStop).arg(posteriorMs).arg(extra).toUtf8()).object();
        };
        const fases::Marcas nueva = fases::desdeMetadata(meta(R"(, "fase_posterior_iniciada": false)", 0, 20000, "2026-10-01T10:00:50.000Z"));
        comprobar(!nueva.posteriorIniciada && nueva.origenPosterior == "metadata", "con la marca en metadata se usa tal cual");
        const fases::Marcas vieja = fases::desdeMetadata(meta("", 0, 20000, "2026-10-01T10:00:41.500Z"));
        comprobar(vieja.posteriorIniciada && vieja.origenPosterior == "deducida",
                  "corrida vieja: status 0, fase_posterior_ms > 0 y t_stop - t_despues >= 1 s -> iniciada (deducida)");
        comprobar(!fases::desdeMetadata(meta("", 4, 20000, "2026-10-01T10:00:30.050Z")).posteriorIniciada,
                  "corrida vieja cancelada en el entrenamiento: no iniciada");
        comprobar(!fases::desdeMetadata(meta("", 0, 20000, "2026-10-01T10:00:30.400Z")).posteriorIniciada,
                  "corrida vieja con la DLL en OK pero t_stop a menos de 1 s de t_despues: no iniciada");
        comprobar(!fases::desdeMetadata(meta("", 0, 0, "2026-10-01T10:00:35.000Z")).posteriorIniciada,
                  "corrida vieja sin fase posterior (fase_posterior_ms = 0): no iniciada");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "ENERGIA OK" : "ENERGIA CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
