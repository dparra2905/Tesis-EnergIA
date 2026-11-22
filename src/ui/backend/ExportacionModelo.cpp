// ExportacionModelo.cpp - ver ExportacionModelo.h.
#include "ExportacionModelo.h"

#include "FormatosModelo.h"
#include "PaqueteCorrida.h"
#include "ZipSimple.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace exportacion {

namespace {

constexpr const char* ARCHIVO_MODELO = "modelo.bin";

QJsonObject leerJson(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString aviso(const char* clave)
{
    const QString c = QLatin1String(clave);
    if (c == "columnas")
        return "La corrida es anterior a columnas_entrada: EnergIA no guardó el orden de las columnas de entrada. "
               "Son las columnas del CSV original sin la columna objetivo, en el orden del encabezado: hace falta "
               "ese CSV para saber cuál es cada una.";
    if (c == "sinteticos")
        return "Datos de desarrollo (sintéticos): los nombres de las columnas son generados (x0, x1, ...) y no "
               "corresponden a un dataset real.";
    if (c == "sha_exportar")
        return "El sha256 del dataset se calculó al exportar: describe el CSV que hay ahora en esa ruta, que puede no "
               "ser el que se usó en la corrida.";
    if (c == "sin_csv") return "No se encontró el CSV de la corrida: modelo.json no lleva el sha256 del dataset.";
    return {};
}

// Avisos que se conocen sin leer el dataset
QJsonArray avisosBase(const QJsonObject& datos)
{
    QJsonArray a;
    if (datos["origen"].toString() == "sintetico") a.append(aviso("sinteticos"));
    if (!datos["columnas_entrada"].isArray()) a.append(aviso("columnas"));
    return a;
}

QStringList lista(const QJsonValue& v)
{
    QStringList l;
    for (const QJsonValue& x : v.toArray()) l << (x.isString() ? x.toString() : QString::number(x.toDouble(), 'g', 17));
    return l;
}

QString valorTexto(const QJsonValue& v)
{
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 17);
    if (v.isBool()) return v.toBool() ? "true" : "false";
    return "null";
}

QString leemeMlp(const QJsonObject& j)
{
    const QJsonObject h = j["hiperparametros"].toObject();
    const QString capas = h["capas"].toString().isEmpty() ? QString("64,32") : h["capas"].toString();
    const QString act = h["activacion"].toString();
    const QString capaAct = act == "tanh" ? "Tanh" : act == "sigmoide" ? "Sigmoid" : "ReLU";
    const QString nCols = valorTexto(j["n_cols"]), nClases = valorTexto(j["n_clases"]);
    QString t;
    t += "CÓMO CARGAR EL MODELO (MLP, libtorch)\n"
         "-------------------------------------\n"
         "modelo_libtorch.pt es la red guardada con torch::save (formato nativo de libtorch para C++).\n"
         "torch::load solo carga los pesos: antes hay que construir la MISMA red que construyó EnergIA:\n"
         "por cada capa oculta, Linear seguida de la activación, y al final una Linear con una salida por\n"
         "clase (logits). Con los valores de esta corrida:\n\n"
         "    #include <torch/torch.h>\n\n"
         "    torch::nn::Sequential red;\n"
         "    int64_t previas = " + nCols + ";                 // n_cols\n"
         "    for (int64_t neuronas : {" + capas + "}) {       // hiperparametros.capas\n"
         "        red->push_back(torch::nn::Linear(previas, neuronas));\n"
         "        red->push_back(torch::nn::" + capaAct + "());          // activacion: " + (act.isEmpty() ? "relu" : act) + "\n"
         "        previas = neuronas;\n"
         "    }\n"
         "    red->push_back(torch::nn::Linear(previas, " + nClases + "));   // n_clases\n"
         "    torch::load(red, \"modelo_libtorch.pt\");\n"
         "    red->eval();\n"
         "    torch::NoGradGuard sinGradientes;\n\n"
         "    // X: tensor float32 de N x " + nCols + " (una fila por ejemplo), columnas en el orden de columnas_entrada\n"
         "    torch::Tensor indices = red->forward(X).argmax(1);   // 0 .. n_clases-1\n\n"
         "La activación sale de hiperparametros.activacion: relu -> torch::nn::ReLU, tanh -> torch::nn::Tanh,\n"
         "sigmoide -> torch::nn::Sigmoid. Usa una versión de libtorch compatible con la del campo\n"
         "\"libreria\" de modelo.json. EnergIA convierte los datos a float32 antes de entrenar.\n";
    return t;
}

QString leemeArbol(const QJsonObject& j)
{
    const QString criterio = j["hiperparametros"].toObject()["criterio"].toString();
    const QString ganancia = criterio == "entropia" ? "mlpack::InformationGain" : "mlpack::GiniGain";
    const QString nCols = valorTexto(j["n_cols"]);
    QString t;
    t += "CÓMO CARGAR EL MODELO (Árbol de decisión, mlpack)\n"
         "-------------------------------------------------\n"
         "modelo_mlpack.bin es el árbol serializado con cereal en formato binario (el mismo que usa\n"
         "mlpack::data::Save con extensión .bin), bajo el nombre \"model\". Para cargarlo hay que declarar\n"
         "el árbol con el mismo criterio con que se entrenó (hiperparametros.criterio = " + (criterio.isEmpty() ? "gini" : criterio) + "):\n"
         "gini -> mlpack::GiniGain, entropia -> mlpack::InformationGain.\n\n"
         "    #include <mlpack.hpp>\n"
         "    #include <cereal/archives/binary.hpp>\n"
         "    #include <fstream>\n\n"
         "    mlpack::DecisionTree<" + ganancia + "> arbol;\n"
         "    std::ifstream f(\"modelo_mlpack.bin\", std::ios::binary);\n"
         "    cereal::BinaryInputArchive ar(f);\n"
         "    ar(cereal::make_nvp(\"model\", arbol));\n\n"
         "    // X: arma::mat de " + nCols + " x N en double: una COLUMNA por ejemplo (convención de mlpack),\n"
         "    // con las filas de X en el orden de columnas_entrada\n"
         "    arma::Row<size_t> indices;\n"
         "    arbol.Classify(X, indices);   // 0 .. n_clases-1\n\n"
         "Usa la misma versión de mlpack (y de cereal) del campo \"libreria\" de modelo.json: el formato\n"
         "binario de cereal no es portable entre versiones distintas.\n";
    return t;
}

} // namespace

