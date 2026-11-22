// test_auditoria.cpp - Auditoria de corridas (D-012) con DATOS SINTETICOS (inventados).
//
// Reproduce los dos casos con que se validan los umbrales provisionales, con datos equivalentes:
//   1. una corrida de una hora (66 min de entrenamiento) con actividad ajena en 10 minutos:
//      con_advertencias (10/66 = 15 % de los minutos > 10 %);
//   2. una calibracion de 3 epocas sin problemas: valida.
// Y comprueba cada regla en sus bordes: cobertura sin los huecos de borde, lineas base, caidas
// BLE, muestras saltadas, medidores, pasos, estado (con el motivo de cada fase), corridas viejas
// (no_evaluable no empeora el veredicto) y el residuo de CPU. Reglas version 2 (D-014): corridas
// con inferencia (cobertura_inferencia, pasos_inferencia, base_media, actividad ajena en la
// inferencia y los motivos de estado de las fases nuevas).

#include "Auditoria.h"
#include "AuditoriaReglas.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cmath>
#include <cstdio>
#include <functional>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

bool cerca(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

constexpr qint64 T0 = 1792656000000LL;  // 2026-10-22T10:00:00Z (t_start)

QString iso(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs); }

void escribir(const QString& ruta, const QByteArray& contenido)
{
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(contenido);
}

void escribirJson(const QString& ruta, const QJsonObject& o) { escribir(ruta, QJsonDocument(o).toJson()); }

// Una corrida sintetica: base previa de 20 s, entrenamiento de 'minutos' minutos, base posterior de
// 20 s; monitor de software (residuo de CPU 1 % en reposo; 'ajeno' puntos en los minutos marcados)
// y monitor fisico (resultados.json con cobertura completa salvo lo que se pida).
struct Corrida {
    int minutos = 66;
    QList<int> minutosAjenos;      // minutos de entrenamiento con actividad ajena
    double residuoAjeno = 8.0;     // residuo de CPU en esos minutos (puntos)
    double residuoReposo = 1.0;
    int pasos = 3;
    int pasosRegistrados = 3;
    QJsonObject ventanaEntrenamiento;  // vacia = cobertura completa
    QJsonArray medidores;              // vacio = los dos con codigo 0
    QJsonObject energiaInfo;           // vacio = 1 caida
    QJsonObject sistemaInfo;           // vacio = 0 muestras saltadas
    QString estado = "completada";
    // Inferencia (D-014): base_media de 20 s y 'minutosInferencia' minutos de inferencia entre el
    // entrenamiento y la base posterior
    bool inferencia = false;
    int minutosInferencia = 10;
    QList<int> minutosAjenosInferencia;
    int statusInferencia = 0;
    qint64 pasadas = 120;
    QJsonObject ventanaInferencia;     // vacia = cobertura completa
    QJsonObject ventanaMedia;          // vacia = cobertura completa
};

