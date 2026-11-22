// test_exportacion.cpp - Exportacion del modelo (D-013): .zip con el modelo renombrado,
// modelo.json y LEEME.txt.
//
// Arma carpetas de corrida con archivos escritos a mano (DATOS INVENTADOS, con el formato de
// metadata.json) y un modelo.bin falso, exporta y lee el .zip con un lector minimo (firmas,
// tamanos y CRC-32 de cada entrada). Casos: MLP con huella en metadata, Arbol de una corrida
// vieja (sin columnas_entrada ni huella: sha256 calculado al exportar, de la cache o del CSV),
// datos sinteticos, CSV que ya no existe, DLL de prueba y corrida sin modelo.bin (no exportables)
// y la exportacion asincrona de Historial.

#include "ExportacionModelo.h"
#include "Historial.h"
#include "PaqueteCorrida.h"
#include "ZipSimple.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
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

void escribir(const QString& ruta, const QByteArray& contenido)
{
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly)) f.write(contenido);
}

void esperar(const std::function<bool()>& listo, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

quint32 le32(const QByteArray& b, qsizetype i)
{
    return static_cast<quint8>(b[i]) | (static_cast<quint8>(b[i + 1]) << 8) | (static_cast<quint8>(b[i + 2]) << 16)
           | (static_cast<quint32>(static_cast<quint8>(b[i + 3])) << 24);
}
quint16 le16(const QByteArray& b, qsizetype i) { return static_cast<quint16>(static_cast<quint8>(b[i]) | (static_cast<quint8>(b[i + 1]) << 8)); }

// Lector minimo del .zip: recorre el directorio central y comprueba cada entrada local (firma,
// metodo stored, tamanos y CRC). Devuelve {nombre: contenido}; vacio si algo no cuadra.
QMap<QString, QByteArray> leerZip(const QString& ruta, QStringList* orden)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.readAll();
    const qsizetype fin = b.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (fin < 0) return {};
    const int n = le16(b, fin + 10);
    qsizetype c = le32(b, fin + 16);
    QMap<QString, QByteArray> r;
    for (int i = 0; i < n; ++i) {
        if (le32(b, c) != 0x02014b50) return {};
        const quint32 crc = le32(b, c + 16), tam = le32(b, c + 20);
        const int largo = le16(b, c + 28), extra = le16(b, c + 30), coment = le16(b, c + 32);
        const qsizetype local = le32(b, c + 42);
        const QString nombre = QString::fromUtf8(b.mid(c + 46, largo));
        if (le32(b, local) != 0x04034b50 || le16(b, local + 8) != 0 || le32(b, local + 14) != crc) return {};
        const qsizetype datos = local + 30 + le16(b, local + 26) + le16(b, local + 28);
        const QByteArray contenido = b.mid(datos, tam);
        if (zip::crc32(contenido) != crc) return {};
        r[nombre] = contenido;
        if (orden) *orden << nombre;
        c += 46 + largo + extra + coment;
    }
    return r;
}

struct Corrida {
    QString clave = "mlp";
    QString nombre = "Perceptron multicapa (MLP)";
    QString libreria = "libtorch 2.14.0 (CPU)";
    QJsonObject hiper{{"capas", "16,8"}, {"activacion", "tanh"}, {"optimizador", "adam"}, {"epocas", 3}};
    QJsonObject datos;  // se completa en crear()
    bool modeloBin = true;
};

QString crear(const QString& raiz, const QString& nombre, const Corrida& c)
{
    const QString dir = raiz + "/corridas/" + nombre + "/corrida_001";
    QDir().mkpath(dir);
    QJsonObject m{{"version_metadata", 1}, {"experimento", nombre}, {"corrida", 1}, {"id_corrida", nombre + "/corrida_001"},
                  {"estado", "completada"},
                  {"modelo", QJsonObject{{"clave", c.clave}, {"nombre", c.nombre}, {"libreria", c.libreria},
                                         {"dll", "EnergIA_X.dll"}, {"version_contrato", 3}}},
                  {"hiperparametros", c.hiper}, {"datos", c.datos},
                  {"evaluacion", QJsonObject{{"ejecutada", true}, {"metricas", QJsonObject{{"val_accuracy", 0.875}}}}},
                  {"tiempos", QJsonObject{{"t_creacion", "2026-10-22T15:00:00.000Z"}}}};
    escribir(dir + "/metadata.json", QJsonDocument(m).toJson());
    if (c.modeloBin) escribir(dir + "/modelo.bin", QByteArray("\x00\x01PESOS\xff", 8));
    return dir;
}