QJsonObject info(const QString& dirCorrida)
{
    QJsonObject r;
    r["exportable"] = false;
    const QDir dir(dirCorrida);
    const QJsonObject m = leerJson(dir.filePath("metadata.json"));
    if (m.isEmpty()) {
        r["motivo"] = "La carpeta no contiene una corrida (falta metadata.json)";
        return r;
    }
    const QJsonObject datos = m["datos"].toObject();
    r["columnas_conocidas"] = datos["columnas_entrada"].isArray();
    r["datos_sinteticos"] = datos["origen"].toString() == "sintetico";
    r["avisos"] = avisosBase(datos);
    const modelos::FormatoExportacion* f = modelos::formatoExportacion(m["modelo"].toObject()["clave"].toString());
    if (!QFile::exists(dir.filePath(ARCHIVO_MODELO))) {
        r["motivo"] = "La corrida no guardó modelo.bin";
        return r;
    }
    if (!f) {
        r["motivo"] = "El modelo de esta corrida no tiene un formato exportable (la DLL de prueba no guarda un modelo real)";
        return r;
    }
    r["exportable"] = true;
    r["formato"] = f->formato;
    r["archivo"] = f->archivo;
    return r;
}

QJsonObject huellaDataset(const QJsonObject& metadata, const QString& rutaHuellas)
{
    const QJsonObject datos = metadata["datos"].toObject();
    QJsonObject h{{"sha256_dataset", QJsonValue::Null}, {"sha256_origen", QJsonValue::Null}};
    if (datos["sha256"].isString() && !datos["sha256"].toString().isEmpty()) {
        h["sha256_dataset"] = datos["sha256"];
        h["sha256_origen"] = "corrida";
        return h;
    }
    if (datos["origen"].toString() != "csv") return h;
    const QString ruta = datos["ruta"].toString();
    const QFileInfo fi(ruta);
    if (ruta.isEmpty() || !fi.exists()) return h;
    // Cache de la cola de subidas: misma ruta, tamano y fecha de modificacion
    const QString fecha = fi.lastModified().toUTC().toString(Qt::ISODateWithMs);
    const QJsonObject cache = leerJson(rutaHuellas)[ruta].toObject();
    QString sha;
    if (!rutaHuellas.isEmpty() && cache["tam"].toInteger(-1) == fi.size() && cache["fecha"].toString() == fecha)
        sha = cache["sha256"].toString();
    if (sha.isEmpty()) {
        QString error;
        sha = PaqueteCorrida::huellaArchivo(ruta, &error);
    }
    if (sha.isEmpty()) return h;
    h["sha256_dataset"] = sha;
    h["sha256_origen"] = "calculada_al_exportar";
    return h;
}

