// test_subidas.cpp - Paquete de subida y cola de pendientes (D-011), contra un servidor falso.
//
// Arma una corrida con archivos escritos a mano (DATOS INVENTADOS, con el formato de
// metadata.json, resultados.json, progreso.csv y los CSV de los medidores) y comprueba:
//   1. el paquete de subir_corrida (configuracion, resumen, pasos, metricas y serie);
//   2. que la serie, tal como la devuelve la base, se exporta a un CSV identico al local;
//   3. la cola: pendiente sin sesion, subida al iniciar sesion, reintento tras un fallo del
//      servidor, "ya subida" (409) y rechazo con reintento manual.

#include "ClienteSupabase.h"
#include "ColaSubidas.h"
#include "Historial.h"
#include "HistorialRemoto.h"
#include "PaqueteCorrida.h"
#include "ResumenCorrida.h"
#include "ServidorSupabaseFalso.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
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

bool cerca(double a, double b) { return std::fabs(a - b) < 1e-6; }

bool esperar(const std::function<bool()>& condicion, int limiteMs = 8000)
{
    QElapsedTimer t;
    t.start();
    while (!condicion() && t.elapsed() < limiteMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return condicion();
}

void escribir(const QString& ruta, const QByteArray& contenido)
{
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(contenido);
}

QByteArray leer(const QString& ruta)
{
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Marcas de la linea de tiempo segun metadata.json (como Historial::exportarCsv)
fases::Marcas marcas(const QString& dirCorrida)
{
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    return f.open(QIODevice::ReadOnly) ? fases::desdeMetadata(QJsonDocument::fromJson(f.readAll()).object()) : fases::Marcas();
}

QString ts(int segundo) { return QString("2026-10-01T10:00:%1.000Z").arg(segundo, 2, 10, QChar('0')); }

// Corrida: t_start 10:00:00, entrenamiento 10:00:02 - 10:00:06, t_stop 10:00:08, datos CSV
// t_creacion por defecto distinto para cada numero (identifica la corrida en la base)
QString crearCorrida(const QDir& raiz, const QString& experimento, int numero, const QString& rutaDatos,
                     QString tCreacion = {})
{
    if (tCreacion.isEmpty()) tCreacion = QString("2026-10-01T09:%1:50.000Z").arg(50 + numero % 10);
    const QString dir = raiz.filePath(QString("corridas/%1/corrida_%2").arg(experimento).arg(numero, 3, 10, QChar('0')));
    QDir().mkpath(dir);
    QString rutaJson = rutaDatos;
    rutaJson.replace('\\', "\\\\");
    escribir(dir + "/metadata.json", QString(R"json({
      "version_metadata": 1, "experimento": "%1", "corrida": %2, "id_corrida": "%1/corrida_%3",
      "estado": "completada", "error": null,
      "modelo": {"clave": "arbol_decision", "dll": "EnergIA_Arbol.dll", "nombre": "Arbol de decision",
                 "version_contrato": 3, "interno": false, "unidad_paso": "arbol"},
      "hiperparametros": {"criterio": "entropia", "ganancia_min": 1e-08, "max_depth": 0, "min_hoja": 10},
      "datos": {"origen": "csv", "ruta": "%4", "columna_objetivo": "# label", "n_clases": 2, "n_cols": 3,
                "n_filas_total": 4, "n_train": 3, "n_val": 1, "porcentaje_train": 80, "semilla": 42},
      "entrenamiento": {"pasos_registrados": 2, "status_dll": 0},
      "evaluacion": {"metricas": {"val_accuracy": 0.75}},
      "tiempos": {"t_creacion": "%9", "t_start": "%5", "t_antes": "%6", "t_despues": "%7",
                  "t_stop": "%8", "t_fin": "%8", "duracion_entrenamiento_ms": 4000},
      "medidores": [{"nombre": "sistema", "listo_en_t_start": null}, {"nombre": "energia", "listo_en_t_start": true}]
    })json").arg(experimento).arg(numero).arg(numero, 3, 10, QChar('0')).arg(rutaJson, ts(0), ts(2), ts(6), ts(8)).arg(tCreacion).toUtf8());

    escribir(dir + "/progreso.csv",
             "timestamp_inicio,timestamp_fin,step_index,step_total,metrica_1_nombre,metrica_1_valor,metrica_2_nombre,"
             "metrica_2_valor,metrica_3_nombre,metrica_3_valor,metrica_4_nombre,metrica_4_valor\n"
             + ts(2).toUtf8() + "," + ts(4).toUtf8() + ",0,2,profundidad,5,hojas,12,,,,\n"
             + ts(4).toUtf8() + "," + ts(6).toUtf8() + ",1,2,profundidad,7,,,,,,\n");
    escribir(dir + "/sistema_info.json", R"({"ram_total_bytes": 8000000000})");
    QByteArray s = "timestamp,timestamp_lectura,intervalo_ms,cpu_sis_ocupado_s,cpu_sis_total_s,ram_sis_usada_bytes,"
                   "rendimiento_cpu_pct,orq_cpu_s,orq_ws_bytes,orq_privado_bytes,med_cpu_s,med_ws_bytes,med_privado_bytes\n";
    for (int i = 1; i <= 8; ++i) {
        const bool entrena = i >= 3 && i <= 6;
        s += QString("%1,%1,1000.0,%2,8.0000,4000000000,,%3,%4,1,0.0000,1,1\n")
                 .arg(ts(i)).arg(entrena ? "2.0000" : "0.8000").arg(entrena ? "1.0000" : "0.0000")
                 .arg(entrena ? 209715200 : 104857600).toUtf8();
    }
    escribir(dir + "/sistema_metrics.csv", s);
    QByteArray e = "timestamp,timestamp_trama,conectado,voltage_v,current_a,power_w,energy_kwh,raw_hex\n";
    for (int i = 0; i <= 8; ++i) {
        if (i == 7) { e += QString("%1,,0,,,,,\n").arg(ts(i)).toUtf8(); continue; }
        const bool entrena = i >= 2 && i <= 6;
        e += QString("%1,2026-10-01T09:59:%2.950Z,1,220.0,%3,%4,1.00,ab%2\n").arg(ts(i)).arg(59 - 8 + i, 2, 10, QChar('0'))
                 .arg(entrena ? "0.182" : "0.091").arg(i == 4 ? "45.5" : entrena ? "40.0" : "20.0").toUtf8();
    }
    escribir(dir + "/energia_atorch.csv", e);
    escribir(dir + "/resultados.json", R"({"version_resultados": 1, "energia_experimento": {"metodo": "trapecio_tramas_unicas",
        "version_metodo": 1, "ventanas": {
          "entrenamiento": {"energia_wh": 0.0444444, "potencia_media_w": 40.0, "cobertura": 1.0},
          "medicion": {"energia_wh": 0.0666666, "potencia_media_w": 30.0, "cobertura": 1.0},
          "base_pre": {"energia_wh": 0.0111111, "potencia_media_w": 20.0, "cobertura": 1.0}}}})");
    return dir;
}

// Agrega a una corrida de crearCorrida una inferencia (D-014): metadata.inferencia y sus marcas,
// progreso_inferencia.csv con dos grupos y las ventanas base_media e inferencia en resultados.json
void conInferencia(const QString& dir)
{
    QJsonObject m = QJsonDocument::fromJson(leer(dir + "/metadata.json")).object();
    QJsonObject t = m["tiempos"].toObject();
    t["t_inf_antes"] = ts(6);
    t["t_inf_despues"] = ts(7);
    t["fase_media_iniciada"] = false;
    t["inferencia_iniciada"] = true;
    m["tiempos"] = t;
    m["inferencia"] = QJsonObject{{"activada", true}, {"lote", 65536}, {"duracion_min_s", 60}, {"paso_min_ms", 1000},
                                  {"datos", "validacion"}, {"n_filas", 1}, {"status_dll", 0}, {"grupos_registrados", 2},
                                  {"pasadas", 400000}, {"n_predicciones", 400000}, {"duracion_ms", 1000},
                                  {"tiempo_por_prediccion_us", 2.5}, {"motivo_sin_por_prediccion", QJsonValue::Null}};
    escribir(dir + "/metadata.json", QJsonDocument(m).toJson());
    escribir(dir + "/progreso_inferencia.csv",
             "timestamp_inicio,timestamp_fin,step_index,step_total,metrica_1_nombre,metrica_1_valor,metrica_2_nombre,"
             "metrica_2_valor,metrica_3_nombre,metrica_3_valor,metrica_4_nombre,metrica_4_valor\n"
             "2026-10-01T10:00:06.000Z,2026-10-01T10:00:06.500Z,0,0,pasadas,200000,predicciones,200000,,,,\n"
             "2026-10-01T10:00:06.500Z,2026-10-01T10:00:07.000Z,1,0,pasadas,200000,predicciones,200000,,,,\n");
    QJsonObject r = QJsonDocument::fromJson(leer(dir + "/resultados.json")).object();
    QJsonObject e = r["energia_experimento"].toObject();
    QJsonObject v = e["ventanas"].toObject();
    v["inferencia"] = QJsonObject{{"energia_wh", 0.0111111}, {"energia_j", 40.0}, {"n_predicciones", 400000},
                                  {"energia_por_prediccion_j", 0.0001}, {"tiempo_por_prediccion_us", 2.5}};
    v["base_media"] = QJsonObject{{"energia_wh", 0.0}, {"energia_j", 0.0}};
    e["ventanas"] = v;
    r["energia_experimento"] = e;
    r["version_resultados"] = 3;
    escribir(dir + "/resultados.json", QJsonDocument(r).toJson());
}