QString crear(const QTemporaryDir& tmp, const QString& nombre, const Corrida& c)
{
    const QString dir = tmp.filePath(nombre);
    QDir().mkpath(dir);
    const qint64 tAntes = T0 + 20000, tDespues = tAntes + c.minutos * 60000LL;
    const qint64 tInfAntes = c.inferencia ? tDespues + 20000 : 0;
    const qint64 tInfDespues = c.inferencia ? tInfAntes + c.minutosInferencia * 60000LL : 0;
    const qint64 tStop = (c.inferencia ? tInfDespues : tDespues) + 20000;
    QJsonArray med = c.medidores;
    if (med.isEmpty())
        med = {QJsonObject{{"nombre", "sistema"}, {"codigo_salida", 0}, {"forzado", false}, {"termino_antes", false}},
               QJsonObject{{"nombre", "energia"}, {"codigo_salida", 0}, {"forzado", false}, {"termino_antes", false}}};
    QJsonObject tiempos{{"t_start", iso(T0)}, {"t_antes", iso(tAntes)}, {"t_despues", iso(tDespues)},
                        {"t_stop", iso(tStop)}, {"fase_posterior_iniciada", true}};
    QJsonObject metadata{{"id_corrida", "sintetica/" + nombre}, {"estado", c.estado},
                         {"entrenamiento", QJsonObject{{"status_dll", 0}, {"pasos_registrados", c.pasosRegistrados}}},
                         {"medidores", med}};
    if (c.inferencia) {
        tiempos["t_inf_antes"] = iso(tInfAntes);
        tiempos["t_inf_despues"] = iso(tInfDespues);
        tiempos["fase_media_iniciada"] = true;
        tiempos["inferencia_iniciada"] = true;
        metadata["inferencia"] = QJsonObject{{"activada", true}, {"status_dll", c.statusInferencia}, {"pasadas", c.pasadas},
                                             {"grupos_registrados", c.minutosInferencia * 60}};
    } else {
        metadata["inferencia"] = QJsonObject{{"activada", false}};
    }
    metadata["tiempos"] = tiempos;
    escribirJson(dir + "/metadata.json", metadata);
    QByteArray p = "timestamp_inicio,timestamp_fin,step_index,step_total\n";
    for (int i = 0; i < c.pasosRegistrados; ++i) p += QString("%1,%2,%3,%4\n").arg(iso(tAntes), iso(tDespues)).arg(i).arg(c.pasos).toUtf8();
    escribir(dir + "/progreso.csv", p);

    // sistema_metrics.csv: total 8 s de CPU por fila (8 nucleos); el proceso usa 1 s y el medidor 0,04 s
    QByteArray s = "timestamp,timestamp_lectura,intervalo_ms,cpu_sis_ocupado_s,cpu_sis_total_s,ram_sis_usada_bytes,"
                   "rendimiento_cpu_pct,orq_cpu_s,orq_ws_bytes,orq_privado_bytes,med_cpu_s,med_ws_bytes,med_privado_bytes\n";
    int filas = 0;
    for (qint64 t = T0 + 1000; t <= tStop - 1; t += 1000) {
        const bool entrena = t - 1000 >= tAntes && t <= tDespues;
        const bool infiere = c.inferencia && t - 1000 >= tInfAntes && t <= tInfDespues;
        const int minuto = static_cast<int>((t - 1000 - tAntes) / 60000);
        const int minutoInf = static_cast<int>((t - 1000 - tInfAntes) / 60000);
        const double orq = entrena || infiere ? 1.0 : 0.0, medidor = 0.04;
        const bool ajeno = (entrena && c.minutosAjenos.contains(minuto)) || (infiere && c.minutosAjenosInferencia.contains(minutoInf));
        const double residuo = ajeno ? c.residuoAjeno : c.residuoReposo;
        const double ocupado = orq + medidor + residuo / 100.0 * 8.0;
        s += QString("%1,%1,1000.0,%2,8.0,1,,%3,1,1,%4,1,1\n").arg(iso(t)).arg(ocupado, 0, 'f', 4).arg(orq, 0, 'f', 4)
                 .arg(medidor, 0, 'f', 4).toUtf8();
        ++filas;
    }
    escribir(dir + "/sistema_metrics.csv", s);
    escribirJson(dir + "/sistema_info.json", c.sistemaInfo.isEmpty() ? QJsonObject{{"filas_escritas", filas}, {"muestras_saltadas", 0}}
                                                                     : c.sistemaInfo);
    escribirJson(dir + "/energia_info.json", c.energiaInfo.isEmpty() ? QJsonObject{{"cierre", QJsonObject{{"caidas", 1}}}}
                                                                     : c.energiaInfo);
    auto ventana = [](qint64 a, qint64 b) {
        const double d = (b - a) / 1000.0;
        return QJsonObject{{"inicio", iso(a)}, {"fin", iso(b)}, {"duracion_s", d}, {"segundos_cubiertos", d}, {"cobertura", 1.0},
                           {"energia_j", 40.0 * d}, {"huecos", QJsonArray()}};
    };
    QJsonObject ventanas{{"entrenamiento", c.ventanaEntrenamiento.isEmpty() ? ventana(tAntes, tDespues) : c.ventanaEntrenamiento},
                         {"medicion", ventana(T0, tStop)},
                         {"base_pre", ventana(T0, tAntes)},
                         {"base_post", ventana(c.inferencia ? tInfDespues : tDespues, tStop)}};
    if (c.inferencia) {
        ventanas["base_media"] = c.ventanaMedia.isEmpty() ? ventana(tDespues, tInfAntes) : c.ventanaMedia;
        ventanas["inferencia"] = c.ventanaInferencia.isEmpty() ? ventana(tInfAntes, tInfDespues) : c.ventanaInferencia;
    }
    escribirJson(dir + "/resultados.json",
                 QJsonObject{{"version_resultados", 3}, {"energia_experimento", QJsonObject{{"ventanas", ventanas}}}});
    return dir;
}