QJsonValue resumenMetricasModelo(const QJsonObject& mm)
{
    if (mm.isEmpty()) return QJsonValue(QJsonValue::Null);
    QJsonObject r;
    r["archivo"] = ARCHIVO_METRICAS;
    for (const char* conjunto : {"validacion", "entrenamiento"}) {
        const QJsonObject c = mm[conjunto].toObject();
        const QJsonObject macro = c["macro"].toObject();
        auto valor = [](const QJsonValue& v) { return v.isDouble() ? v : QJsonValue(QJsonValue::Null); };
        QJsonObject s;
        s["exactitud"] = valor(c["exactitud"]);
        s["f1_macro"] = valor(macro["f1"]);
        s["precision_macro"] = valor(macro["precision"]);
        s["recall_macro"] = valor(macro["recall"]);
        s["motivo"] = c["motivo"].isString() ? c["motivo"] : QJsonValue(QJsonValue::Null);
        r[conjunto] = s;
    }
    return r;
}

QJsonObject modeloJson(const QJsonObject& m, const QJsonObject& huella, const QDateTime& exportado,
                       const QJsonObject& metricasModelo)
{
    const QJsonObject modelo = m["modelo"].toObject();
    const QJsonObject datos = m["datos"].toObject();
    const modelos::FormatoExportacion* f = modelos::formatoExportacion(modelo["clave"].toString());
    const bool sinteticos = datos["origen"].toString() == "sintetico";

    QJsonObject j;
    j["version_exportacion"] = VERSION_EXPORTACION;
    j["descripcion"] = "Modelo exportado de EnergIA (D-013). No se puede recargar dentro de EnergIA; ver LEEME.txt.";
    j["formato"] = f ? QJsonValue(f->formato) : QJsonValue(QJsonValue::Null);
    j["archivo_modelo"] = f ? QJsonValue(f->archivo) : QJsonValue(QJsonValue::Null);
    j["libreria"] = modelo["libreria"];
    j["version_contrato"] = modelo["version_contrato"];
    j["modelo"] = QJsonObject{{"clave", modelo["clave"]}, {"nombre", modelo["nombre"]}, {"dll", modelo["dll"]}};
    j["hiperparametros"] = m["hiperparametros"];
    j["datos_sinteticos"] = sinteticos;
    j["columnas_entrada"] = datos["columnas_entrada"].isArray() ? datos["columnas_entrada"] : QJsonValue(QJsonValue::Null);
    j["columnas_generadas"] = datos["columnas_generadas"].isBool() ? datos["columnas_generadas"] : QJsonValue(QJsonValue::Null);
    j["columna_objetivo"] = datos["columna_objetivo"];
    j["indice_objetivo"] = datos.contains("indice_objetivo") ? datos["indice_objetivo"] : QJsonValue(QJsonValue::Null);
    QJsonArray clases;
    const QJsonArray originales = datos["clases_originales"].toArray();
    for (int i = 0; i < originales.size(); ++i) clases.append(QJsonObject{{"indice", i}, {"valor", originales[i]}});
    j["clases_originales"] = clases;
    j["n_cols"] = datos["n_cols"];
    j["n_clases"] = datos["n_clases"];
    const QJsonObject eval = m["evaluacion"].toObject();
    j["metricas"] = eval["ejecutada"].toBool() ? eval["metricas"] : QJsonValue(QJsonValue::Null);
    // Resumen de las metricas del modelo (D-015); el detalle va completo en metricas_modelo.json
    j["metricas_modelo"] = resumenMetricasModelo(metricasModelo);
    j["particion"] = QJsonObject{{"porcentaje_entrenamiento", datos["porcentaje_train"]}, {"semilla", datos["semilla"]},
                                 {"n_train", datos["n_train"]}, {"n_val", datos["n_val"]},
                                 {"metricas_sobre", "validacion"}};
    j["id_corrida"] = m["id_corrida"];
    j["experimento"] = m["experimento"];
    j["t_creacion"] = m["tiempos"].toObject()["t_creacion"];
    j["dataset"] = sinteticos ? QJsonValue(QJsonValue::Null)
                              : QJsonValue(QJsonObject{{"nombre", QFileInfo(datos["ruta"].toString()).fileName()},
                                                       {"n_filas_total", datos["n_filas_total"]}});
    j["sha256_dataset"] = huella["sha256_dataset"];
    j["sha256_origen"] = huella["sha256_origen"];
    j["exportado"] = exportado.toUTC().toString(Qt::ISODateWithMs);

    QJsonArray avisos = avisosBase(datos);
    if (huella["sha256_origen"].toString() == "calculada_al_exportar") avisos.append(aviso("sha_exportar"));
    if (!sinteticos && huella["sha256_dataset"].isNull()) avisos.append(aviso("sin_csv"));
    j["avisos"] = avisos;
    return j;
}