ClienteSupabase::Opciones opcionesSesion(const QString& url, const QString& rutaSesion)
{
    ClienteSupabase::Opciones o;
    o.urlBase = url;
    o.llavePublica = "llave-publica-de-prueba";
    o.rutaSesion = rutaSesion;
    o.limiteAuthMs = 3000;
    o.limiteRestMs = 3000;
    return o;
}

ColaSubidas::Opciones opcionesCola(const QTemporaryDir& tmp, const QString& nombre)
{
    ColaSubidas::Opciones o;
    o.rutaCola = tmp.filePath(nombre + "/subidas.json");
    o.rutaHuellas = tmp.filePath(nombre + "/huellas.json");
    o.equipo = "PC-PRUEBA";
    o.esperaInicialMs = 200;
    o.esperaMaximaMs = 400;
    return o;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QDir raiz(tmp.path());
    const QString rutaDatos = QDir::toNativeSeparators(tmp.filePath("datos.csv"));
    const QByteArray contenidoDatos = "# label,a,b,c\n1,0.1,0.2,0.3\n0,0.4,0.5,0.6\n1,0.7,0.8,0.9\n0,1.0,1.1,1.2\n";
    escribir(rutaDatos, contenidoDatos);
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(contenidoDatos, QCryptographicHash::Sha256).toHex());
    const QString dirA = crearCorrida(raiz, "exp_a", 1, rutaDatos);

    std::printf("\n== 1. Paquete de subir_corrida\n");
    QString error;
    comprobar(PaqueteCorrida::huellaArchivo(rutaDatos, &error) == sha, "sha256 del dataset");
    comprobar(PaqueteCorrida::rutaDataset(dirA) == rutaDatos, "ruta del dataset desde metadata.json");
    const QJsonObject p = PaqueteCorrida::construir(dirA, "PC-PRUEBA", sha, &error);
    comprobar(error.isEmpty(), "paquete sin error " + error);
    const QJsonObject c = p["corrida"].toObject();
    comprobar(p["experimento"].toObject()["nombre"].toString() == "exp_a",
              "experimento por nombre (corrida sin experimento_carpeta: se sube como antes)");
    {
        // Corrida nueva con tildes: la carpeta usa el nombre seguro y metadata.json el original
        const QString dirT = crearCorrida(raiz, "_rbol_HEPMASS", 1, rutaDatos);
        QFile fm(dirT + "/metadata.json");
        QJsonObject mt;
        if (fm.open(QIODevice::ReadOnly)) mt = QJsonDocument::fromJson(fm.readAll()).object();
        fm.close();
        mt["experimento"] = QString::fromUtf8("Árbol HEPMASS");
        mt["experimento_carpeta"] = "_rbol_HEPMASS";
        if (fm.open(QIODevice::WriteOnly)) fm.write(QJsonDocument(mt).toJson());
        fm.close();
        QString errorT;
        const QJsonObject pt = PaqueteCorrida::construir(dirT, "PC-PRUEBA", sha, &errorT);
        comprobar(errorT.isEmpty() && pt["experimento"].toObject()["nombre"].toString() == QString::fromUtf8("Árbol HEPMASS"),
                  "se sube el nombre original del experimento: " + pt["experimento"].toObject()["nombre"].toString());
        comprobar(pt["corrida"].toObject()["metadata"].toObject()["experimento_carpeta"].toString() == "_rbol_HEPMASS",
                  "la copia de metadata.json conserva experimento_carpeta");
        QDir(dirT).removeRecursively();
    }
    comprobar(p["dataset"].toObject()["sha256"].toString() == sha && p["dataset"].toObject()["nombre"].toString() == "datos.csv",
              "dataset: huella y nombre del archivo");
    comprobar(p["dataset"].toObject()["columnas"].toInt() == 4 && p["dataset"].toObject()["filas"].toInt() == 4,
              "dataset: columnas = atributos + objetivo, filas totales");
    comprobar(c["equipo"].toString() == "PC-PRUEBA" && c["numero"].toInt() == 1 && c["visibilidad"].toString() == "privada",
              "equipo, numero y visibilidad privada por defecto");
    comprobar(c["estado"].toString() == "completada" && c["status_code"].toInt() == 0 && c["modelo_dll"].toString() == "EnergIA_Arbol.dll"
                  && c["contrato_version"].toInt() == 3,
              "estado, status_code, modelo_dll y contrato");
    comprobar(cerca(c["pct_entrenamiento"].toDouble(), 0.8) && c["semilla"].toInt() == 42, "pct_entrenamiento como fraccion y semilla");
    comprobar(c["t_start"].toString() == ts(0) && c["t_despues"].toString() == ts(6), "marcas temporales de metadata.json");
    comprobar(c["listo_en_t_start"].toBool() && cerca(c["accuracy_val"].toDouble(), 0.75), "listo_en_t_start y accuracy_val");
    comprobar(cerca(c["duracion_entrenamiento_s"].toDouble(), 4.0), "duracion del entrenamiento en s");
    comprobar(cerca(c["energia_entrenamiento_wh"].toDouble(), 0.0444444) && cerca(c["energia_total_wh"].toDouble(), 0.0666666),
              "energia de las ventanas entrenamiento y medicion (resultados.json)");
    comprobar(cerca(c["energia_base_pre_wh"].toDouble(), 0.0111111) && c["energia_base_post_wh"].isNull(),
              "energia de la linea base desde resultados.json: base_pre en bruto; base_post null (la ventana no existe)");
    comprobar(cerca(c["potencia_media_w"].toDouble(), 40.0) && cerca(c["potencia_max_w"].toDouble(), 45.5),
              "potencia media (resultados.json) y maxima en el entrenamiento");
    comprobar(cerca(c["cpu_media_pct"].toDouble(), 12.5), "CPU media del proceso en el entrenamiento");
    comprobar(c["metadata"].toObject()["id_corrida"].toString() == "exp_a/corrida_001", "copia integra de metadata.json");
    comprobar(c["resultados"].toObject()["energia_experimento"].toObject()["metodo"].toString() == "trapecio_tramas_unicas",
              "copia de resultados.json");
    const QJsonObject hw = p["hardware"].toObject();
    comprobar(hw["nombre"].toString() == "PC-PRUEBA" && hw["ram_total_bytes"].toInteger() == 8000000000LL
                  && !hw["sistema_operativo"].toString().isEmpty() && hw["nucleos_logicos"].toInt() > 0,
              "equipo: nombre, RAM de sistema_info.json, sistema operativo y nucleos");
#ifdef Q_OS_WIN
    comprobar(!hw["cpu"].toString().isEmpty(), "equipo: modelo de CPU (" + hw["cpu"].toString() + ")");