QJsonObject datosCsv(const QString& ruta)
{
    return QJsonObject{{"origen", "csv"}, {"ruta", QDir::toNativeSeparators(ruta)}, {"columna_objetivo", "etiqueta"},
                       {"n_filas_total", 40}, {"n_cols", 2}, {"n_train", 32}, {"n_val", 8}, {"porcentaje_train", 80},
                       {"semilla", 7}, {"n_clases", 3}, {"clases_originales", QJsonArray{-1, 2, 5}}};
}

QJsonObject exportar(const QString& dir, const QString& zip, const QString& huellas, QString* error)
{
    QJsonObject j;
    error->clear();
    exportacion::exportar(dir, zip, huellas, &j, error);
    return j;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString raiz = tmp.path();

    std::printf("\n== 1. CRC-32 y .zip minimo\n");
    comprobar(zip::crc32("123456789") == 0xCBF43926u, "CRC-32 de '123456789' = CBF43926 (valor de referencia)");

    std::printf("\n== 2. MLP con columnas de entrada y huella en metadata (corrida nueva)\n");
    {
        Corrida c;
        c.datos = datosCsv(raiz + "/no_hace_falta.csv");
        c.datos["columnas_entrada"] = QJsonArray{"x1", "x2"};
        c.datos["indice_objetivo"] = 1;
        c.datos["columnas_generadas"] = false;
        c.datos["sha256"] = QString(64, 'b');
        const QString dir = crear(raiz, "mlp", c);
        const QJsonObject info = exportacion::info(dir);
        comprobar(info["exportable"].toBool() && info["archivo"].toString() == "modelo_libtorch.pt"
                      && info["formato"].toString() == "libtorch_torch_save" && info["avisos"].toArray().isEmpty(),
                  "info: exportable, modelo_libtorch.pt, sin avisos");
        QString error;
        const QString zip = raiz + "/mlp.zip";
        const QJsonObject j = exportar(dir, zip, {}, &error);
        QStringList orden;
        const QMap<QString, QByteArray> z = leerZip(zip, &orden);
        comprobar(error.isEmpty() && orden == QStringList{"modelo_libtorch.pt", "modelo.json", "LEEME.txt"},
                  "el .zip tiene modelo_libtorch.pt, modelo.json y LEEME.txt, con CRC correcto (" + error + ")");
        comprobar(z.value("modelo_libtorch.pt") == QByteArray("\x00\x01PESOS\xff", 8), "el modelo es modelo.bin byte a byte");
        comprobar(QFile::exists(dir + "/modelo.bin") && !QFile::exists(dir + "/modelo_libtorch.pt"),
                  "en la carpeta de la corrida sigue llamandose modelo.bin");
        const QJsonObject mj = QJsonDocument::fromJson(z.value("modelo.json")).object();
        comprobar(mj == j, "modelo.json del .zip = el devuelto");
        comprobar(mj["formato"] == "libtorch_torch_save" && mj["libreria"] == "libtorch 2.14.0 (CPU)" && mj["version_contrato"] == 3
                      && mj["modelo"].toObject()["clave"] == "mlp" && mj["hiperparametros"].toObject()["capas"] == "16,8",
                  "modelo.json: formato, libreria con version, contrato, clave e hiperparametros");
        comprobar(mj["columnas_entrada"].toArray() == QJsonArray{"x1", "x2"} && mj["columnas_generadas"] == false
                      && mj["columna_objetivo"] == "etiqueta" && mj["indice_objetivo"] == 1 && mj["n_cols"] == 2 && mj["n_clases"] == 3,
                  "modelo.json: columnas_entrada, columnas_generadas, objetivo, n_cols y n_clases");
        comprobar(mj["clases_originales"].toArray()[2].toObject() == QJsonObject{{"indice", 2}, {"valor", 5}},
                  "modelo.json: clases_originales con su indice");
        comprobar(mj["metricas"].toObject()["val_accuracy"] == 0.875 && mj["particion"].toObject()["semilla"] == 7
                      && mj["particion"].toObject()["porcentaje_entrenamiento"] == 80,
                  "modelo.json: metricas de evaluar, particion y semilla");
        comprobar(mj["id_corrida"] == "mlp/corrida_001" && mj["t_creacion"] == "2026-10-22T15:00:00.000Z",
                  "modelo.json: id de la corrida y t_creacion");
        comprobar(mj["sha256_dataset"] == QString(64, 'b') && mj["sha256_origen"] == "corrida",
                  "sha256 de metadata.datos: sha256_origen = corrida (sin el CSV)");
        const QString leeme = QString::fromUtf8(z.value("LEEME.txt"));
        comprobar(leeme.contains("torch::load(red, \"modelo_libtorch.pt\")") && leeme.contains("{16,8}")
                      && leeme.contains("torch::nn::Tanh()") && leeme.contains("Linear(previas, 3)") && leeme.contains("int64_t previas = 2"),
                  "LEEME: reconstruir la misma red (capas, activacion, n_cols, n_clases) y torch::load");
        comprobar(leeme.contains("x1, x2") && leeme.contains("2 -> 5") && leeme.contains("\r\n"),
                  "LEEME: columnas en orden, etiquetas segun clases_originales, fin de linea de Windows");
        // Corrida sin metricas_modelo.json (anterior a D-015): resumen null y el .zip no lo trae
        comprobar(mj["metricas_modelo"].isNull() && !z.contains("metricas_modelo.json")
                      && leeme.contains("no tiene métricas del modelo"),
                  "sin metricas_modelo.json: metricas_modelo null, sin el archivo en el .zip y LEEME lo dice");
    }

    std::printf("\n== 2b. Con metricas_modelo.json (D-015): resumen en modelo.json y archivo completo en el .zip\n");
    {
        Corrida c;
        c.datos = datosCsv(raiz + "/no_hace_falta.csv");
        c.datos["columnas_entrada"] = QJsonArray{"x1", "x2"};
        const QString dir = crear(raiz, "mlp_metricas", c);
        auto conjunto = [](double exactitud, double p, double r, double f) {
            return QJsonObject{{"status_dll", 0}, {"motivo", QJsonValue::Null}, {"exactitud", exactitud},
                               {"macro", QJsonObject{{"precision", p}, {"recall", r}, {"f1", f}}}};
        };
        const QJsonObject mm{{"version_formato", 1}, {"clases_originales", QJsonArray{-1, 2, 5}},
                             {"validacion", conjunto(0.875, 0.8, 0.7, 0.75)},
                             {"entrenamiento", QJsonObject{{"status_dll", 3}, {"motivo", "predecir devolvio: error interno de la DLL"},
                                                           {"exactitud", QJsonValue::Null}, {"macro", QJsonValue::Null}}}};
        const QByteArray bytes = QJsonDocument(mm).toJson();
        escribir(dir + "/metricas_modelo.json", bytes);
        QString error;
        const QJsonObject j = exportar(dir, raiz + "/mlp_metricas.zip", {}, &error);
        QStringList orden;
        const QMap<QString, QByteArray> z = leerZip(raiz + "/mlp_metricas.zip", &orden);
        comprobar(error.isEmpty()
                      && orden == QStringList{"modelo_libtorch.pt", "modelo.json", "LEEME.txt", "metricas_modelo.json"}
                      && z.value("metricas_modelo.json") == bytes,
                  "el .zip agrega metricas_modelo.json byte a byte");
        const QJsonObject r = j["metricas_modelo"].toObject();
        comprobar(r["archivo"] == "metricas_modelo.json"
                      && r["validacion"].toObject() == QJsonObject{{"exactitud", 0.875}, {"f1_macro", 0.75}, {"precision_macro", 0.8},
                                                                   {"recall_macro", 0.7}, {"motivo", QJsonValue::Null}},
                  "modelo.json: resumen de validacion (exactitud y F1, precision y recall macro)");
        comprobar(r["entrenamiento"].toObject()["exactitud"].isNull() && r["entrenamiento"].toObject()["f1_macro"].isNull()
                      && r["entrenamiento"].toObject()["motivo"] == "predecir devolvio: error interno de la DLL",
                  "modelo.json: un conjunto que fallo queda en null con su motivo");
        comprobar(j["metricas"].toObject()["val_accuracy"] == 0.875, "modelo.json: metricas de evaluar sin cambios");
        const QString leeme = QString::fromUtf8(z.value("LEEME.txt"));
        comprobar(leeme.contains("metricas_modelo.json (en este .zip)") && leeme.contains("zero_division=0"),
                  "LEEME menciona metricas_modelo.json");
    }

    std::printf("\n== 3. Arbol de una corrida vieja: sin columnas_entrada ni huella\n");
    {
        const QString csv = raiz + "/datos_arbol.csv";
        escribir(csv, "a,b,etiqueta\n1,2,0\n3,4,1\n");
        Corrida c;
        c.clave = "arbol_decision";
        c.nombre = "Arbol de decision";
        c.libreria = "mlpack 4.8.0 / Armadillo 14.0.0";
        c.hiper = QJsonObject{{"criterio", "entropia"}, {"max_depth", 0}};
        c.datos = datosCsv(csv);
        const QString dir = crear(raiz, "arbol", c);
        const QJsonObject info = exportacion::info(dir);
        comprobar(info["exportable"].toBool() && info["archivo"] == "modelo_mlpack.bin" && !info["columnas_conocidas"].toBool()
                      && info["avisos"].toArray().size() == 1,
                  "info: exportable como modelo_mlpack.bin, con el aviso de columnas desconocidas para la interfaz");
        QString error;
        QJsonObject j = exportar(dir, raiz + "/arbol.zip", {}, &error);
        const QMap<QString, QByteArray> z = leerZip(raiz + "/arbol.zip", nullptr);
        const QString shaReal = PaqueteCorrida::huellaArchivo(csv, &error);
        comprobar(z.contains("modelo_mlpack.bin") && j["columnas_entrada"].isNull() && j["columnas_generadas"].isNull(),
                  "se exporta igual, con columnas_entrada null");
        comprobar(j["sha256_dataset"] == shaReal && j["sha256_origen"] == "calculada_al_exportar",
                  "sin huella en metadata: sha256 del CSV actual, sha256_origen = calculada_al_exportar");
        const QString leeme = QString::fromUtf8(z.value("LEEME.txt"));
        comprobar(leeme.contains("anterior a columnas_entrada") && leeme.contains("puede no") && leeme.contains("columnas_entrada es null"),
                  "LEEME: avisos de columnas desconocidas y de sha256 calculado al exportar");
        comprobar(leeme.contains("mlpack::DecisionTree<mlpack::InformationGain>") && leeme.contains("cereal::BinaryInputArchive")
                      && leeme.contains("make_nvp(\"model\", arbol)"),
                  "LEEME: el criterio (entropia -> InformationGain) y la carga con cereal/mlpack");

        // Con la cache de huellas (misma ruta, tamano y fecha) no se lee el CSV
        const QFileInfo fi(csv);
        const QString huellas = raiz + "/huellas.json";
        escribir(huellas, QJsonDocument(QJsonObject{{QDir::toNativeSeparators(csv),
                                                     QJsonObject{{"tam", fi.size()},
                                                                 {"fecha", fi.lastModified().toUTC().toString(Qt::ISODateWithMs)},
                                                                 {"sha256", QString(64, 'c')}}}})
                               .toJson());
        j = exportar(dir, raiz + "/arbol2.zip", huellas, &error);
        comprobar(j["sha256_dataset"] == QString(64, 'c') && j["sha256_origen"] == "calculada_al_exportar",
                  "con la cache de huellas: se reutiliza la huella (sha256_origen = calculada_al_exportar)");

        // El CSV ya no esta: sin sha256
        QFile::remove(csv);
        j = exportar(dir, raiz + "/arbol3.zip", {}, &error);
        comprobar(error.isEmpty() && j["sha256_dataset"].isNull() && j["sha256_origen"].isNull()
                      && QJsonDocument(j["avisos"].toArray()).toJson().contains("No se encontr"),
                  "sin el CSV: sha256 null y aviso");
    }

    std::printf("\n== 4. Datos sinteticos (modo de desarrollo)\n");
    {
        Corrida c;
        c.datos = QJsonObject{{"origen", "sintetico"}, {"columna_objetivo", "clase"}, {"n_cols", 8}, {"n_clases", 3},
                              {"clases_originales", QJsonArray{0, 1, 2}}, {"columnas_entrada", QJsonArray{"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"}},
                              {"indice_objetivo", 8}, {"columnas_generadas", true}, {"sha256", QJsonValue::Null}};
        const QString dir = crear(raiz, "sintetico", c);
        QString error;
        const QJsonObject j = exportar(dir, raiz + "/sint.zip", {}, &error);
        const QString leeme = QString::fromUtf8(leerZip(raiz + "/sint.zip", nullptr).value("LEEME.txt"));
        comprobar(j["datos_sinteticos"] == true && j["columnas_generadas"] == true && j["dataset"].isNull()
                      && j["sha256_dataset"].isNull() && j["sha256_origen"].isNull(),
                  "modelo.json: datos_sinteticos, columnas_generadas, sin dataset ni sha256");
        comprobar(leeme.contains("DATOS DE DESARROLLO") && leeme.contains("nombres de las columnas son generados"),
                  "LEEME: dice que son datos de desarrollo con nombres generados");
        comprobar(exportacion::info(dir)["datos_sinteticos"].toBool() && exportacion::info(dir)["avisos"].toArray().size() == 1,
                  "info: aviso de datos sinteticos para la interfaz");
    }

    std::printf("\n== 5. No exportables\n");
    {
        Corrida p;
        p.clave = "prueba";
        p.datos = datosCsv(raiz + "/x.csv");
        const QString dirP = crear(raiz, "prueba", p);
        QString error;
        QJsonObject j;
        comprobar(!exportacion::info(dirP)["exportable"].toBool() && !exportacion::exportar(dirP, raiz + "/p.zip", {}, &j, &error),
                  "DLL de prueba: no exportable");
        comprobar(!QFile::exists(raiz + "/p.zip") && !error.isEmpty(), "no se escribe el .zip: " + error);
        Corrida s;
        s.modeloBin = false;
        s.datos = datosCsv(raiz + "/x.csv");
        const QString dirS = crear(raiz, "sin_modelo", s);
        comprobar(!exportacion::info(dirS)["exportable"].toBool()
                      && exportacion::info(dirS)["motivo"].toString().contains("modelo.bin"),
                  "sin modelo.bin: no exportable (" + exportacion::info(dirS)["motivo"].toString() + ")");
    }

    std::printf("\n== 6. Historial: exportacion en otro hilo\n");
    {
        Historial h(raiz);
        const QString dir = raiz + "/corridas/mlp/corrida_001";
        comprobar(h.infoExportacion(dir)["exportable"].toBool(), "infoExportacion");
        QString err = "sin respuesta";
        QVariantList avisos;
        QObject::connect(&h, &Historial::modeloExportado, [&](const QString&, const QString&, const QString& e, const QVariantList& a) {
            err = e;
            avisos = a;
        });
        h.exportarModelo(dir, raiz + "/desde_historial.zip");
        comprobar(h.exportandoModelo(), "exportandoModelo mientras trabaja el hilo");
        esperar([&] { return err != "sin respuesta"; });
        comprobar(err.isEmpty() && !h.exportandoModelo() && leerZip(raiz + "/desde_historial.zip", nullptr).size() == 3,
                  "modeloExportado sin error y el .zip escrito");
    }

    // Opcional: copia los .zip a una carpeta para abrirlos con otras herramientas (unzip, 7-Zip)
    if (argc > 1)
        for (const char* z : {"mlp.zip", "arbol.zip", "sint.zip"}) QFile::copy(raiz + "/" + z, QString::fromLocal8Bit(argv[1]) + "/" + z);

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "EXPORTACION OK" : "EXPORTACION CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
