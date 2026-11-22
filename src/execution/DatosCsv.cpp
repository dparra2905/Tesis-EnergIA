// DatosCsv.cpp - ver DatosCsv.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "DatosCsv.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

#include <random>

namespace datos {

namespace {

char detectarSeparador(const QByteArray& encabezado)
{
    const int comas = encabezado.count(',');
    const int puntoYComa = encabezado.count(';');
    const int tabs = encabezado.count('\t');
    if (puntoYComa > comas && puntoYComa >= tabs) return ';';
    if (tabs > comas && tabs > puntoYComa) return '\t';
    return ',';
}

QByteArray limpiarCampo(QByteArray campo)
{
    campo = campo.trimmed();
    if (campo.size() >= 2 && campo.startsWith('"') && campo.endsWith('"')) campo = campo.mid(1, campo.size() - 2).trimmed();
    return campo;
}

} // namespace

bool leerEncabezado(const QString& ruta, QStringList* columnas, char* separador, QString* error)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) { *error = "No se pudo abrir " + ruta; return false; }
    QByteArray linea = f.readLine();
    if (linea.startsWith("\xEF\xBB\xBF")) linea = linea.mid(3);  // BOM de Excel
    linea = linea.trimmed();
    if (linea.isEmpty()) { *error = "El CSV no tiene encabezado"; return false; }

    *separador = detectarSeparador(linea);
    columnas->clear();
    for (const QByteArray& c : linea.split(*separador)) columnas->append(QString::fromUtf8(limpiarCampo(c)));
    if (columnas->size() < 2) { *error = "El CSV necesita al menos 2 columnas"; return false; }
    return true;
}

bool cargarCsv(const QString& ruta, int columnaObjetivo, std::vector<double>* X, std::vector<double>* Y,
               int* nCols, QJsonObject* resumen, QString* error)
{
    QStringList columnas;
    char sep = ',';
    if (!leerEncabezado(ruta, &columnas, &sep, error)) return false;
    const int nTotal = columnas.size();
    if (columnaObjetivo < 0 || columnaObjetivo >= nTotal) { *error = "Columna objetivo invalida"; return false; }

    // Huella del archivo (D-013): el sha256 se calcula con los mismos bytes que se leen, sin leer
    // el archivo dos veces. Tamano y fecha de modificacion se toman antes de leerlo.
    const QFileInfo info(ruta);
    const qint64 tam = info.size();
    const QString fecha = info.lastModified().toUTC().toString(Qt::ISODateWithMs);
    QCryptographicHash huella(QCryptographicHash::Sha256);

    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) { *error = "No se pudo abrir " + ruta; return false; }
    huella.addData(f.readLine());  // encabezado

    const bool comaDecimal = (sep != ',');
    std::vector<double> fila(static_cast<size_t>(nTotal));
    int numLinea = 1;
    while (!f.atEnd()) {
        const QByteArray cruda = f.readLine();  // con su fin de linea: la huella es la del archivo
        huella.addData(cruda);
        const QByteArray linea = cruda.trimmed();
        ++numLinea;
        if (linea.isEmpty()) continue;
        const QList<QByteArray> campos = linea.split(sep);
        if (campos.size() != nTotal) {
            *error = QString("Linea %1: tiene %2 columnas y el encabezado tiene %3").arg(numLinea).arg(campos.size()).arg(nTotal);
            return false;
        }
        for (int j = 0; j < nTotal; ++j) {
            QByteArray v = limpiarCampo(campos[j]);
            if (comaDecimal) v.replace(',', '.');
            bool ok = false;
            fila[j] = v.toDouble(&ok);  // independiente de la configuracion regional
            if (!ok) {
                *error = QString("Linea %1, columna '%2': el valor '%3' no es numerico")
                             .arg(numLinea).arg(columnas[j], QString::fromUtf8(campos[j].trimmed()));
                return false;
            }
        }
        for (int j = 0; j < nTotal; ++j) {
            if (j == columnaObjetivo) Y->push_back(fila[j]);
            else X->push_back(fila[j]);
        }
    }
    if (Y->empty()) { *error = "El CSV no tiene filas de datos"; return false; }

    *nCols = nTotal - 1;
    // Columnas de X en el orden en que la DLL las recibe (las del CSV sin la objetivo): hacen
    // falta para usar el modelo fuera de EnergIA (exportacion del modelo, D-013)
    QJsonArray entrada;
    for (int j = 0; j < nTotal; ++j)
        if (j != columnaObjetivo) entrada.append(columnas[j]);
    (*resumen)["origen"] = "csv";
    (*resumen)["ruta"] = QDir::toNativeSeparators(ruta);
    (*resumen)["columna_objetivo"] = columnas[columnaObjetivo];
    (*resumen)["separador"] = (sep == '\t') ? QString("tab") : QString(QChar(sep));
    (*resumen)["columnas_entrada"] = entrada;
    (*resumen)["indice_objetivo"] = columnaObjetivo;
    (*resumen)["columnas_generadas"] = false;
    (*resumen)["sha256"] = QString::fromLatin1(huella.result().toHex());
    (*resumen)["tam_bytes"] = tam;
    (*resumen)["fecha_modificacion"] = fecha;
    return true;
}

void generarSinteticos(uint32_t semilla, std::vector<double>* X, std::vector<double>* Y, int* nCols,
                       QJsonObject* resumen)
{
    const int n = SINTETICO_FILAS, c = SINTETICO_COLS, k = SINTETICO_CLASES;
    std::mt19937 rng(semilla);
    auto unif = [&rng] { return rng() / 4294967296.0; };  // [0, 1), igual en todo compilador
    X->resize(static_cast<size_t>(n) * c);
    Y->resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const int clase = i % k;
        (*Y)[i] = clase;
        for (int j = 0; j < c; ++j) (*X)[static_cast<size_t>(i) * c + j] = clase * 0.5 + (j % 3) * 0.2 + unif();
    }
    *nCols = c;
    // Sin encabezado real: nombres generados x0 .. x{c-1} y la columna "clase" despues de los
    // atributos (columnas_generadas = true)
    QJsonArray entrada;
    for (int j = 0; j < c; ++j) entrada.append(QString("x%1").arg(j));
    (*resumen)["origen"] = "sintetico";
    (*resumen)["columna_objetivo"] = "clase";
    (*resumen)["columnas_entrada"] = entrada;
    (*resumen)["indice_objetivo"] = c;
    (*resumen)["columnas_generadas"] = true;
    // Sin archivo: sin huella
    (*resumen)["sha256"] = QJsonValue::Null;
    (*resumen)["tam_bytes"] = QJsonValue::Null;
    (*resumen)["fecha_modificacion"] = QJsonValue::Null;
}

} // namespace datos