QString leeme(const QJsonObject& j)
{
    const QJsonObject modelo = j["modelo"].toObject();
    QString t;
    t += "MODELO EXPORTADO DE EnergIA\n"
         "===========================\n\n"
         "Corrida: " + j["id_corrida"].toString() + " (creada " + j["t_creacion"].toString() + ")\n"
         "Modelo: " + modelo["nombre"].toString() + " (" + modelo["clave"].toString() + ")\n"
         "Formato: " + j["formato"].toString() + ", archivo " + j["archivo_modelo"].toString() + "\n"
         "Librería: " + j["libreria"].toString() + "\n"
         "Exportado: " + j["exportado"].toString() + "\n\n"
         "Este .zip contiene el modelo, modelo.json (todo lo que hace falta para usarlo) y este archivo.\n"
         "El modelo NO se puede volver a cargar dentro de EnergIA: sirve para usarlo fuera, con la misma\n"
         "librería con que se entrenó. En la carpeta de la corrida el archivo se llama modelo.bin.\n\n";
    const QJsonArray avisos = j["avisos"].toArray();
    if (!avisos.isEmpty()) {
        t += "AVISOS\n------\n";
        for (const QJsonValue& a : avisos) t += "- " + a.toString() + "\n";
        t += "\n";
    }
    if (j["datos_sinteticos"].toBool())
        t += "DATOS DE DESARROLLO: esta corrida usó datos sintéticos generados por EnergIA, no un dataset real.\n"
             "Los nombres de las columnas son generados.\n\n";

    if (j["formato"].toString() == "libtorch_torch_save") t += leemeMlp(j);
    else t += leemeArbol(j);

    t += "\nCÓMO PREPARAR LOS DATOS\n"
         "-----------------------\n"
         "- Una fila por ejemplo con " + valorTexto(j["n_cols"]) + " valores numéricos (n_cols), SIN la columna objetivo.\n";
    if (j["columnas_entrada"].isArray())
        t += "- Las columnas van en este orden (columnas_entrada):\n    " + lista(j["columnas_entrada"]).join(", ") + "\n";
    else
        t += "- columnas_entrada es null (corrida anterior a ese campo): el orden es el del encabezado del CSV\n"
             "  original sin la columna objetivo (\"" + j["columna_objetivo"].toString() + "\").\n";
    t += "- Los valores van tal cual están en el CSV: EnergIA no normaliza ni escala los datos. Con separador\n"
         "  ';' o tabulación, la coma decimal se lee como punto.\n"
         "- El modelo devuelve un índice de clase de 0 a n_clases-1. La etiqueta original es la de ese\n"
         "  índice en clases_originales:\n";
    for (const QJsonValue& c : j["clases_originales"].toArray())
        t += QString("    %1 -> %2\n").arg(c.toObject()["indice"].toInt()).arg(valorTexto(c.toObject()["valor"]));
    t += "\nMÉTRICAS Y PARTICIÓN\n"
         "--------------------\n"
         "Las métricas de modelo.json (metricas) son las de evaluar() sobre la partición de validación:\n"
         "porcentaje de entrenamiento y semilla en particion. sha256_dataset identifica el CSV usado\n"
         "(sha256_origen: \"corrida\" = calculado al preparar los datos; \"calculada_al_exportar\" = calculado\n"
         "al exportar sobre el archivo actual).\n";
    if (j["metricas_modelo"].isObject())
        t += "\nmetricas_modelo.json (en este .zip) tiene las métricas de clasificación del modelo en validación\n"
             "y en entrenamiento: matriz de confusión (filas = clase real, columnas = clase predicha, en el\n"
             "orden de clases_originales), exactitud, precisión, recall, F1 y soporte por clase, y los promedios\n"
             "macro y ponderado. Se calcularon como scikit-learn con zero_division=0: una métrica no definida\n"
             "vale 0 y lleva indefinida: true. modelo.json (metricas_modelo) resume la exactitud y la precisión,\n"
             "el recall y el F1 macro de cada conjunto.\n";
    else
        t += "\nEsta corrida no tiene métricas del modelo (metricas_modelo.json): es anterior a ellas o su\n"
             "entrenamiento no terminó. metricas_modelo es null en modelo.json.\n";
    return t;
}

