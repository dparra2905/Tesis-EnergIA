// test_corrida.cpp - Pruebas de una corrida completa con energia_orquestador --ejecutar (F3).
//
// Lanza el orquestador real como proceso hijo (como lo hara ui.exe), con config.json en un
// directorio temporal y la DLL de prueba, y comprueba:
//   - corrida completada: carpeta corrida_NNN, progreso.csv, metadata.json, evaluacion y modelo;
//   - numeracion de corridas (corrida_001, corrida_002);
//   - cancelacion por stdin a mitad del entrenamiento;
//   - datos desde CSV;
//   - configuracion invalida (sin corrida);
//   - stdout solo con eventos JSON Lines validos; el cierre de stdin no detiene la corrida.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

struct Resultado {
    int codigo = -1;
    QList<QJsonObject> eventos;
    int lineasNoJson = 0;
    QByteArray stderrTexto;
};

QDateTime leerIso(const QJsonValue& v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

QJsonObject leerJson(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString escribirConfig(const QDir& dir, const QString& nombre, const QJsonObject& cfg)
{
    const QString ruta = dir.filePath(nombre);
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(cfg).toJson());
    return ruta;
}

// Ejecuta el orquestador. Si cancelar es true, envia la orden al ver 'cancelarEn' en stdout
// (por defecto, el primer fin de paso).
Resultado correr(const QString& rutaConfig, bool cancelar, const QByteArray& cancelarEn = "\"fin_paso\"")
{
    Resultado r;
    QProcess p;
    p.start(QString::fromUtf8(ENERGIA_ORQUESTADOR), {"--ejecutar", rutaConfig});
    if (!p.waitForStarted(10000)) { r.codigo = -2; return r; }
    if (!cancelar) p.closeWriteChannel();  // como si ui.exe se cerrara: la corrida debe seguir

    QByteArray salida;
    bool enviado = false;
    QElapsedTimer reloj;
    reloj.start();
    while (p.state() != QProcess::NotRunning && reloj.elapsed() < 60000) {
        p.waitForReadyRead(100);
        salida += p.readAllStandardOutput();
        if (cancelar && !enviado && salida.contains(cancelarEn)) {
            p.write("{\"v\":1,\"orden\":\"cancelar\"}\n");
            enviado = true;
        }
    }
    if (p.state() != QProcess::NotRunning) { p.kill(); p.waitForFinished(5000); }
    salida += p.readAllStandardOutput();
    r.stderrTexto = p.readAllStandardError();
    r.codigo = (p.exitStatus() == QProcess::NormalExit) ? p.exitCode() : -3;

    for (const QByteArray& linea : salida.split('\n')) {
        if (linea.trimmed().isEmpty()) continue;
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(linea, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject() || doc.object()["v"].toInt() != 1
            || doc.object()["evento"].toString().isEmpty())
            ++r.lineasNoJson;
        else
            r.eventos.append(doc.object());
    }
    return r;
}

QList<QJsonObject> filtrar(const Resultado& r, const QString& evento)
{
    QList<QJsonObject> out;
    for (const QJsonObject& e : r.eventos)
        if (e["evento"].toString() == evento) out.append(e);
    return out;
}

QList<QStringList> leerProgreso(const QString& ruta, QStringList* encabezado)
{
    QList<QStringList> filas;
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return filas;
    *encabezado = QString::fromUtf8(f.readLine()).trimmed().split(',');
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.isEmpty()) filas.append(l.split(','));
    }
    return filas;
}

// Busca recursivamente claves que sugieran metricas energeticas (F3 no mide energia)
bool tieneClaveEnergetica(const QJsonValue& v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString k = it.key().toLower();
            // Contiene "energ" sin ser una metrica de energia (D-009/D-013: el modelo no se recarga en EnergIA)
            if (k == "recargable_en_energia") continue;
            if (k.contains("energ") || k.contains("potencia") || k.contains("power") || k.contains("kwh")) return true;
            if (tieneClaveEnergetica(it.value())) return true;
        }
    } else if (v.isArray()) {
        for (const QJsonValue& x : v.toArray())
            if (tieneClaveEnergetica(x)) return true;
    }
    return false;
}