QJsonObject auditar(const QString& dir)
{
    QJsonObject a;
    QString error;
    if (!auditoria::auditarYGuardar(dir, &a, &error)) comprobar(false, "auditar: " + error);
    return a;
}

QJsonObject comprobacion(const QJsonObject& a, const QString& id)
{
    for (const QJsonValue& v : a["comprobaciones"].toArray())
        if (v.toObject()["id"].toString() == id) return v.toObject();
    return {};
}

QString resultado(const QJsonObject& a, const QString& id) { return comprobacion(a, id)["resultado"].toString(); }

QJsonObject leerMetadata(const QString& dir)
{
    QFile f(dir + "/metadata.json");
    f.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(f.readAll()).object();
}

void cambiarMetadata(const QString& dir, const std::function<void(QJsonObject&)>& cambio)
{
    QJsonObject m = leerMetadata(dir);
    cambio(m);
    escribirJson(dir + "/metadata.json", m);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;

    std::printf("\n== 1. Corrida de una hora con actividad ajena en 10 minutos (caso del 22/10)\n");
    {
        Corrida c;
        c.minutos = 66;
        for (int m = 30; m < 40; ++m) c.minutosAjenos << m;
        const QString dir = crear(tmp, "hora", c);
        const QJsonObject a = auditar(dir);
        comprobar(a["veredicto"].toString() == "con_advertencias", "veredicto con_advertencias (" + a["veredicto"].toString() + ")");
        const QJsonObject act = comprobacion(a, "actividad_ajena");
        comprobar(act["resultado"].toString() == "advertencia" && cerca(act["valor"].toDouble(), 100.0 * 10 / 66, 1e-9),
                  QString("actividad_ajena: 10 de 66 minutos = %1 % > 10 % (advertencia)").arg(act["valor"].toDouble()));
        comprobar(act["detalle"].toArray().size() == 1 && act["detalle"][0]["minutos"].toInt() == 66
                      && act["detalle"][0]["minutos_con_actividad"].toInt() == 10,
                  "detalle por fase: 66 minutos de entrenamiento, 10 con actividad");
        comprobar(act["motivo"].toString().contains("referencia 1.0 % de la base previa"),
                  "referencia: residuo medio de la base previa (20 filas >= 10): " + act["motivo"].toString());
        bool soloActividad = true;
        for (const QJsonValue& v : a["comprobaciones"].toArray()) {
            const QString id = v.toObject()["id"].toString(), r = v.toObject()["resultado"].toString();
            // Las dos de la inferencia son no_evaluable sin la inferencia activada (se comprueban abajo)
            if (id != "actividad_ajena" && id != "cobertura_inferencia" && id != "pasos_inferencia" && r != "ok") soloActividad = false;
        }
        comprobar(soloActividad, "las demas comprobaciones dan ok (salvo las de la inferencia, no evaluables)");
        comprobar(QFile::exists(dir + "/auditoria.json") && a["version_reglas"].toInt() == auditoria::reglas::VERSION_REGLAS
                      && a["version_reglas"].toInt() == 2,
                  "auditoria.json escrito con version_reglas = 2");
        comprobar(resultado(a, "cobertura_inferencia") == "no_evaluable" && resultado(a, "pasos_inferencia") == "no_evaluable"
                      && comprobacion(a, "pasos_inferencia")["motivo"].toString() == "la corrida no tiene la inferencia activada"
                      && comprobacion(a, "cobertura_inferencia")["motivo"].toString() == "la corrida no tiene la inferencia activada",
                  "sin inferencia activada: cobertura_inferencia y pasos_inferencia no_evaluable");
        comprobar(resultado(a, "reposo_base_previa") == "ok" && cerca(comprobacion(a, "reposo_base_previa")["valor"].toDouble(), 1.0),
                  "reposo_base_previa: residuo medio de 1 % en la base previa");
    }

    std::printf("\n== 2. Calibracion de 3 epocas sin problemas\n");
    {
        Corrida c;
        c.minutos = 3;
        const QJsonObject a = auditar(crear(tmp, "calibracion", c));
        comprobar(a["veredicto"].toString() == "valida", "veredicto valida (" + a["veredicto"].toString() + ")");
        comprobar(resultado(a, "pasos") == "ok" && resultado(a, "medidores") == "ok" && resultado(a, "actividad_ajena") == "ok"
                      && resultado(a, "cobertura_entrenamiento") == "ok" && resultado(a, "cobertura_lineas_base") == "ok"
                      && resultado(a, "caidas_ble") == "ok" && resultado(a, "muestras_saltadas") == "ok",
                  "pasos, medidores, actividad, coberturas, caidas y muestras: ok");
        const QJsonObject estado = comprobacion(a, "estado");
        comprobar(estado["resultado"].toString() == "ok" && !estado["umbral"].toString().isEmpty()
                      && !estado["descripcion"].toString().isEmpty() && estado.contains("valor") && estado.contains("motivo"),
                  "cada comprobacion trae id, descripcion, valor, umbral, resultado y motivo");
    }

    std::printf("\n== 3. Cobertura sin contar los huecos de borde\n");
    {
        auto conHuecos = [&](const QString& nombre, double duracion, double cubiertos, QJsonArray huecos) {
            Corrida c;
            c.minutos = 3;
            c.ventanaEntrenamiento = QJsonObject{{"duracion_s", duracion}, {"segundos_cubiertos", cubiertos}, {"huecos", huecos}};
            return auditar(crear(tmp, nombre, c));
        };
        auto hueco = [](const char* tipo, double d) { return QJsonObject{{"tipo", tipo}, {"duracion_s", d}}; };
        // 180 s con 10 s de borde final: 170/170 = 100 %
        QJsonObject a = conHuecos("borde", 180, 170, {hueco("borde_final", 10)});
        comprobar(resultado(a, "cobertura_entrenamiento") == "ok" && cerca(comprobacion(a, "cobertura_entrenamiento")["valor"].toDouble(), 100.0),
                  "huecos de borde: no cuentan (170 s cubiertos de 180 s con 10 s de borde = 100 %)");
        a = conHuecos("cob97", 100, 97, {hueco("entre_tramas", 3)});
        comprobar(resultado(a, "cobertura_entrenamiento") == "advertencia" && a["veredicto"].toString() == "con_advertencias",
                  "97 % entre tramas: advertencia (90-98 %)");
        a = conHuecos("cob98", 100, 98, {hueco("entre_tramas", 2)});
        comprobar(resultado(a, "cobertura_entrenamiento") == "ok", "98 %: ok (>= 98 %)");
        a = conHuecos("cob89", 100, 89, {hueco("entre_tramas", 11)});
        comprobar(resultado(a, "cobertura_entrenamiento") == "no_valida" && a["veredicto"].toString() == "no_valida",
                  "89 %: no_valida (< 90 %) y veredicto no_valida");
        a = conHuecos("solo_bordes", 5, 0, {hueco("borde_inicial", 5)});
        comprobar(resultado(a, "cobertura_entrenamiento") == "no_evaluable", "ventana con solo huecos de borde: no_evaluable");
    }

    std::printf("\n== 4. Caidas BLE, muestras saltadas y medidores\n");
    {
        Corrida c;
        c.minutos = 3;
        c.energiaInfo = QJsonObject{{"cierre", QJsonObject{{"caidas", 3}}}};
        comprobar(resultado(auditar(crear(tmp, "caidas3", c)), "caidas_ble") == "advertencia", "3 caidas BLE: advertencia");
        c.energiaInfo = QJsonObject{{"cierre", QJsonObject{{"caidas", 2}}}};
        comprobar(resultado(auditar(crear(tmp, "caidas2", c)), "caidas_ble") == "ok", "2 caidas BLE: ok");

        Corrida s;
        s.minutos = 3;
        for (const auto& [saltadas, esperado] : QList<QPair<int, QString>>{{1, "ok"}, {2, "advertencia"}, {10, "advertencia"}, {11, "no_valida"}}) {
            s.sistemaInfo = QJsonObject{{"filas_escritas", 100}, {"muestras_saltadas", saltadas}};
            comprobar(resultado(auditar(crear(tmp, QString("saltadas%1").arg(saltadas), s)), "muestras_saltadas") == esperado,
                      QString("%1 muestras saltadas de 100: %2").arg(saltadas).arg(esperado));
        }

        auto medidor = [](int codigo, bool forzado, bool antes) {
            return QJsonObject{{"nombre", "energia"}, {"codigo_salida", codigo}, {"forzado", forzado}, {"termino_antes", antes}};
        };
        Corrida m;
        m.minutos = 3;
        m.medidores = {medidor(2, false, false)};
        QJsonObject a = auditar(crear(tmp, "med_codigo2", m));
        comprobar(resultado(a, "medidores") == "advertencia"
                      && comprobacion(a, "medidores")["motivo"].toString() == "energia: código 2: terminó por el tope de duración",
                  "codigo distinto de 0 sin forzar: advertencia con el codigo y su significado ("
                      + comprobacion(a, "medidores")["motivo"].toString() + ")");
        m.medidores = {medidor(0, true, false)};
        comprobar(resultado(auditar(crear(tmp, "med_forzado", m)), "medidores") == "no_valida", "cerrado a la fuerza: no_valida");
        m.medidores = {medidor(5, false, true)};
        comprobar(resultado(auditar(crear(tmp, "med_antes", m)), "medidores") == "no_valida", "termino antes de tiempo: no_valida");
        m.medidores = {QJsonObject{{"nombre", "sistema"}, {"codigo_salida", QJsonValue::Null}, {"forzado", false}, {"termino_antes", false}}};
        comprobar(resultado(auditar(crear(tmp, "med_null", m)), "medidores") == "no_valida", "sin codigo de salida (no termino): no_valida");
    }

    std::printf("\n== 5. Pasos y estado\n");
    {
        Corrida c;
        c.minutos = 3;
        c.pasos = 5;
        c.pasosRegistrados = 4;
        comprobar(resultado(auditar(crear(tmp, "pasos_faltan", c)), "pasos") == "no_valida",
                  "completada con 4 de 5 pasos registrados: no_valida");

        const QString dir = crear(tmp, "estados", Corrida{});
        auto motivoEstado = [&](const std::function<void(QJsonObject&)>& cambio) {
            cambiarMetadata(dir, cambio);
            const QJsonObject a = auditar(dir);
            return QPair<QString, QString>(resultado(a, "estado"), comprobacion(a, "estado")["motivo"].toString());
        };
        auto e = motivoEstado([](QJsonObject& m) {
            m["estado"] = "cancelada";
            QJsonObject ent = m["entrenamiento"].toObject();
            ent["status_dll"] = 4;
            m["entrenamiento"] = ent;
        });
        comprobar(e.first == "advertencia" && e.second == "cancelada durante el entrenamiento: la ventana de entrenamiento es parcial",
                  "cancelada en el entrenamiento: advertencia (" + e.second + ")");
        comprobar(resultado(auditar(dir), "pasos") == "no_evaluable", "pasos: no_evaluable si la corrida no esta completada");
        e = motivoEstado([](QJsonObject& m) {
            QJsonObject ent = m["entrenamiento"].toObject();
            ent["status_dll"] = 0;
            m["entrenamiento"] = ent;
        });
        comprobar(e.first == "advertencia" && e.second == "cancelada durante la línea base posterior: el entrenamiento está completo",
                  "cancelada en la base posterior: " + e.second);
        e = motivoEstado([](QJsonObject& m) {
            QJsonObject t = m["tiempos"].toObject();
            t["fase_posterior_iniciada"] = false;
            m["tiempos"] = t;
        });
        comprobar(e.second == "cancelada al terminar el entrenamiento: el entrenamiento está completo",
                  "cancelada entre el entrenamiento y la base posterior: " + e.second);
        e = motivoEstado([](QJsonObject& m) {
            QJsonObject t = m["tiempos"].toObject();
            t.remove("t_antes");
            m["tiempos"] = t;
        });
        comprobar(e.second == "cancelada durante la línea base previa: no hubo entrenamiento", "cancelada en la base previa: " + e.second);
        e = motivoEstado([](QJsonObject& m) {
            m["estado"] = "error";
            m["error"] = QJsonObject{{"tipo", "dll"}, {"mensaje", "La DLL devolvio: datos invalidos"}};
        });
        comprobar(e.first == "advertencia" && e.second.startsWith("error de la DLL durante el entrenamiento"),
                  "error de la DLL: advertencia, nunca no valida (" + e.second + ")");
        e = motivoEstado([](QJsonObject& m) { m["estado"] = "en_curso"; });
        comprobar(e.first == "advertencia" && e.second == "detención forzada: corrida incompleta",
                  "detencion forzada (--postprocesar): " + e.second);
    }

    std::printf("\n== 6. Corrida vieja o sin medidores: no_evaluable no empeora el veredicto\n");
    {
        QTemporaryDir vieja;
        escribirJson(vieja.filePath("metadata.json"),
                     QJsonObject{{"estado", "completada"}, {"entrenamiento", QJsonObject{{"pasos_registrados", 1}}},
                                 {"medidores", QJsonArray()}, {"tiempos", QJsonObject{{"t_start", iso(T0)}}}});
        escribir(vieja.filePath("progreso.csv"), "timestamp_inicio,timestamp_fin,step_index,step_total\nx,y,0,1\n");
        QJsonObject a;
        QString error;
        const bool auditada = auditoria::auditar(vieja.path(), &a, &error);
        comprobar(auditada && a["veredicto"].toString() == "valida",
                  "sin medidores ni resultados.json: valida (" + a["veredicto"].toString() + ")");
        int noEvaluables = 0;
        for (const QJsonValue& v : a["comprobaciones"].toArray()) noEvaluables += v.toObject()["resultado"] == "no_evaluable";
        comprobar(noEvaluables == 9 && resultado(a, "estado") == "ok" && resultado(a, "pasos") == "ok",
                  QString("9 comprobaciones no evaluables (las 2 de la inferencia incluidas); estado y pasos ok (%1)").arg(noEvaluables));
        comprobar(!QFile::exists(vieja.filePath("auditoria.json")), "auditar() no escribe nada");
        QTemporaryDir vacia;
        const bool sinMetadata = !auditoria::auditar(vacia.path(), &a, &error);
        comprobar(sinMetadata && !error.isEmpty(), "sin metadata.json: error (" + error + ")");
    }

    std::printf("\n== 7. Residuo de CPU y referencia por defecto\n");
    {
        double r = 0;
        comprobar(auditoria::residuoCpu(3.0, 8.0, 1.0, 0.04, &r) && cerca(r, 24.5),
                  "residuo = 100 x (3 - 1 - 0,04) / 8 = 24,5 %");
        comprobar(!auditoria::residuoCpu(1.0, 0.0, 0.5, 0.0, &r), "sin CPU total: no hay residuo");
        // Base previa de 5 filas (< 10): la referencia es 3 % y el umbral 3 + 3 = 6 %. Con 7 % de
        // residuo en todo el entrenamiento, el 100 % de los minutos tiene actividad ajena.
        Corrida c;
        c.minutos = 2;
        c.residuoReposo = 7.0;
        QTemporaryDir t2;
        const QString dir = crear(t2, "ref", c);
        // t_start 5 s antes de t_antes: la base previa queda con 5 filas
        cambiarMetadata(dir, [](QJsonObject& m) {
            QJsonObject t = m["tiempos"].toObject();
            t["t_start"] = iso(T0 + 15000);
            m["tiempos"] = t;
        });
        const QJsonObject a = auditar(dir);
        const QJsonObject act = comprobacion(a, "actividad_ajena");
        comprobar(act["motivo"].toString().contains("referencia 3.0 % por defecto") && act["resultado"].toString() == "advertencia"
                      && resultado(a, "reposo_base_previa") == "ok",
                  "base previa con menos de 10 filas: referencia 3 %; 7 % > 6 % en todos los minutos (" + act["motivo"].toString() + ")");
        comprobar(a["veredicto"].toString() == "con_advertencias", "actividad_ajena nunca invalida: con_advertencias");
    }

    std::printf("\n== 8. Corrida con inferencia (reglas version 2, D-014)\n");
    {
        QTemporaryDir t8;
        Corrida c;
        c.minutos = 5;
        c.inferencia = true;
        const QString dir = crear(t8, "inferencia", c);
        const QJsonObject a = auditar(dir);
        comprobar(a["veredicto"].toString() == "valida" && a["version_reglas"].toInt() == 2,
                  "inferencia completa sin problemas: valida, reglas version 2 (" + a["veredicto"].toString() + ")");
        comprobar(resultado(a, "cobertura_inferencia") == "ok" && cerca(comprobacion(a, "cobertura_inferencia")["valor"].toDouble(), 100.0),
                  "cobertura_inferencia: 100 % (ok)");
        comprobar(resultado(a, "pasos_inferencia") == "ok" && comprobacion(a, "pasos_inferencia")["valor"].toDouble() == 120
                      && comprobacion(a, "pasos_inferencia")["motivo"].toString().startsWith("120 pasada(s) completa(s)"),
                  "pasos_inferencia: 120 pasadas completas (" + comprobacion(a, "pasos_inferencia")["motivo"].toString() + ")");
        QStringList bases;
        for (const QJsonValue& v : comprobacion(a, "cobertura_lineas_base")["detalle"].toArray()) bases << v.toObject()["ventana"].toString();
        comprobar(bases == QStringList({"base_pre", "base_media", "base_post"}), "cobertura_lineas_base incluye base_media: " + bases.join(','));
        const QJsonArray act = comprobacion(a, "actividad_ajena")["detalle"].toArray();
        comprobar(act.size() == 2 && act[1].toObject()["fase"].toString() == "inferencia" && act[1].toObject()["minutos"].toInt() == 10,
                  "actividad_ajena evalua tambien los 10 minutos de inferencia");

        // Cobertura de la inferencia: los mismos umbrales que el entrenamiento, sin los huecos de borde
        auto conCobertura = [&](const QString& nombre, double cubiertos, const QJsonArray& huecos) {
            Corrida x = c;
            const double d = 600.0;
            x.ventanaInferencia = QJsonObject{{"duracion_s", d}, {"segundos_cubiertos", cubiertos}, {"cobertura", cubiertos / d},
                                              {"energia_j", 40.0 * cubiertos}, {"huecos", huecos}};
            return auditar(crear(t8, nombre, x));
        };
        const QJsonArray entre{QJsonObject{{"tipo", "entre_tramas"}, {"duracion_s", 30.0}}};
        comprobar(resultado(conCobertura("inf95", 570.0, entre), "cobertura_inferencia") == "advertencia",
                  "cobertura_inferencia de 95 %: advertencia");
        const QJsonArray grande{QJsonObject{{"tipo", "entre_tramas"}, {"duracion_s", 90.0}}};
        const QJsonObject a85 = conCobertura("inf85", 510.0, grande);
        comprobar(resultado(a85, "cobertura_inferencia") == "no_valida" && a85["veredicto"].toString() == "no_valida",
                  "cobertura_inferencia de 85 %: no_valida (veredicto no_valida)");
        const QJsonArray borde{QJsonObject{{"tipo", "borde_final"}, {"duracion_s", 30.0}}};
        comprobar(resultado(conCobertura("inf_borde", 570.0, borde), "cobertura_inferencia") == "ok",
                  "cobertura_inferencia sin el hueco de borde final: 100 % (ok)");
        // base_media por debajo de 90 %: advertencia en cobertura_lineas_base
        Corrida media = c;
        media.ventanaMedia = QJsonObject{{"duracion_s", 20.0}, {"segundos_cubiertos", 16.0}, {"cobertura", 0.8}, {"energia_j", 640.0},
                                         {"huecos", QJsonArray{QJsonObject{{"tipo", "entre_tramas"}, {"duracion_s", 4.0}}}}};
        comprobar(resultado(auditar(crear(t8, "media80", media)), "cobertura_lineas_base") == "advertencia",
                  "base_media con 80 % de cobertura: cobertura_lineas_base en advertencia");
        // pasos_inferencia
        Corrida cero = c;
        cero.pasadas = 0;
        comprobar(resultado(auditar(crear(t8, "cero", cero)), "pasos_inferencia") == "no_valida",
                  "inferencia completa sin ninguna pasada: pasos_inferencia no_valida");
        Corrida cancelada = c;
        cancelada.statusInferencia = 4;
        comprobar(resultado(auditar(crear(t8, "inf_cancelada", cancelada)), "pasos_inferencia") == "no_evaluable",
                  "inferencia cancelada: pasos_inferencia no_evaluable (la inferencia no termino completa)");
        // Actividad ajena en 3 de los 10 minutos de inferencia (30 % > 10 %): advertencia
        Corrida ajena = c;
        ajena.minutosAjenosInferencia = {2, 3, 4};
        const QJsonObject aa = auditar(crear(t8, "inf_ajena", ajena));
        const QJsonArray detAjena = comprobacion(aa, "actividad_ajena")["detalle"].toArray();
        comprobar(resultado(aa, "actividad_ajena") == "advertencia" && detAjena.size() == 2
                      && detAjena[0].toObject()["resultado"].toString() == "ok"
                      && detAjena[1].toObject()["minutos_con_actividad"].toInt() == 3
                      && detAjena[1].toObject()["resultado"].toString() == "advertencia",
                  "actividad ajena en 3 de 10 minutos de inferencia: advertencia (el entrenamiento, ok)");

        // Motivos de estado de las fases nuevas
        const QString dirE = crear(t8, "estados_inf", c);
        auto motivo = [&](const std::function<void(QJsonObject&)>& cambio) {
            cambiarMetadata(dirE, cambio);
            const QJsonObject x = auditar(dirE);
            return QPair<QString, QString>(resultado(x, "estado"), comprobacion(x, "estado")["motivo"].toString());
        };
        auto e = motivo([](QJsonObject& m) {
            m["estado"] = "cancelada";
            QJsonObject t = m["tiempos"].toObject();
            t["fase_posterior_iniciada"] = false;
            t["inferencia_iniciada"] = false;
            m["tiempos"] = t;
        });
        comprobar(e.first == "advertencia" && e.second == "cancelada durante la línea base intermedia: el entrenamiento está completo",
                  "cancelada en base_media: " + e.second);
        e = motivo([](QJsonObject& m) {
            QJsonObject t = m["tiempos"].toObject();
            t["inferencia_iniciada"] = true;
            m["tiempos"] = t;
        });
        comprobar(e.first == "advertencia" && e.second == "cancelada durante la inferencia: el entrenamiento está completo",
                  "cancelada en la inferencia: " + e.second);
        e = motivo([](QJsonObject& m) {
            m["estado"] = "error";
            m["error"] = QJsonObject{{"tipo", "inferencia"}, {"mensaje", "inferir devolvio: error interno de la DLL"}};
        });
        comprobar(e.first == "advertencia" && e.second.startsWith("error de inferir: el entrenamiento está completo"),
                  "error de inferir: advertencia (" + e.second + ")");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "AUDITORIA OK" : "AUDITORIA CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