#endif
    const QJsonArray hiper = p["hiperparametros"].toArray();
    QMap<QString, QString> h;
    for (const QJsonValue& v : hiper) h[v.toObject()["nombre"].toString()] = v.toObject()["valor"].toString();
    comprobar(h.size() == 4 && h["criterio"] == "entropia" && h["max_depth"] == "0" && h["ganancia_min"] == "1e-08",
              "hiperparametros como texto: " + QStringList(h.values()).join(" | "));
    comprobar(p["pasos"].toArray().size() == 2 && p["pasos"].toArray()[1].toObject()["step_total"].toInt() == 2
                  && p["pasos"].toArray()[0].toObject()["t_inicio"].toString() == ts(2),
              "pasos de progreso.csv");
    comprobar(p["metricas"].toArray().size() == 3, "metricas por paso (pares con nombre)");
    const QJsonArray serie = p["serie"].toArray();
    comprobar(serie.size() == 9, QString("serie: 9 puntos de la rejilla (%1)").arg(serie.size()));
    const QJsonObject s3 = serie[3].toObject();
    comprobar(s3["t"].toString() == ts(3) && cerca(s3["t_rel_s"].toDouble(), 3) && s3["en_entrenamiento"].toBool()
                  && cerca(s3["cpu_proceso_pct"].toDouble(), 12.5) && cerca(s3["potencia_w"].toDouble(), 40.0),
              "punto 3 con ambos medidores y dentro del entrenamiento");
    comprobar(serie[0].toObject()["cpu_proceso_pct"].isNull() && serie[7].toObject()["potencia_w"].isNull()
                  && serie[7].toObject()["timestamp_trama"].isNull(),
              "columnas sin dato van como null");
    QStringList claves = serie[0].toObject().keys();
    claves.sort();
    comprobar(claves.join(',') == "corriente_a,cpu_proceso_pct,cpu_sistema_pct,en_entrenamiento,fase,paso_desde,paso_hasta,"
                                  "potencia_w,ram_proceso_mb,ram_sistema_pct,t,t_rel_s,timestamp_trama,voltaje_v",
              "claves de la serie = columnas de serie_unida");
    // Fase y pasos (cada fila es el intervalo (t - 1 s, t]): base_pre en (0,2], entrenamiento en
    // (2,6]; sin fase_posterior_ms la fase posterior no empezo (7 y 8 sin fase). Pasos 0 [2,4] y
    // 1 [4,6]: el 2 toca el paso 0 (empieza en t), el 4 toca los dos, el 7 ninguno.
    QStringList fasesSerie, pasosSerie;
    for (const QJsonValue& v : serie) {
        const QJsonObject o = v.toObject();
        fasesSerie << (o["fase"].isNull() ? "null" : o["fase"].toString());
        pasosSerie << (o["paso_desde"].isNull() ? "null" : QString("%1-%2").arg(o["paso_desde"].toInt()).arg(o["paso_hasta"].toInt()));
    }
    comprobar(fasesSerie.join(',') == "null,base_pre,base_pre,entrenamiento,entrenamiento,entrenamiento,entrenamiento,null,null",
              "fase de cada punto: " + fasesSerie.join(','));
    comprobar(pasosSerie.join(',') == "null,null,0-0,0-0,0-1,1-1,1-1,null,null", "pasos de cada punto: " + pasosSerie.join(','));
    const QJsonObject sinDs = PaqueteCorrida::construir(dirA, "PC-PRUEBA", "", &error);
    comprobar(!sinDs.contains("dataset"), "sin huella no se envia dataset");
    const QString dirSinMeta = raiz.filePath("vacia");
    QDir().mkpath(dirSinMeta);
    PaqueteCorrida::construir(dirSinMeta, "PC", "", &error);
    comprobar(!error.isEmpty(), "carpeta sin metadata.json: error");

    std::printf("\n== 2. Serie de la base -> CSV identico al local\n");
    {
        const QString local = tmp.filePath("local.csv"), remoto = tmp.filePath("remoto.csv");
        int filas = 0;
        analisis::exportarRejilla(dirA, marcas(dirA), local, &filas, &error);
        // PostgREST devuelve timestamptz como "2026-10-01T10:00:03+00:00" y los numeros con su precision
        QJsonArray comoBase;
        for (const QJsonValue& v : serie) {
            QJsonObject o = v.toObject();
            o["t"] = QDateTime::fromString(o["t"].toString(), Qt::ISODateWithMs).toUTC().toString(Qt::ISODate).replace("Z", "+00:00");
            if (o["timestamp_trama"].isString())
                o["timestamp_trama"] = o["timestamp_trama"].toString().replace("Z", "+00:00");
            comoBase.append(o);
        }
        comprobar(analisis::escribirRejillaCsv(PaqueteCorrida::filasDesdeJson(comoBase), remoto, &error), "CSV desde la serie");
        comprobar(!leer(local).isEmpty() && leer(local) == leer(remoto), "el CSV exportado desde la base es identico al local");
    }

    std::printf("\n== 3. Cola de subidas\n");
    ServidorSupabaseFalso srv;
    {
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s1/sesion.json")));
        ColaSubidas cola(opcionesCola(tmp, "s1"), &sesion);
        int subidas = 0;
        QObject::connect(&cola, &ColaSubidas::corridaSubida, [&](const QString&, qint64) { ++subidas; });

        cola.encolar(dirA);
        cola.encolar(dirA);  // idempotente
        comprobar(cola.elementos().size() == 1, "encolar dos veces la misma carpeta: un elemento");
        comprobar(cola.estadoDe(dirA)["estado"].toString() == "pendiente" && cola.pendientes() == 1,
                  "sin sesion queda pendiente");
        comprobar(cola.estadoDe(dirA)["usuario_id"].toString().isEmpty(), "sin cuenta: sin dueño todavía");

        comprobar(cola.estadoDe(dirA)["propietario"].toString() == "sin_cuenta" && cola.sinCuenta() == 1,
                  "terminada sin cuenta: propietario sin_cuenta");

        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        esperar([&] { return cola.estadoDe(dirA)["estado"].toString() != "pendiente"; }, 800);
        comprobar(cola.estadoDe(dirA)["estado"].toString() == "pendiente" && srv.contar("/rest/v1/rpc/subir_corrida") == 0,
                  "al iniciar sesion NO se sube sola con la cuenta nueva (falla 3)");
        cola.reclamarSinCuenta();
        esperar([&] { return cola.estadoDe(dirA)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dirA)["estado"].toString() == "subida", "al reclamarla se sube");
        comprobar(cola.sinCuenta() == 0, "ya no quedan corridas sin cuenta");
        comprobar(cola.estadoDe(dirA)["id_remoto"].toLongLong() == 1 && subidas == 1, "id de la corrida en la base");
        comprobar(cola.estadoDe(dirA)["usuario_id"].toString() == "u-ana", "queda a nombre de quien la subio");
        comprobar(srv.series.value(1).size() == 9, "la base recibio la serie completa");
        // Migracion 006: el detalle solo entra por subir_corrida (una transaccion). La app no lo
        // inserta por REST, y un insert directo en una corrida ya subida se rechaza
        int insertsDirectos = 0;
        for (const char* t : {"/rest/v1/paso", "/rest/v1/metrica_paso", "/rest/v1/serie_unida", "/rest/v1/hiperparametro"})
            for (const auto& pet : srv.peticiones) insertsDirectos += (pet.ruta == t && pet.metodo == "POST");
        comprobar(insertsDirectos == 0, "la subida no inserta el detalle por REST: todo va en subir_corrida");
        for (const QString tabla : {"paso", "metrica_paso", "serie_unida", "hiperparametro"}) {
            ClienteSupabase::Resultado res;
            bool listo = false;
            sesion.rest("POST", tabla, {}, QJsonDocument(QJsonObject{{"corrida_id", 1}, {"step_index", 99}}).toJson(QJsonDocument::Compact),
                        [&](const ClienteSupabase::Resultado& r) { res = r; listo = true; });
            esperar([&] { return listo; });
            comprobar(res.http == 403 && res.cuerpo.object()["code"].toString() == "42501",
                      QString("insert directo en %1 de una corrida ya subida: 403 / 42501 (%2)").arg(tabla, res.error));
        }
        {
            ClienteSupabase::Resultado res;
            bool listo = false;
            sesion.rest("POST", "corrida", {}, R"({"equipo": "PC", "subida_en": "2026-10-01T10:00:00Z"})",
                        [&](const ClienteSupabase::Resultado& r) { res = r; listo = true; });
            esperar([&] { return listo; });
            comprobar(res.http == 403 && res.cuerpo.object()["code"].toString() == "42501",
                      "insert directo en corrida fijando subida_en: permiso denegado (" + res.error + ")");
        }
        comprobar(QFile::exists(tmp.filePath("s1/huellas.json")), "huella del dataset guardada para no recalcularla");
        comprobar(cola.pendientes() == 0, "sin pendientes");

        // Otra instalacion sube la misma corrida: la base responde 409
        ColaSubidas otra(opcionesCola(tmp, "s1b"), &sesion);
        otra.encolar(dirA);
        esperar([&] { return otra.estadoDe(dirA)["estado"].toString() != "subiendo" && otra.estadoDe(dirA)["estado"].toString() != "pendiente"; });
        comprobar(otra.estadoDe(dirA)["estado"].toString() == "subida" && otra.estadoDe(dirA)["id_remoto"].toLongLong() == 1,
                  "409 comprobado en la base (equipo + t_creacion): ya estaba subida, con su id");
        comprobar(srv.corridas.size() == 1, "no se duplico en la base");

        // Servidor caido dos veces y luego bien: reintento con espera
        const QString dirB = crearCorrida(raiz, "exp_a", 2, rutaDatos);
        int caidas = 0;
        srv.manejador = [&](const ServidorSupabaseFalso::Peticion& pet) {
            ServidorSupabaseFalso::Respuesta r;
            r.http = 0;
            if (pet.ruta == "/rest/v1/rpc/subir_corrida" && caidas < 2) {
                ++caidas;
                r.http = 503;
                r.cuerpo = R"({"message": "servicio no disponible"})";
            }
            return r;
        };
        cola.encolar(dirB);
        esperar([&] { return caidas >= 1 && cola.estadoDe(dirB)["estado"].toString() == "pendiente"; }, 3000);
        comprobar(caidas >= 1 && cola.estadoDe(dirB)["ultimo_error"].toString().contains("503"),
                  "503: queda pendiente con el motivo (" + cola.estadoDe(dirB)["ultimo_error"].toString() + ")");
        esperar([&] { return cola.estadoDe(dirB)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dirB)["estado"].toString() == "subida" && caidas == 2, "se reintenta sola y se sube");
        comprobar(cola.estadoDe(dirB)["intentos"].toInt() == 3, "tres intentos");

        // Rechazo permanente (p. ej. un error de la funcion) y reintento manual
        const QString dirC = crearCorrida(raiz, "exp_a", 3, rutaDatos);
        bool rechazar = true;
        srv.manejador = [&](const ServidorSupabaseFalso::Peticion& pet) {
            ServidorSupabaseFalso::Respuesta r;
            r.http = 0;
            if (pet.ruta == "/rest/v1/rpc/subir_corrida" && rechazar) {
                r.http = 400;
                r.cuerpo = R"({"code": "22P02", "message": "invalid input syntax"})";
            }
            return r;
        };
        cola.encolar(dirC);
        esperar([&] { return cola.estadoDe(dirC)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(dirC)["estado"].toString() == "rechazada" && cola.rechazadas() == 1, "400: rechazada");
        rechazar = false;
        cola.reintentar(dirC);
        esperar([&] { return cola.estadoDe(dirC)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dirC)["estado"].toString() == "subida", "reintento manual: subida");
        srv.manejador = nullptr;
    }

    std::printf("\n== 4. Cola guardada en disco\n");
    {
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s1/sesion.json")));
        ColaSubidas cola(opcionesCola(tmp, "s1"), &sesion);
        comprobar(cola.elementos().size() == 3 && cola.estadoDe(dirA)["estado"].toString() == "subida",
                  "la cola se recupera al reiniciar la aplicacion");
    }

    std::printf("\n== 5. Historial remoto\n");
    {
        // Una corrida compartida de otro usuario, con una serie de 2500 puntos (3 paginas)
        QJsonArray larga;
        for (int i = 0; i < 2500; ++i)
            larga.append(QJsonObject{{"t", QDateTime::fromMSecsSinceEpoch(1790000000000LL + i * 1000LL, QTimeZone::UTC)
                                               .toString(Qt::ISODate).replace("Z", "+00:00")},
                                     {"t_rel_s", i}, {"en_entrenamiento", i > 10}, {"potencia_w", 50.0 + (i % 7)},
                                     {"cpu_proceso_pct", QJsonValue::Null}});
        srv.series[99] = larga;
        srv.corridas.append(QJsonObject{{"id", 99}, {"usuario_id", "u-beto"}, {"equipo", "PC-BETO"}, {"numero", 4},
                                        {"visibilidad", "compartida"}, {"estado", "completada"},
                                        {"experimento", QJsonObject{{"nombre", "mlp"}}}, {"perfil", QJsonObject{{"nombre", "Beto"}}}});

        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s1/sesion.json")));
        sesion.restaurarSesion();
        esperar([&] { return sesion.conectado(); });
        HistorialRemoto hr(&sesion);
        hr.recargar();
        comprobar(hr.estado() == "cargando", "cargando");
        esperar([&] { return hr.estado() != "cargando"; });
        comprobar(hr.estado() == "listo" && hr.corridas().size() == 4, QString("4 corridas visibles (%1)").arg(hr.corridas().size()));
        const QVariantMap propia = hr.corridas().value(0).toMap();
        const QVariantMap ajena = hr.corridas().value(3).toMap();
        comprobar(propia["propia"].toBool() && propia["experimento"].toString() == "exp_a" && propia["dueno"].toString() == "Ana",
                  "corrida propia con experimento y dueño");
        comprobar(!ajena["propia"].toBool() && ajena["dueno"].toString() == "Beto" && ajena["experimento"].toString() == "mlp",
                  "corrida compartida de otro usuario");
        const auto& consulta = srv.peticiones.last().consulta;
        comprobar(consulta.queryItemValue("select").contains("perfil(nombre)") && consulta.queryItemValue("order") == "subida_en.desc",
                  "consulta con el dueño embebido y la más reciente primero");

        QString errVis = "sin respuesta";
        QObject::connect(&hr, &HistorialRemoto::visibilidadCambiada, [&](qint64, const QString&, const QString& e) { errVis = e; });
        hr.cambiarVisibilidad(1, "compartida");
        esperar([&] { return errVis != "sin respuesta"; });
        comprobar(errVis.isEmpty() && hr.corridas().value(0).toMap()["visibilidad"].toString() == "compartida",
                  "la dueña comparte su corrida");
        comprobar(srv.peticiones.last().cabeceras.value("prefer") == "return=representation", "PATCH pide la fila actualizada");
        errVis = "sin respuesta";
        hr.cambiarVisibilidad(99, "privada");
        esperar([&] { return errVis != "sin respuesta"; });
        comprobar(errVis.contains("Solo quien subió"), "una corrida ajena no se puede cambiar: " + errVis);

        int filasExp = -1;
        QString errExp;
        QObject::connect(&hr, &HistorialRemoto::exportacionTerminada,
                         [&](qint64, const QString&, int filas, const QString& e) { filasExp = filas; errExp = e; });
        const QString destinoRemoto = tmp.filePath("remota_1.csv");
        hr.exportarCsv(1, destinoRemoto);
        comprobar(hr.exportando() == 1, "exportando la corrida 1");
        esperar([&] { return filasExp >= 0; });
        const QString destinoLocal = tmp.filePath("local_1.csv");
        analisis::exportarRejilla(dirA, marcas(dirA), destinoLocal, nullptr, &error);
        comprobar(errExp.isEmpty() && filasExp == 9 && leer(destinoRemoto) == leer(destinoLocal),
                  "exportar desde la base: mismo CSV que la exportación local " + errExp);
        filasExp = -1;
        const int antes = srv.contar("/rest/v1/serie_unida");
        hr.exportarCsv(99, tmp.filePath("remota_99.csv"));
        esperar([&] { return filasExp >= 0; });
        comprobar(errExp.isEmpty() && filasExp == 2500, QString("serie de 2500 puntos completa (%1) %2").arg(filasExp).arg(errExp));
        comprobar(srv.contar("/rest/v1/serie_unida") - antes == 3, "descargada en 3 páginas de 1000");
        const QList<QByteArray> lineas = leer(tmp.filePath("remota_99.csv")).split('\n');
        comprobar(lineas.size() == 2502 && lineas[1].startsWith("2026-09-21T") && lineas[1].contains(".000Z,0.000,0,"),
                  "marcas temporales en el formato de los medidores: " + QString::fromUtf8(lineas.value(1)));
        filasExp = -1;
        srv.series[2] = QJsonArray();
        hr.exportarCsv(2, tmp.filePath("vacia.csv"));
        esperar([&] { return filasExp >= 0; });
        comprobar(!errExp.isEmpty() && !QFile::exists(tmp.filePath("vacia.csv")), "sin serie: error y sin archivo (" + errExp + ")");

        // Detalle desde la base: las mismas estadisticas que el detalle local de la misma corrida
        hr.cargarDetalle(1);
        comprobar(hr.estadoDetalle() == "cargando", "cargando el detalle");
        esperar([&] { return hr.estadoDetalle() != "cargando"; });
        const QVariantMap rem = hr.detalle();
        const QVariantMap loc = Historial(tmp.path()).detalle(dirA);
        comprobar(hr.estadoDetalle() == "listo" && rem.value("dir").toString() == "base:1" && rem.value("origen").toString() == "base",
                  "detalle remoto listo " + hr.errorDetalle());
        bool iguales = true;
        QStringList distintas;
        for (const char* ventana : {"entrenamiento", "medicion"})
            for (const char* grupo : {"sistema", "energia"}) {
                const QVariantMap a = loc["ventanas"].toMap()[ventana].toMap()[grupo].toMap();
                const QVariantMap b = rem["ventanas"].toMap()[ventana].toMap()[grupo].toMap();
                for (auto it = a.begin(); it != a.end(); ++it) {
                    if (it.key() == "filas" || it.key() == "filas_con_trama") continue;  // conteo de filas del CSV
                    const QVariantMap ea = it.value().toMap(), eb = b.value(it.key()).toMap();
                    for (const char* k : {"n", "min", "max", "media"})
                        if (!cerca(ea.value(k).toDouble(), eb.value(k).toDouble())) {
                            iguales = false;
                            distintas << QString("%1.%2.%3.%4").arg(ventana, grupo, it.key(), k);
                        }
                }
            }
        comprobar(iguales, "ventanas: mismas estadisticas que el detalle local " + distintas.join(" "));
        comprobar(rem["estado"] == loc["estado"] && rem["hiperparametros"] == loc["hiperparametros"]
                      && rem["medidores"] == loc["medidores"] && rem["evaluacion"] == loc["evaluacion"]
                      && cerca(rem["t_antes_rel_s"].toDouble(), loc["t_antes_rel_s"].toDouble()),
                  "resumen (estado, hiperparametros, medidores, evaluacion, tiempos) igual al local");
        comprobar(rem["energia_experimento"] == loc["energia_experimento"] && rem["metodo_energia"] == loc["metodo_energia"],
                  "energia del experimento desde la copia de resultados.json");
        comprobar(rem["propia"].toBool() && rem["hardware"].toMap().value("ram_total_bytes").toLongLong() == 8000000000LL,
                  "propia y con el equipo (hardware)");
        comprobar(rem["serie_entrenamiento"].toMap()["potencia_w"].toList().size() == 5,
                  "serie del entrenamiento para comparar (5 puntos)");
        comprobar(rem["series_energia"].toMap()["potencia_w"].toList().size() == 8
                      && rem["series_sistema"].toMap()["cpu_proceso_pct"].toList().size() == 8,
                  "series para las graficas");

        // Comparacion
        hr.cargarComparacion({1});
        comprobar(hr.estadoComparacion() == "error", "comparar una sola corrida: error");
        hr.cargarComparacion({1, 99});
        esperar([&] { return hr.estadoComparacion() != "cargando"; });
        comprobar(hr.estadoComparacion() == "listo" && hr.comparacion().size() == 2
                      && hr.comparacion()[1].toMap()["id_remoto"].toLongLong() == 99,
                  "comparar 2 corridas, en el orden pedido " + hr.errorComparacion());
        comprobar(!hr.comparacion()[1].toMap()["propia"].toBool() && hr.comparacion()[1].toMap()["dueno"].toString() == "Beto",
                  "la corrida de otro usuario se compara con su dueño");

        // Borrar
        QString errBorrar = "sin respuesta";
        QObject::connect(&hr, &HistorialRemoto::corridaBorrada, [&](qint64, const QString& e) { errBorrar = e; });
        hr.borrar(99);
        esperar([&] { return errBorrar != "sin respuesta"; });
        comprobar(errBorrar.contains("Solo quien subió") && srv.corridas.size() == 4, "no se borra una corrida ajena");
        errBorrar = "sin respuesta";
        const int antesBorrar = static_cast<int>(hr.corridas().size());
        hr.borrar(1);
        esperar([&] { return errBorrar != "sin respuesta"; });
        comprobar(errBorrar.isEmpty() && static_cast<int>(hr.corridas().size()) == antesBorrar - 1 && !srv.series.contains(1),
                  "borrar una corrida propia (con su serie)");
        comprobar(hr.estadoDetalle() == "inactivo", "el detalle de la corrida borrada se descarta");

        sesion.cerrarSesion();
        comprobar(hr.corridas().isEmpty() && hr.estado() == "inactivo", "al cerrar sesión se vacía la lista");
    }


    std::printf("\n== 6. Identidad de la corrida, dueño y estado desde la base\n");
    {
        ServidorSupabaseFalso srv6;
        ClienteSupabase sesion(opcionesSesion(srv6.url(), tmp.filePath("s6/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        ColaSubidas cola(opcionesCola(tmp, "s6"), &sesion);

        // La carpeta corrida_001 se borro y el orquestador volvio a usar el numero 1
        const QDir raiz6(tmp.filePath("raiz6"));
        const QString primera = crearCorrida(raiz6, "exp_a", 1, rutaDatos, "2026-10-01T09:00:00.000Z");
        cola.encolar(primera);
        esperar([&] { return cola.estadoDe(primera)["estado"].toString() == "subida"; });
        const QDir raiz6b(tmp.filePath("raiz6b"));
        const QString reusada = crearCorrida(raiz6b, "exp_a", 1, rutaDatos, "2026-10-05T09:00:00.000Z");
        cola.encolar(reusada);
        esperar([&] { return cola.estadoDe(reusada)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(reusada)["estado"].toString() == "subida" && srv6.corridas.size() == 2,
                  "numero repetido con otro t_creacion: se sube como corrida nueva (falla 1)");
        comprobar(srv6.corridas.last().toObject()["t_creacion"].toString() == "2026-10-05T09:00:00.000Z",
                  "el paquete envia t_creacion");

        // Base sin la migracion 003: el 409 por numero no se da por bueno
        srv6.baseSin003 = true;
        const QDir raiz6c(tmp.filePath("raiz6c"));
        const QString choca = crearCorrida(raiz6c, "exp_a", 1, rutaDatos, "2026-10-09T09:00:00.000Z");
        cola.encolar(choca);
        esperar([&] { return cola.estadoDe(choca)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(choca)["estado"].toString() == "rechazada"
                      && cola.estadoDe(choca)["ultimo_error"].toString().contains("migración 003"),
                  "409 que no se puede comprobar: rechazada, no 'subida' (" + cola.estadoDe(choca)["ultimo_error"].toString() + ")");
        srv6.baseSin003 = false;

        // Corrida de otra cuenta en la cola: no se sube con esta sesion
        QFile f(tmp.filePath("s6/subidas.json"));
        comprobar(f.open(QIODevice::ReadOnly), "cola s6 en disco");
        QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        const QDir raiz6d(tmp.filePath("raiz6d"));
        const QString ajena = crearCorrida(raiz6d, "exp_a", 7, rutaDatos);
        QJsonArray lista = doc["corridas"].toArray();
        lista.append(QJsonObject{{"clave", QDir::cleanPath(QFileInfo(ajena).absoluteFilePath()).toLower()},
                                 {"dir", ajena}, {"estado", "pendiente"}, {"usuario_id", "u-beto"}, {"intentos", 0}});
        doc["corridas"] = lista;
        comprobar(f.open(QIODevice::WriteOnly), "escribir cola con una corrida ajena");
        f.write(QJsonDocument(doc).toJson());
        f.close();
        const int subidasAntes = srv6.contar("/rest/v1/rpc/subir_corrida");
        ColaSubidas cola2(opcionesCola(tmp, "s6"), &sesion);
        esperar([&] { return false; }, 600);
        comprobar(cola2.estadoDe(ajena)["propietario"].toString() == "otra" && srv6.contar("/rest/v1/rpc/subir_corrida") == subidasAntes,
                  "una corrida de otra cuenta no se sube con esta sesion");
        cola2.reclamarSinCuenta();
        esperar([&] { return false; }, 300);
        comprobar(cola2.estadoDe(ajena)["estado"].toString() == "pendiente", "reclamar sin cuenta no toca las de otra cuenta");

        // Estado desde la lista de la base: una carpeta que no esta en la cola
        const QDir raiz6e(tmp.filePath("raiz6e"));
        const QString fuera = crearCorrida(raiz6e, "exp_a", 8, rutaDatos, "2026-10-12T09:00:00.000Z");
        comprobar(cola2.estadoDe(fuera)["estado"].toString() == "no_encolada", "fuera de la cola y sin lista: no_encolada");
        cola2.sincronizarRemotas({QVariantMap{{"id", 42}, {"equipo", "PC-PRUEBA"}, {"t_creacion", "2026-10-12T09:00:00+00:00"}},
                                  QVariantMap{{"id", 43}, {"equipo", "OTRO-PC"}, {"t_creacion", "2026-10-01T09:57:50+00:00"}}});
        comprobar(cola2.estadoDe(fuera)["estado"].toString() == "subida" && cola2.estadoDe(fuera)["id_remoto"].toLongLong() == 42
                      && cola2.estadoDe(fuera)["origen"].toString() == "base",
                  "la lista de la base marca la carpeta como subida (mismo equipo y t_creacion)");
        comprobar(cola2.estadoDe(ajena)["estado"].toString() == "pendiente", "otro equipo con la misma fecha no cuenta");
        cola2.olvidarRemota(42);
        comprobar(cola2.estadoDe(fuera)["estado"].toString() == "no_encolada", "borrada de la base: vuelve a 'solo local'");
        const qint64 idPrimera = cola2.estadoDe(primera)["id_remoto"].toLongLong();
        cola2.olvidarRemota(idPrimera);
        comprobar(cola2.estadoDe(primera)["estado"].toString() == "no_encolada", "la entrada subida de la cola tambien se olvida");
    }

    std::printf("\n== 8. Modo de subida, esperando decision y nota (D-012)\n");
    {
        ServidorSupabaseFalso srv8;
        ClienteSupabase sesion(opcionesSesion(srv8.url(), tmp.filePath("s8/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        ColaSubidas::Opciones op = opcionesCola(tmp, "s8");
        op.rutaPreferencias = tmp.filePath("s8/preferencias.json");
        escribir(op.rutaPreferencias, R"({"linea_base_s": 30})");
        const QDir raiz8(tmp.filePath("raiz8"));
        auto conAuditoria = [&](int n, const QString& veredicto) {
            const QString d = crearCorrida(raiz8, "exp_modo", n, rutaDatos, QString("2026-10-21T09:%1:00.000Z").arg(n, 2, 10, QChar('0')));
            if (!veredicto.isEmpty())
                escribir(d + "/auditoria.json", QString(R"({"version_reglas": 1, "veredicto": "%1", "comprobaciones": []})")
                                                    .arg(veredicto).toUtf8());
            return d;
        };
        QStringList pendientesDecision;
        {
            ColaSubidas cola(op, &sesion);
            QObject::connect(&cola, &ColaSubidas::decisionPendiente,
                             [&](const QString& dir, const QString&, const QString&) { pendientesDecision << dir; });
            comprobar(cola.modoSubida() == "subir_todo", "modo de subida por defecto: subir_todo");
            const QString d1 = conAuditoria(1, "con_advertencias");
            cola.encolar(d1);
            esperar([&] { return cola.estadoDe(d1)["estado"].toString() == "subida"; });
            comprobar(cola.estadoDe(d1)["estado"].toString() == "subida", "subir_todo: una corrida con advertencias se sube sola");

            cola.setModoSubida("preguntar_si_hay_problemas");
            QFile pf(op.rutaPreferencias);
            pf.open(QIODevice::ReadOnly);
            const QJsonObject pref = QJsonDocument::fromJson(pf.readAll()).object();
            pf.close();  // en Windows, un archivo abierto no deja reemplazarlo al guardar
            comprobar(pref["modo_subida"].toString() == "preguntar_si_hay_problemas" && pref["linea_base_s"].toInt() == 30,
                      "el modo se guarda en preferencias.json y conserva la linea base");
            const QString d2 = conAuditoria(2, "valida"), d3 = conAuditoria(3, "con_advertencias"), d4 = conAuditoria(4, "");
            cola.encolar(d2);
            cola.encolar(d3);
            cola.encolar(d4);
            esperar([&] { return cola.estadoDe(d2)["estado"].toString() == "subida"; });
            esperar([&] { return false; }, 300);
            comprobar(cola.estadoDe(d2)["estado"].toString() == "subida", "preguntar si hay problemas: la valida se sube sola");
            comprobar(cola.estadoDe(d3)["estado"].toString() == "esperando_decision"
                          && cola.estadoDe(d4)["estado"].toString() == "esperando_decision" && cola.esperandoDecision() == 2,
                      "con advertencias y sin auditoria: esperando decision (2)");
            comprobar(pendientesDecision.size() == 2 && srv8.corridas.size() == 2, "aviso de decision pendiente y nada subido sin decidir");
            comprobar(cola.estadoDe(d3)["veredicto"].toString() == "con_advertencias", "la entrada de la cola lleva el veredicto");
            cola.reintentarTodo();
            esperar([&] { return false; }, 300);
            comprobar(cola.estadoDe(d3)["estado"].toString() == "esperando_decision", "Reintentar ahora no sube las que esperan decision");

            // Subir con nota: la nota se guarda en nota.txt y viaja en el paquete
            Historial h(raiz8.path());
            comprobar(h.guardarNota(d3, QString::fromUtf8("Ventilador al máximo; navegador abierto")).isEmpty()
                          && h.nota(d3) == QString::fromUtf8("Ventilador al máximo; navegador abierto"),
                      "la nota de condiciones se guarda en nota.txt");
            cola.decidir(d3, true);
            esperar([&] { return cola.estadoDe(d3)["estado"].toString() == "subida"; });
            const QJsonObject subida3 = srv8.corridas.last().toObject();
            comprobar(cola.estadoDe(d3)["estado"].toString() == "subida" && subida3["veredicto"].toString() == "con_advertencias"
                          && subida3["auditoria"].toObject()["version_reglas"].toInt() == 1
                          && subida3["nota"].toString() == QString::fromUtf8("Ventilador al máximo; navegador abierto"),
                      "decidir 'subir': se sube con el veredicto, auditoria.json integro y la nota");

            // No subir: vuelve a "Solo local"; despues se puede subir a mano (sin preguntar)
            cola.decidir(d4, false);
            comprobar(cola.estadoDe(d4)["estado"].toString() == "no_encolada", "decidir 'no subir': Solo local");
            // Corrida activa: la cola se pausa (no audita, no arma paquetes ni envia, ni a mano ni por
            // el reintento automatico) y se reanuda sola al terminar la corrida
            const int subidasAntes = static_cast<int>(srv8.corridas.size());
            cola.setCorridaActiva(true);
            cola.reintentar(d4);
            cola.procesar();
            esperar([&] { return false; }, 300);
            comprobar(cola.estadoDe(d4)["estado"].toString() == "pendiente" && !QFile::exists(d4 + "/auditoria.json")
                          && srv8.corridas.size() == subidasAntes,
                      "con una corrida activa la cola se pausa: la corrida queda pendiente, sin auditar ni enviar");
            cola.setCorridaActiva(false);
            esperar([&] { return cola.estadoDe(d4)["estado"].toString() == "subida"; });
            comprobar(cola.estadoDe(d4)["estado"].toString() == "subida", "subir a mano despues de 'no subir': se sube sin preguntar");
            comprobar(QFile::exists(d4 + "/auditoria.json") && srv8.corridas.last().toObject()["veredicto"].isString(),
                      "la corrida sin auditoria.json se audita antes de subirla y llega con veredicto");

            cola.setModoSubida("preguntar_siempre");
            const QString d5 = conAuditoria(5, "valida");
            cola.encolar(d5);
            esperar([&] { return false; }, 300);
            comprobar(cola.estadoDe(d5)["estado"].toString() == "esperando_decision", "preguntar siempre: hasta una valida espera");
            cola.setModoSubida("otro");
            comprobar(cola.modoSubida() == "preguntar_siempre", "un modo desconocido se ignora");
        }
        {
            ColaSubidas cola(op, &sesion);
            esperar([&] { return false; }, 400);
            const QString d5 = raiz8.filePath("corridas/exp_modo/corrida_005");
            comprobar(cola.modoSubida() == "preguntar_siempre" && cola.estadoDe(d5)["estado"].toString() == "esperando_decision"
                          && srv8.corridas.size() == 4,
                      QString("al reiniciar: el modo se lee de preferencias.json y la corrida sigue esperando (no se sube sola): %1, %2, %3")
                          .arg(cola.modoSubida(), cola.estadoDe(d5)["estado"].toString()).arg(srv8.corridas.size()));
        }

        // Nota editable despues de subida, solo por el dueno
        HistorialRemoto hr(&sesion);
        QString errNota = "sin respuesta";
        QObject::connect(&hr, &HistorialRemoto::notaCambiada, [&](qint64, const QString&, const QString& e) { errNota = e; });
        hr.cambiarNota(1, "Nota nueva");
        esperar([&] { return errNota != "sin respuesta"; });
        comprobar(errNota.isEmpty() && srv8.corridas[0].toObject()["nota"].toString() == "Nota nueva"
                      && srv8.peticiones.last().cabeceras.value("prefer") == "return=representation",
                  "la dueña cambia la nota de una corrida subida (PATCH corrida.nota)");
        srv8.corridas.append(QJsonObject{{"id", 50}, {"usuario_id", "u-beto"}, {"visibilidad", "compartida"}});
        errNota = "sin respuesta";
        hr.cambiarNota(50, "No es mia");
        esperar([&] { return errNota != "sin respuesta"; });
        comprobar(errNota.contains("Solo quien subió"), "la nota de una corrida ajena no se puede cambiar: " + errNota);
        errNota = "sin respuesta";
        hr.cambiarNota(1, "");
        esperar([&] { return errNota != "sin respuesta"; });
        comprobar(errNota.isEmpty() && srv8.corridas[0].toObject()["nota"].isNull(), "nota vacia = null en la base");
    }

    std::printf("\n== 7. Marca 'En la base' de una corrida borrada por fuera de la aplicacion (D3)\n");
    {
        ServidorSupabaseFalso srv7;
        ClienteSupabase sesion(opcionesSesion(srv7.url(), tmp.filePath("s7/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        HistorialRemoto hr(&sesion);
        auto recargar = [&] {
            hr.recargar();
            esperar([&] { return hr.estado() != "cargando"; });
            esperar([&] { return false; }, 200);  // la cola procesa la lista (y la consulta por id, si hace falta)
        };

        const QDir raiz7(tmp.filePath("raiz7"));
        QStringList dirs;
        {
            ColaSubidas cola(opcionesCola(tmp, "s7"), &sesion);
            for (int n = 1; n <= 4; ++n) {
                dirs << crearCorrida(raiz7, "exp_d3", n, rutaDatos, QString("2026-10-20T09:0%1:00.000Z").arg(n));
                cola.encolar(dirs.last());
                esperar([&] { return cola.estadoDe(dirs.last())["estado"].toString() == "subida"; });
            }
        }
        comprobar(srv7.corridas.size() == 4, "cuatro corridas subidas");
        // Una corrida de otra cuenta de este equipo marcada como subida (su id no es visible para Ana)
        {
            QFile f(tmp.filePath("s7/subidas.json"));
            f.open(QIODevice::ReadOnly);
            QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            const QString ajena = crearCorrida(raiz7, "exp_d3", 9, rutaDatos);
            dirs << ajena;
            QJsonArray lista = doc["corridas"].toArray();
            lista.append(QJsonObject{{"clave", QDir::cleanPath(QFileInfo(ajena).absoluteFilePath()).toLower()},
                                     {"dir", ajena}, {"estado", "subida"}, {"usuario_id", "u-beto"}, {"id_remoto", 77}});
            doc["corridas"] = lista;
            f.open(QIODevice::WriteOnly);
            f.write(QJsonDocument(doc).toJson());
        }
        ColaSubidas cola7(opcionesCola(tmp, "s7"), &sesion);
        QObject::connect(&hr, &HistorialRemoto::listaCargada, &cola7, &ColaSubidas::depurarSubidas);
        auto estado = [&](int i) { return cola7.estadoDe(dirs[i])["estado"].toString(); };

        // La corrida 1 se borra desde el panel de Supabase (por fuera de la aplicacion)
        srv7.corridas.removeAt(0);
        recargar();
        comprobar(hr.estado() == "listo" && estado(0) == "no_encolada",
                  "lista completa sin la corrida 1: su carpeta vuelve a 'Solo local'");
        comprobar(estado(1) == "subida" && estado(2) == "subida" && estado(3) == "subida",
                  "las que siguen en la base conservan 'En la base'");
        comprobar(estado(4) == "subida", "una corrida de otra cuenta no se toca (no es de la cuenta con sesion)");

        // Si la lista falla, no se concluye nada
        srv7.corridas.removeAt(0);  // la corrida 2
        srv7.manejador = [](const ServidorSupabaseFalso::Peticion& pet) {
            ServidorSupabaseFalso::Respuesta r;
            r.http = (pet.ruta == "/rest/v1/corrida" && pet.metodo == "GET") ? 503 : 0;
            return r;
        };
        recargar();
        comprobar(hr.estado() == "error" && estado(1) == "subida", "lista con error: la marca se conserva");
        srv7.manejador = nullptr;

        // Lista que trae el maximo de corridas: antes de concluir, se piden por id las que faltan
        for (int i = 0; i < HistorialRemoto::LIMITE_LISTA; ++i)
            srv7.corridas.append(QJsonObject{{"id", 1000 + i}, {"usuario_id", "u-beto"}, {"equipo", "PC-BETO"}, {"numero", i},
                                             {"visibilidad", "compartida"}, {"estado", "completada"}});
        // La corrida 3 sigue en la base pero no vino en la lista (es "mas vieja" que las 500)
        const QJsonValue tercera = srv7.corridas.takeAt(0);
        const int consultasAntes = srv7.contar("/rest/v1/corrida");
        srv7.manejador = [&](const ServidorSupabaseFalso::Peticion& pet) {
            ServidorSupabaseFalso::Respuesta r;
            r.http = 0;  // el resto de las peticiones, como siempre
            if (pet.ruta == "/rest/v1/corrida" && pet.metodo == "GET"
                && pet.consulta.queryItemValue("id", QUrl::FullyDecoded).startsWith("in.(")) {
                // La consulta por id si la encuentra (el servidor falso devuelve lo que tiene)
                QJsonArray hay{tercera};
                r.http = 200;
                r.cuerpo = QJsonDocument(hay).toJson(QJsonDocument::Compact);
            }
            return r;
        };
        recargar();
        comprobar(srv7.contar("/rest/v1/corrida") - consultasAntes == 2,
                  "lista con el maximo de corridas: una consulta por id ademas de la lista");
        bool pidioPorId = false;
        for (const auto& pet : srv7.peticiones)
            pidioPorId = pidioPorId || pet.consulta.queryItemValue("id", QUrl::FullyDecoded).startsWith("in.(");
        comprobar(pidioPorId, "consulta id=in.(...) con las corridas que faltan");
        comprobar(estado(1) == "no_encolada", "la corrida 2 (borrada) vuelve a 'Solo local'");
        comprobar(estado(2) == "subida", "la corrida 3 (fuera de la lista pero en la base) conserva 'En la base'");
        comprobar(estado(3) == "subida", "la corrida 4 (en la lista) conserva 'En la base'");
        srv7.manejador = nullptr;
    }

    std::printf("\n== 9. Huella del dataset calculada al preparar los datos (metadata.datos.sha256)\n");
    {
        ServidorSupabaseFalso srv9;
        ClienteSupabase sesion(opcionesSesion(srv9.url(), tmp.filePath("s9/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        const QDir raiz9(tmp.filePath("raiz9"));
        // El CSV de la corrida ya no existe: con la huella en metadata.json se sube igual con su dataset
        const QString rutaBorrada = tmp.filePath("raiz9/ya_no_esta.csv");
        const QString d = crearCorrida(raiz9, "exp_huella", 1, rutaBorrada);
        const QString shaMeta = QString(64, 'a');
        {
            QFile f(d + "/metadata.json");
            f.open(QIODevice::ReadOnly);
            QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            QJsonObject datos = m["datos"].toObject();
            datos["sha256"] = shaMeta;
            datos["tam_bytes"] = 1234;
            datos["fecha_modificacion"] = "2026-10-01T08:00:00.000Z";
            m["datos"] = datos;
            escribir(d + "/metadata.json", QJsonDocument(m).toJson());
        }
        ColaSubidas cola(opcionesCola(tmp, "s9"), &sesion);
        cola.encolar(d);
        esperar([&] { return cola.estadoDe(d)["estado"].toString() == "subida"; });
        QJsonObject paquete;
        for (const auto& pet : srv9.peticiones)
            if (pet.ruta == "/rest/v1/rpc/subir_corrida") paquete = QJsonDocument::fromJson(pet.cuerpo).object()["p"].toObject();
        comprobar(cola.estadoDe(d)["estado"].toString() == "subida" && paquete["dataset"].toObject()["sha256"].toString() == shaMeta,
                  "corrida nueva: el dataset se sube con metadata.datos.sha256 aunque el CSV ya no exista");
        QFile hf(tmp.filePath("s9/huellas.json"));
        hf.open(QIODevice::ReadOnly);
        const QByteArray huellas = hf.readAll();
        comprobar(huellas.contains(shaMeta.toLatin1()) && huellas.contains("2026-10-01T08:00:00.000Z"),
                  "la huella de metadata queda en la cache de huellas (ruta, tamano y fecha)");
    }

    std::printf("\n== 10. Inferencia (D-014): paquete con tarea y migracion 008\n");
    {
        // 10a. Paquete: pasos y metricas con su tarea; columnas de la inferencia
        const QDir raiz10(tmp.filePath("raiz10"));
        const QString dInf = crearCorrida(raiz10, "exp_inf", 1, rutaDatos, "2026-10-26T09:01:00.000Z");
        conInferencia(dInf);
        QString err;
        const QJsonObject p = PaqueteCorrida::construir(dInf, "PC-PRUEBA", "", &err);
        QStringList tareas;
        for (const QJsonValue& v : p["pasos"].toArray())
            tareas << v.toObject()["tarea"].toString() + ":" + QString::number(v.toObject()["step_index"].toInt());
        comprobar(err.isEmpty() && tareas.join(',') == "entrenamiento:0,entrenamiento:1,inferencia:0,inferencia:1",
                  "paquete: pasos de progreso.csv con tarea entrenamiento y de progreso_inferencia.csv con inferencia ("
                      + tareas.join(',') + ")");
        int metricasInf = 0, metricasEnt = 0;
        for (const QJsonValue& v : p["metricas"].toArray()) {
            metricasInf += v.toObject()["tarea"] == "inferencia";
            metricasEnt += v.toObject()["tarea"] == "entrenamiento";
        }
        comprobar(metricasEnt == 3 && metricasInf == 4, QString("paquete: metricas por tarea (%1 de entrenamiento, %2 de inferencia)")
                                                            .arg(metricasEnt).arg(metricasInf));
        const QJsonObject c = p["corrida"].toObject();
        comprobar(c["inferencia_activada"] == QJsonValue(true) && c["inferencia_lote"].toInt() == 65536
                      && c["inferencia_duracion_min_s"].toInt() == 60 && c["t_inf_antes"].toString() == ts(6)
                      && c["t_inf_despues"].toString() == ts(7) && c["inferencia_pasadas"].toInteger() == 400000
                      && c["n_predicciones"].toInteger() == 400000,
                  "paquete: inferencia_activada, lote, duracion, marcas, pasadas y n_predicciones de metadata.json");
        comprobar(c["tiempo_por_prediccion_us"].toDouble() == 2.5 && cerca(c["energia_por_prediccion_j"].toDouble(), 0.0001)
                      && cerca(c["energia_inferencia_wh"].toDouble(), 0.0111111) && c["energia_base_media_wh"].toDouble() == 0.0,
                  "paquete: tiempo por prediccion de metadata; energia por prediccion y energias de resultados.json");
        const QJsonObject pViejo = PaqueteCorrida::construir(crearCorrida(raiz10, "exp_viejo", 1, rutaDatos), "PC-PRUEBA", "", &err);
        const QJsonObject cViejo = pViejo["corrida"].toObject();
        comprobar(cViejo["inferencia_activada"] == QJsonValue(false) && cViejo["n_predicciones"].isNull()
                      && cViejo["tiempo_por_prediccion_us"].isNull() && cViejo["energia_inferencia_wh"].isNull()
                      && pViejo["pasos"].toArray().first().toObject()["tarea"] == "entrenamiento",
                  "corrida sin inferencia: inferencia_activada false, columnas de la inferencia null y pasos de entrenamiento");

        // 10b. Base sin la migracion 008: la corrida con inferencia queda rechazada (una sola
        // comprobacion por sesion); la corrida sin inferencia se sube igual
        ServidorSupabaseFalso srv10;
        srv10.baseSin008 = true;
        ClienteSupabase sesion(opcionesSesion(srv10.url(), tmp.filePath("s10/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        auto comprobaciones008 = [&] {
            int n = 0;
            for (const auto& pet : srv10.peticiones)
                n += pet.ruta == "/rest/v1/corrida" && pet.consulta.queryItemValue("select") == "inferencia_activada";
            return n;
        };
        ColaSubidas cola(opcionesCola(tmp, "s10"), &sesion);
        cola.encolar(dInf);
        esperar([&] { return cola.estadoDe(dInf)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(dInf)["estado"].toString() == "rechazada"
                      && cola.estadoDe(dInf)["ultimo_error"].toString() == ColaSubidas::MOTIVO_SIN_008 && srv10.corridas.isEmpty(),
                  "sin la 008: la corrida con inferencia queda rechazada ('" + cola.estadoDe(dInf)["ultimo_error"].toString()
                      + "') sin llamar a subir_corrida");
        const QString dSin = crearCorrida(raiz10, "exp_sin_inf", 1, rutaDatos, "2026-10-26T09:02:00.000Z");
        cola.encolar(dSin);
        esperar([&] { return cola.estadoDe(dSin)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dSin)["estado"].toString() == "subida", "sin la 008: una corrida sin inferencia se sube como antes");
        const QString dInf2 = crearCorrida(raiz10, "exp_inf", 2, rutaDatos, "2026-10-26T09:03:00.000Z");
        conInferencia(dInf2);
        cola.encolar(dInf2);
        esperar([&] { return cola.estadoDe(dInf2)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(dInf2)["estado"].toString() == "rechazada" && comprobaciones008() == 1,
                  QString("la comprobacion de la 008 se hace una vez por sesion (%1 consulta)").arg(comprobaciones008()));

        // La lista de Base de datos avisa que falta la 008
        HistorialRemoto hr(&sesion);
        QObject::connect(&hr, &HistorialRemoto::migracion008, &cola, &ColaSubidas::setMigracion008);
        hr.recargar();
        esperar([&] { return hr.estado() != "cargando"; });
        comprobar(hr.estado() == "error" && hr.error().startsWith("Falta aplicar la migración 008"),
                  "la lista de Base de datos avisa: " + hr.error().left(50));

        // Se aplica la 008: al recargar la lista, las rechazadas por ese motivo vuelven solas a
        // pendiente y se suben
        srv10.baseSin008 = false;
        hr.recargar();
        esperar([&] { return cola.estadoDe(dInf)["estado"].toString() == "subida" && cola.estadoDe(dInf2)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dInf)["estado"].toString() == "subida" && cola.estadoDe(dInf2)["estado"].toString() == "subida",
                  "con la 008 aplicada, al recargar la lista las dos corridas con inferencia vuelven a pendiente y se suben");
        QJsonObject subida;
        for (const QJsonValue& v : srv10.corridas)
            if (v.toObject()["t_creacion"].toString() == "2026-10-26T09:01:00.000Z") subida = v.toObject();
        comprobar(subida["n_predicciones"].toInteger() == 400000 && subida["inferencia_activada"].toBool(),
                  "la corrida subida trae sus columnas de la inferencia");
    }
    {
        // 10c. La comprobacion no responde (503): no se concluye nada, la corrida sigue pendiente
        ServidorSupabaseFalso srv;
        srv.manejador = [](const ServidorSupabaseFalso::Peticion& pet) {
            ServidorSupabaseFalso::Respuesta r;
            if (pet.ruta == "/rest/v1/corrida" && pet.consulta.queryItemValue("select") == "inferencia_activada") r.http = 503;
            return r;
        };
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s10c/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        ColaSubidas cola(opcionesCola(tmp, "s10c"), &sesion);
        const QString d = crearCorrida(QDir(tmp.filePath("raiz10c")), "exp_inf", 1, rutaDatos, "2026-10-26T09:04:00.000Z");
        conInferencia(d);
        cola.encolar(d);
        esperar([&] { return srv.peticiones.size() > 3 && cola.estadoDe(d)["estado"].toString() == "pendiente"; }, 3000);
        comprobar(cola.estadoDe(d)["estado"].toString() == "pendiente"
                      && cola.estadoDe(d)["ultimo_error"].toString() != ColaSubidas::MOTIVO_SIN_008 && srv.corridas.isEmpty(),
                  "sin respuesta de la comprobacion: la corrida sigue pendiente (no se rechaza ni se sube)");
    }
    {
        // 10d. Al iniciar sesion se vuelve a comprobar: si la 008 ya esta, las rechazadas vuelven solas
        ServidorSupabaseFalso srv;
        srv.baseSin008 = true;
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s10d/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        ColaSubidas cola(opcionesCola(tmp, "s10d"), &sesion);
        const QString d = crearCorrida(QDir(tmp.filePath("raiz10d")), "exp_inf", 1, rutaDatos, "2026-10-26T09:05:00.000Z");
        conInferencia(d);
        cola.encolar(d);
        esperar([&] { return cola.estadoDe(d)["estado"].toString() == "rechazada"; });
        srv.baseSin008 = false;
        sesion.cerrarSesion();
        esperar([&] { return !sesion.conectado(); }, 2000);
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return cola.estadoDe(d)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(d)["estado"].toString() == "subida",
                  "al iniciar sesion con la 008 aplicada, la corrida rechazada por su falta vuelve a pendiente y se sube");
    }

    std::printf("\n== 11. Metricas del modelo (D-015): paquete y migracion 009\n");
    // metricas_modelo.json de una corrida; con fallaEntrenamiento, predecir fallo en ese conjunto
    auto conMetricas = [](const QString& d, bool fallaEntrenamiento = false) {
        const QJsonObject val{{"status_dll", 0}, {"motivo", QJsonValue::Null}, {"exactitud", 0.75},
                              {"matriz_confusion", QJsonArray{QJsonArray{2, 1}, QJsonArray{0, 1}}},
                              {"macro", QJsonObject{{"precision", 0.75}, {"recall", 0.8}, {"f1", 0.7}}},
                              {"ponderado", QJsonObject{{"precision", 0.8}, {"recall", 0.75}, {"f1", 0.72}}}};
        const QJsonObject ent = fallaEntrenamiento
            ? QJsonObject{{"status_dll", 3}, {"motivo", "predecir devolvio: error interno de la DLL"}, {"exactitud", QJsonValue::Null},
                          {"macro", QJsonValue::Null}}
            : QJsonObject{{"status_dll", 0}, {"exactitud", 0.9}, {"macro", QJsonObject{{"f1", 0.88}}}};
        escribir(d + "/metricas_modelo.json",
                 QJsonDocument(QJsonObject{{"version_formato", 1}, {"clases_originales", QJsonArray{0, 1}},
                                           {"validacion", val}, {"entrenamiento", ent}}).toJson());
    };
    const QDir raiz11(tmp.filePath("raiz11"));
    {
        // 11a. Paquete: copia integra y resumen en columnas
        const QString d = crearCorrida(raiz11, "exp_met", 1, rutaDatos, "2026-10-28T09:01:00.000Z");
        conMetricas(d);
        QString err;
        const QJsonObject c = PaqueteCorrida::construir(d, "PC-PRUEBA", "", &err)["corrida"].toObject();
        comprobar(err.isEmpty() && c["metricas_modelo"].toObject()["validacion"].toObject()["matriz_confusion"].toArray().size() == 2
                      && c["f1_macro_val"].toDouble() == 0.7 && c["f1_ponderado_val"].toDouble() == 0.72
                      && c["precision_macro_val"].toDouble() == 0.75 && c["recall_macro_val"].toDouble() == 0.8
                      && c["accuracy_entrenamiento"].toDouble() == 0.9 && c["f1_macro_entrenamiento"].toDouble() == 0.88,
                  "paquete: copia de metricas_modelo.json y las seis columnas de resumen");
        const QString dFalla = crearCorrida(raiz11, "exp_met", 2, rutaDatos, "2026-10-28T09:02:00.000Z");
        conMetricas(dFalla, true);
        const QJsonObject cf = PaqueteCorrida::construir(dFalla, "PC-PRUEBA", "", &err)["corrida"].toObject();
        comprobar(cf["accuracy_entrenamiento"].isNull() && cf["f1_macro_entrenamiento"].isNull() && cf["f1_macro_val"].toDouble() == 0.7
                      && cf["metricas_modelo"].toObject()["entrenamiento"].toObject()["status_dll"].toInt() == 3,
                  "paquete: el conjunto en que predecir fallo va en null; la copia lleva el detalle");
        const QJsonObject cs = PaqueteCorrida::construir(crearCorrida(raiz11, "exp_sin_met", 1, rutaDatos), "PC-PRUEBA", "", &err)["corrida"].toObject();
        comprobar(cs["metricas_modelo"].isNull() && cs["f1_macro_val"].isNull() && cs["accuracy_entrenamiento"].isNull(),
                  "corrida sin metricas_modelo.json: metricas_modelo y sus columnas en null");
    }
    {
        // 11b. Base sin la 009: la corrida con metricas queda rechazada (una comprobacion por sesion);
        // la corrida sin metricas se sube igual; la lista lo avisa y al aplicarla se suben solas
        ServidorSupabaseFalso srv;
        srv.baseSin009 = true;
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s11/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        auto comprobaciones009 = [&] {
            int n = 0;
            for (const auto& pet : srv.peticiones)
                n += pet.ruta == "/rest/v1/corrida" && pet.consulta.queryItemValue("select") == "metricas_modelo";
            return n;
        };
        ColaSubidas cola(opcionesCola(tmp, "s11"), &sesion);
        const QString d1 = raiz11.filePath("corridas/exp_met/corrida_001");
        cola.encolar(d1);
        esperar([&] { return cola.estadoDe(d1)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(d1)["ultimo_error"].toString() == ColaSubidas::MOTIVO_SIN_009 && srv.corridas.isEmpty(),
                  "sin la 009: la corrida con metricas queda rechazada ('" + cola.estadoDe(d1)["ultimo_error"].toString()
                      + "') sin llamar a subir_corrida");
        const QString dSin = raiz11.filePath("corridas/exp_sin_met/corrida_001");
        cola.encolar(dSin);
        esperar([&] { return cola.estadoDe(dSin)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(dSin)["estado"].toString() == "subida", "sin la 009: una corrida sin metricas se sube como antes");
        const QString d2 = raiz11.filePath("corridas/exp_met/corrida_002");
        cola.encolar(d2);
        esperar([&] { return cola.estadoDe(d2)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(d2)["estado"].toString() == "rechazada" && comprobaciones009() == 1,
                  QString("la comprobacion de la 009 se hace una vez por sesion (%1 consulta)").arg(comprobaciones009()));

        HistorialRemoto hr(&sesion);
        QObject::connect(&hr, &HistorialRemoto::migracion008, &cola, &ColaSubidas::setMigracion008);
        QObject::connect(&hr, &HistorialRemoto::migracion009, &cola, &ColaSubidas::setMigracion009);
        hr.recargar();
        esperar([&] { return hr.estado() != "cargando"; });
        comprobar(hr.estado() == "error" && hr.error().startsWith("Falta aplicar la migración 009"),
                  "la lista de Base de datos avisa: " + hr.error().left(50));

        srv.baseSin009 = false;
        hr.recargar();
        esperar([&] { return cola.estadoDe(d1)["estado"].toString() == "subida" && cola.estadoDe(d2)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(d1)["estado"].toString() == "subida" && cola.estadoDe(d2)["estado"].toString() == "subida",
                  "con la 009 aplicada, al recargar la lista las corridas con metricas vuelven a pendiente y se suben");
        QJsonObject subida;
        for (const QJsonValue& v : srv.corridas)
            if (v.toObject()["t_creacion"].toString() == "2026-10-28T09:01:00.000Z") subida = v.toObject();
        comprobar(subida["f1_macro_val"].toDouble() == 0.7 && subida["metricas_modelo"].isObject(),
                  "la corrida subida trae metricas_modelo y sus columnas");

        // Detalle remoto: la copia de la base, como el detalle local
        const QVariantMap det = HistorialRemoto::construirDetalle(subida, QJsonArray(), "u-ana");
        comprobar(det.value("metricas_modelo").toMap().value("validacion").toMap().value("exactitud").toDouble() == 0.75
                      && det.value("col_f1_macro_val").toDouble() == 0.7 && det.value("col_accuracy_entrenamiento").toDouble() == 0.9,
                  "detalle remoto: metricas_modelo desde la copia de la base y columnas para Comparar");
    }
    {
        // 11c. Corrida con inferencia y metricas, base sin la 008 ni la 009: primero espera la 008 y,
        // aplicada esa, la 009; al iniciar sesion con las dos, se sube
        ServidorSupabaseFalso srv;
        srv.baseSin008 = true;
        srv.baseSin009 = true;
        ClienteSupabase sesion(opcionesSesion(srv.url(), tmp.filePath("s11c/sesion.json")));
        sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        esperar([&] { return sesion.conectado(); });
        ColaSubidas cola(opcionesCola(tmp, "s11c"), &sesion);
        const QString d = crearCorrida(QDir(tmp.filePath("raiz11c")), "exp_inf_met", 1, rutaDatos, "2026-10-28T09:05:00.000Z");
        conInferencia(d);
        conMetricas(d);
        cola.encolar(d);
        esperar([&] { return cola.estadoDe(d)["estado"].toString() == "rechazada"; });
        comprobar(cola.estadoDe(d)["ultimo_error"].toString() == ColaSubidas::MOTIVO_SIN_008, "sin la 008 ni la 009: espera primero la 008");
        auto reiniciarSesion = [&] {
            sesion.cerrarSesion();
            esperar([&] { return !sesion.conectado(); }, 2000);
            sesion.iniciarSesion("ana@ejemplo.com", "secreta1");
        };
        srv.baseSin008 = false;
        reiniciarSesion();
        esperar([&] { return cola.estadoDe(d)["ultimo_error"].toString() == ColaSubidas::MOTIVO_SIN_009; });
        comprobar(cola.estadoDe(d)["estado"].toString() == "rechazada"
                      && cola.estadoDe(d)["ultimo_error"].toString() == ColaSubidas::MOTIVO_SIN_009 && srv.corridas.isEmpty(),
                  "con la 008 aplicada, la corrida pasa a esperar la 009");
        srv.baseSin009 = false;
        reiniciarSesion();
        esperar([&] { return cola.estadoDe(d)["estado"].toString() == "subida"; });
        comprobar(cola.estadoDe(d)["estado"].toString() == "subida",
                  "al iniciar sesion con la 009 aplicada, la corrida rechazada por su falta vuelve a pendiente y se sube");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "SUBIDAS OK" : "SUBIDAS CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