QJsonObject configBase(const QString& dirSalida, const QString& experimento, int pasos, int msPorPaso)
{
    QJsonObject hiper;
    hiper["pasos"] = pasos;
    hiper["ms_por_paso"] = msPorPaso;
    hiper["curva"] = "lineal";
    QJsonObject modelo;
    modelo["dll"] = QString::fromUtf8(ENERGIA_DLL_PRUEBA);
    modelo["hiperparametros"] = hiper;
    QJsonObject datos;
    datos["origen"] = "sintetico";
    QJsonObject particion;
    particion["porcentaje_entrenamiento"] = 80;
    particion["semilla"] = 42;
    QJsonObject c;
    c["version_config"] = 1;
    c["experimento"] = experimento;
    c["dir_salida"] = dirSalida;
    c["modelo"] = modelo;
    c["datos"] = datos;
    c["particion"] = particion;
    c["guardar_modelo"] = true;
    // Sin linea base (el orquestador usa 20 s si no se indica): las pruebas no esperan 40 s
    c["tiempos"] = QJsonObject{{"fase_previa_ms", 0}, {"fase_posterior_ms", 0}};
    return c;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QDir raiz(tmp.path());
    const QString dirExp = raiz.filePath("corridas/prueba_f3");

    // ---------------------------------------------------------------- 1. corrida completada
    std::printf("\n== Corrida completada (sintetico, 5 pasos)\n");
    const Resultado r1 = correr(escribirConfig(raiz, "config1.json", configBase(raiz.path(), "prueba_f3", 5, 100)), false);
    comprobar(r1.codigo == 0, QString("codigo de salida 0 (obtenido %1)").arg(r1.codigo));
    comprobar(r1.lineasNoJson == 0 && !r1.eventos.isEmpty(),
              QString("stdout: %1 eventos JSON Lines validos y %2 lineas invalidas").arg(r1.eventos.size()).arg(r1.lineasNoJson));
    comprobar(!r1.stderrTexto.isEmpty() && !r1.stderrTexto.trimmed().startsWith('{'), "stderr contiene diagnostico en texto (no eventos)");
    comprobar(r1.eventos.first()["evento"].toString() == "inicio" && r1.eventos.last()["evento"].toString() == "fin",
              "el primer evento es 'inicio' y el ultimo 'fin'");
    comprobar(filtrar(r1, "fin").first()["estado"].toString() == "completada" && filtrar(r1, "fin").first()["error"].isNull(),
              "evento fin: estado completada y error null");
    comprobar(filtrar(r1, "progreso").size() == 10, QString("10 eventos de progreso (5 inicio + 5 fin; obtenidos %1)").arg(filtrar(r1, "progreso").size()));

    const QString dir1 = dirExp + "/corrida_001";
    comprobar(QDir(dir1).exists(), "se crea corridas/prueba_f3/corrida_001");

    QStringList enc;
    const QList<QStringList> filas = leerProgreso(dir1 + "/progreso.csv", &enc);
    comprobar(enc.size() == 12 && enc[0] == "timestamp_inicio" && enc[3] == "step_total", "progreso.csv: encabezado de 12 columnas");
    comprobar(filas.size() == 5, QString("progreso.csv: una fila por paso (%1 filas)").arg(filas.size()));

    const QJsonObject m1 = leerJson(dir1 + "/metadata.json");
    comprobar(!m1.isEmpty(), "metadata.json es JSON valido");
    comprobar(m1["estado"].toString() == "completada" && m1["error"].isNull(), "metadata: estado completada, error null");
    comprobar(m1["experimento"].toString() == "prueba_f3" && m1["corrida"].toInt() == 1
                  && m1["id_corrida"].toString() == "prueba_f3/corrida_001", "metadata: experimento, corrida e id_corrida");
    const QJsonObject mod = m1["modelo"].toObject();
    comprobar(mod["clave"].toString() == "prueba" && mod["interno"].toBool() && mod["version_contrato"].toInt() == 4,
              "metadata: modelo 'prueba' interno con contrato v4");
    const QJsonObject hip = m1["hiperparametros"].toObject();
    comprobar(hip["pasos"].toInt() == 5 && hip["curva"].toString() == "lineal" && hip["capas"].toString() == "64,32",
              "metadata: hiperparametros usados (solicitados + por defecto, opcion por nombre)");
    comprobar(m1["datos"].toObject()["origen"].toString() == "sintetico" && m1["datos"].toObject()["n_train"].toInt() == 1200,
              "metadata: datos y particion 80/20");

    const QJsonObject tiempos = m1["tiempos"].toObject();
    const QDateTime tStart = leerIso(tiempos["t_start"]), tAntes = leerIso(tiempos["t_antes"]);
    const QDateTime tDespues = leerIso(tiempos["t_despues"]), tFin = leerIso(tiempos["t_fin"]);
    comprobar(tStart.isValid() && tAntes.isValid() && tDespues.isValid() && tFin.isValid(), "metadata: tiempos presentes");
    comprobar(tStart <= tAntes && tAntes <= tDespues && tDespues <= tFin, "metadata: t_start <= t_antes <= t_despues <= t_fin");
    comprobar(tiempos["fase_previa_ms"].toInt() == 0 && tiempos["fase_posterior_ms"].toInt() == 0,
              "metadata: fases previa/posterior en 0 ms, como se pidieron");
    // Sin la inferencia activada la corrida es como antes (D-014)
    comprobar(m1["inferencia"].toObject() == QJsonObject{{"activada", false}} && tiempos["t_inf_antes"].isNull()
                  && tiempos["inferencia_iniciada"] == QJsonValue(false) && !QFile::exists(dir1 + "/progreso_inferencia.csv"),
              "metadata: sin inferencia, metadata.inferencia = {activada: false} y sin progreso_inferencia.csv");

    bool tiemposPasoOk = filas.size() == 5;
    QDateTime anterior = tAntes;
    for (int i = 0; i < filas.size(); ++i) {
        const QDateTime ini = QDateTime::fromString(filas[i][0], Qt::ISODateWithMs);
        const QDateTime fin = QDateTime::fromString(filas[i][1], Qt::ISODateWithMs);
        tiemposPasoOk = tiemposPasoOk && ini.isValid() && fin.isValid() && anterior <= ini && ini <= fin && fin <= tDespues
                        && filas[i][2].toInt() == i && filas[i][3].toInt() == 5 && filas[i][4] == "train_loss";
        anterior = fin;
    }
    comprobar(tiemposPasoOk, "progreso.csv: timestamps coherentes y dentro de [t_antes, t_despues]");

    const QJsonObject eval = m1["evaluacion"].toObject();
    const double acc = eval["metricas"].toObject()["val_accuracy"].toDouble(-1);
    comprobar(eval["ejecutada"].toBool() && eval["status"].toInt() == 0 && acc >= 0 && acc <= 1,
              QString("metadata: evaluacion con val_accuracy = %1").arg(acc));
    const QJsonObject artMod = m1["artefactos"].toObject()["modelo"].toObject();
    comprobar(artMod["guardado"].toBool() && artMod["archivo"].toString() == "modelo.bin" && QFile::exists(dir1 + "/modelo.bin"),
              "artefacto modelo.bin guardado");
    // D-009 actualizada (D-013): no se recarga en EnergIA; la DLL de prueba no es exportable
    comprobar(artMod["recargable_en_energia"] == false && artMod["exportable"] == false && !artMod.contains("reutilizable"),
              "el artefacto se declara no recargable en EnergIA y, con la DLL de prueba, no exportable (D-009, D-013)");
    const QJsonObject ent = m1["entrenamiento"].toObject();
    // La DLL se libera al terminar el proceso, no con FreeLibrary (ver DllModelo.h)
    comprobar(ent["modelo_liberado"].toString() == "liberado" && ent["dll_descarga"].toString() == "al_terminar_el_proceso",
              "modelo liberado; la DLL se libera al terminar el proceso");
    comprobar(m1["medidores"].toArray().isEmpty() && !tieneClaveEnergetica(m1), "sin medidores ni metricas energeticas (F3)");
    comprobar(QFile::exists(dir1 + "/config.json"), "config.json queda guardado en la corrida");

    // ---------------------------------------------------------------- 2. numeracion
    std::printf("\n== Segunda corrida del mismo experimento\n");
    const Resultado r2 = correr(escribirConfig(raiz, "config2.json", configBase(raiz.path(), "prueba_f3", 2, 50)), false);
    comprobar(r2.codigo == 0 && QDir(dirExp + "/corrida_002").exists(), "se crea corrida_002");

    // ---------------------------------------------------------------- 3. cancelacion
    std::printf("\n== Cancelacion a mitad (50 pasos de 200 ms)\n");
    const Resultado r3 = correr(escribirConfig(raiz, "config3.json", configBase(raiz.path(), "prueba_f3", 50, 200)), true);
    comprobar(r3.codigo == 5, QString("codigo de salida 5 (obtenido %1)").arg(r3.codigo));
    comprobar(r3.lineasNoJson == 0, "stdout: solo eventos JSON Lines");
    comprobar(filtrar(r3, "cancelacion_solicitada").size() == 1, "evento cancelacion_solicitada");
    comprobar(filtrar(r3, "fin").size() == 1 && filtrar(r3, "fin").first()["estado"].toString() == "cancelada",
              "evento fin: estado cancelada");
    const QString dir3 = dirExp + "/corrida_003";
    const QJsonObject m3 = leerJson(dir3 + "/metadata.json");
    comprobar(m3["estado"].toString() == "cancelada", "metadata: estado cancelada");
    comprobar(m3["entrenamiento"].toObject()["status_dll"].toInt() == 4, "metadata: la DLL devolvio ENERGIA_CANCELADO (4)");
    QStringList enc3;
    const int filas3 = leerProgreso(dir3 + "/progreso.csv", &enc3).size();
    comprobar(filas3 >= 1 && filas3 < 50, QString("progreso.csv: %1 de 50 pasos antes de cancelar").arg(filas3));
    comprobar(!QFile::exists(dir3 + "/modelo.bin") && !m3["artefactos"].toObject()["modelo"].toObject()["guardado"].toBool(),
              "no se guarda modelo en una corrida cancelada");
    comprobar(m3["entrenamiento"].toObject()["dll_descarga"].toString() == "al_terminar_el_proceso"
                  && m3["entrenamiento"].toObject()["modelo_liberado"].toString() == "no_aplica",
              "cancelada: no queda modelo vivo (la DLL lo libera al cancelar)");
    comprobar(m3["evaluacion"].toObject()["ejecutada"] == false, "cancelada: no se evalua (evaluacion.ejecutada = false)");

    // ------------------------------------------- 3b. cancelacion en la linea base posterior
    std::printf("\n== Cancelacion durante la linea base posterior\n");
    QJsonObject c3b = configBase(raiz.path(), "cancela_post", 2, 50);
    c3b["tiempos"] = QJsonObject{{"fase_previa_ms", 0}, {"fase_posterior_ms", 5000}};
    QElapsedTimer reloj3b;
    reloj3b.start();
    const Resultado r3b = correr(escribirConfig(raiz, "config3b.json", c3b), true, "\"fase_posterior\"");
    const qint64 dur3b = reloj3b.elapsed();
    const QString dir3b = raiz.filePath("corridas/cancela_post/corrida_001");
    const QJsonObject m3b = leerJson(dir3b + "/metadata.json");
    comprobar(r3b.codigo == 5 && m3b["estado"].toString() == "cancelada" && dur3b < 4500,
              QString("cancelada en la fase posterior: codigo %1, estado %2, %3 ms").arg(r3b.codigo).arg(m3b["estado"].toString()).arg(dur3b));
    comprobar(m3b["evaluacion"].toObject()["ejecutada"].toBool() && m3b["evaluacion"].toObject()["metricas"].toObject().contains("val_accuracy"),
              "el entrenamiento termino bien: se evalua y la exactitud queda registrada");
    comprobar(!QFile::exists(dir3b + "/modelo.bin") && !m3b["artefactos"].toObject()["modelo"].toObject()["guardado"].toBool()
                  && m3b["artefactos"].toObject()["modelo"].toObject()["solicitado"].toBool(),
              "no se guarda el modelo aunque se pidio (corrida cancelada)");
    comprobar(m3b["entrenamiento"].toObject()["modelo_liberado"].toString() == "liberado", "el modelo se libera");
    comprobar(m3b["tiempos"].toObject()["fase_posterior_iniciada"].toBool(), "la fase posterior habia empezado");

    // ---------------------------------------------------------------- 4. datos CSV
    std::printf("\n== Corrida con CSV\n");
    {
        QFile csv(raiz.filePath("mini.csv"));
        if (csv.open(QIODevice::WriteOnly)) {
            // La columna objetivo va en medio: columnas_entrada debe conservar el orden sin ella (D1)
            csv.write("x1;etiqueta;x2\n");
            for (int i = 0; i < 40; ++i)
                csv.write(QString("%1;%2;%3\n").arg(i % 7 * 0.5).arg(i % 2).arg(static_cast<double>(i % 3), 0, 'f', 1).replace('.', ',').toUtf8());
        }
    }
    QJsonObject c4 = configBase(raiz.path(), "prueba_csv", 2, 50);
    QJsonObject datosCsv;
    datosCsv["origen"] = "csv";
    datosCsv["ruta"] = raiz.filePath("mini.csv");
    datosCsv["columna_objetivo"] = "etiqueta";
    c4["datos"] = datosCsv;
    const Resultado r4 = correr(escribirConfig(raiz, "config4.json", c4), false);
    const QJsonObject m4 = leerJson(raiz.filePath("corridas/prueba_csv/corrida_001/metadata.json"));
    comprobar(r4.codigo == 0 && m4["estado"].toString() == "completada", "corrida con CSV completada");
    comprobar(m4["datos"].toObject()["origen"].toString() == "csv" && m4["datos"].toObject()["columna_objetivo"].toString() == "etiqueta"
                  && m4["datos"].toObject()["n_filas_total"].toInt() == 40 && m4["datos"].toObject()["separador"].toString() == ";",
              "metadata: CSV con separador ';', columna 'etiqueta' y 40 filas");
    const QJsonObject d4 = m4["datos"].toObject();
    comprobar(d4["columnas_entrada"].toArray() == QJsonArray{"x1", "x2"} && d4["indice_objetivo"].toInt(-1) == 1
                  && d4["columnas_generadas"] == false,
              "metadata: columnas_entrada en orden sin la objetivo, indice_objetivo y columnas_generadas = false (D1)");

    // ---------------------------------------------------------------- 5. configuracion invalida
    std::printf("\n== Configuracion invalida\n");
    QJsonObject c5 = configBase(raiz.path(), "invalido", 2, 50);
    QJsonObject mod5 = c5["modelo"].toObject();
    mod5["dll"] = raiz.filePath("no_existe.dll");
    c5["modelo"] = mod5;
    const Resultado r5 = correr(escribirConfig(raiz, "config5.json", c5), false);
    comprobar(r5.codigo == 6, QString("codigo de salida 6 (obtenido %1)").arg(r5.codigo));
    comprobar(r5.eventos.size() == 1 && r5.eventos.first()["evento"].toString() == "fin"
                  && r5.eventos.first()["estado"].toString() == "error"
                  && r5.eventos.first()["error"].toObject()["tipo"].toString() == "configuracion",
              "un unico evento fin con estado error y tipo configuracion");
    comprobar(!QDir(raiz.filePath("corridas/invalido")).exists(), "no se crea carpeta de corrida");

    QJsonObject c6 = configBase(raiz.path(), "hiper_invalido", 2, 50);
    QJsonObject mod6 = c6["modelo"].toObject();
    QJsonObject hip6 = mod6["hiperparametros"].toObject();
    hip6["pasos"] = 0;  // rango 1..1000
    mod6["hiperparametros"] = hip6;
    c6["modelo"] = mod6;
    const Resultado r6 = correr(escribirConfig(raiz, "config6.json", c6), false);
    comprobar(r6.codigo == 6 && !r6.eventos.isEmpty() && r6.eventos.last()["error"].toObject()["tipo"].toString() == "configuracion"
                  && !QDir(raiz.filePath("corridas/hiper_invalido")).exists(),
              "hiperparametro fuera de rango: error de configuracion antes de crear la corrida");

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "CORRIDA OK" : "CORRIDA CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