bool exportar(const QString& dirCorrida, const QString& destinoZip, const QString& rutaHuellas, QJsonObject* modelo,
              QString* error)
{
    const QJsonObject i = info(dirCorrida);
    if (!i["exportable"].toBool()) {
        *error = i["motivo"].toString();
        return false;
    }
    const QDir dir(dirCorrida);
    const QJsonObject m = leerJson(dir.filePath("metadata.json"));
    QFile f(dir.filePath(ARCHIVO_MODELO));
    if (!f.open(QIODevice::ReadOnly)) {
        *error = "No se pudo leer " + QDir::toNativeSeparators(f.fileName()) + ": " + f.errorString();
        return false;
    }
    const QByteArray bin = f.readAll();
    f.close();
    const QDateTime ahora = QDateTime::currentDateTimeUtc();
    // metricas_modelo.json (D-015): resumen en modelo.json y el archivo completo en el .zip
    QFile fm(dir.filePath(ARCHIVO_METRICAS));
    const QByteArray metricas = fm.open(QIODevice::ReadOnly) ? fm.readAll() : QByteArray();
    *modelo = modeloJson(m, huellaDataset(m, rutaHuellas), ahora, QJsonDocument::fromJson(metricas).object());
    // LEEME.txt con fin de linea de Windows (se abre con el Bloc de notas) y BOM UTF-8 por las tildes
    QString texto = leeme(*modelo);
    texto.replace("\n", "\r\n");
    QList<QPair<QString, QByteArray>> entradas = {
        {i["archivo"].toString(), bin},
        {ARCHIVO_JSON, QJsonDocument(*modelo).toJson(QJsonDocument::Indented)},
        {ARCHIVO_LEEME, QByteArray("\xEF\xBB\xBF") + texto.toUtf8()},
    };
    if (!metricas.isEmpty()) entradas.append({ARCHIVO_METRICAS, metricas});
    return zip::escribir(destinoZip, entradas, ahora, error);
}

} // namespace exportacion
