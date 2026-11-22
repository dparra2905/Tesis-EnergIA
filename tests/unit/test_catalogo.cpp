// test_catalogo.cpp - Pruebas de CatalogoModelos (descubrimiento de modelos, D-009).
//
// Arma directorios temporales con DLL validas e invalidas y comprueba que el catalogo
// describe la DLL de prueba, descarta las invalidas sin detenerse y genera JSON valido.

#include "CatalogoModelos.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    if (!condicion) ++g_fallos;
}

bool copiar(const QString& origen, const QDir& destino, const QString& nombre)
{
    return QFile::copy(origen, destino.filePath(nombre));
}

QString motivoDe(const CatalogoModelos& c, const QString& archivo)
{
    for (const DllDescartada& d : c.descartadas())
        if (QFileInfo(d.ruta).fileName() == archivo) return d.motivo;
    return QString();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QString dllPrueba = QString::fromUtf8(ENERGIA_DLL_PRUEBA);
    const QString dllIncompleta = QString::fromUtf8(ENERGIA_DLL_INCOMPLETA);
    const QString dllVersionVieja = QString::fromUtf8(ENERGIA_DLL_VERSION_VIEJA);
    const QString dllSinPredecir = QString::fromUtf8(ENERGIA_DLL_SIN_PREDECIR);

    // 1. Directorio inexistente
    {
        CatalogoModelos c;
        comprobar(!c.explorar(QDir::temp().filePath("energia_no_existe_12345")), "directorio inexistente: explorar() devuelve false");
    }

    // 2. Solo la DLL de prueba (valida)
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        comprobar(copiar(dllPrueba, dir, "EnergIA_Prueba.dll"), "se copia la DLL de prueba al directorio temporal");
        CatalogoModelos c;
        comprobar(c.explorar(dir.path()), "explorar() devuelve true");
        comprobar(c.modelos().size() == 1 && c.descartadas().isEmpty(), "un modelo y ninguna DLL descartada");
        if (c.modelos().size() == 1) {
            const ModeloDisponible& m = c.modelos().first();
            comprobar(QString::fromLatin1(m.info.clave) == "prueba", "clave 'prueba' leida desde la DLL");
            comprobar(m.interno, "la DLL de prueba queda marcada como interna (no es modelo del alcance, D-009)");
            comprobar(m.hiper.size() == 5, "5 hiperparametros obtenidos dinamicamente");
            QSet<int32_t> tipos;
            for (const EnergiaHiperparametro& h : m.hiper) tipos.insert(h.tipo);
            comprobar(tipos.size() == 4, "los 4 tipos de hiperparametro estan presentes");
        }
    }

    // 3. Directorio mixto: valida, falsa, incompleta, version vieja, v4 sin predecir (D-015), repetida y
    //    ajena al patron
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        copiar(dllPrueba, dir, "EnergIA_Prueba.dll");
        copiar(dllPrueba, dir, "EnergIA_PruebaCopia.dll");  // misma clave -> repetida
        copiar(dllIncompleta, dir, "EnergIA_Incompleta.dll");
        copiar(dllVersionVieja, dir, "EnergIA_VersionVieja.dll");
        copiar(dllSinPredecir, dir, "EnergIA_SinPredecir.dll");
        copiar(dllPrueba, dir, "otra.dll");                   // no coincide con EnergIA_* -> se ignora
        QFile falsa(dir.filePath("EnergIA_Falsa.dll"));
        comprobar(falsa.open(QIODevice::WriteOnly) && falsa.write("esto no es una DLL") > 0,
                  "se crea un archivo EnergIA_Falsa.dll que no es una DLL");
        falsa.close();

        CatalogoModelos c;
        comprobar(c.explorar(dir.path()), "directorio mixto: la exploracion termina sin detenerse");
        comprobar(c.modelos().size() == 1, "solo queda un modelo valido");
        comprobar(c.descartadas().size() == 5, QString("5 DLL descartadas (obtenidas: %1)").arg(c.descartadas().size()));
        comprobar(motivoDe(c, "EnergIA_Falsa.dll").startsWith("no se pudo cargar"),
                  "archivo que no es DLL: '" + motivoDe(c, "EnergIA_Falsa.dll").left(60) + "'");
        comprobar(motivoDe(c, "EnergIA_Incompleta.dll").contains("no exporta todas las funciones"),
                  "DLL incompleta: '" + motivoDe(c, "EnergIA_Incompleta.dll") + "'");
        comprobar(motivoDe(c, "EnergIA_VersionVieja.dll") == QString::fromUtf8("contrato versión 3, el orquestador usa la 4"),
                  "DLL con contrato v3 descartada: '" + motivoDe(c, "EnergIA_VersionVieja.dll") + "'");
        comprobar(motivoDe(c, "EnergIA_SinPredecir.dll")
                      == QString::fromUtf8("no exporta predecir: recompila la DLL con el contrato v4 actual"),
                  "DLL v4 compilada antes de predecir descartada: '" + motivoDe(c, "EnergIA_SinPredecir.dll") + "'");
        comprobar(motivoDe(c, "EnergIA_PruebaCopia.dll").contains("repetida"),
                  "clave repetida: '" + motivoDe(c, "EnergIA_PruebaCopia.dll") + "'");
        comprobar(motivoDe(c, "otra.dll").isEmpty(), "un archivo fuera del patron EnergIA_* se ignora");

        // 4. JSON generado
        QJsonParseError err;
        const QByteArray bytes = QJsonDocument(c.aJson()).toJson();
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
        comprobar(err.error == QJsonParseError::NoError && doc.isObject(), "el catalogo se serializa a JSON valido");
        const QJsonObject raiz = doc.object();
        comprobar(raiz["version_contrato"].toInt() == 4, "JSON: version_contrato = 4");
        const QJsonArray modelos = raiz["modelos"].toArray();
        comprobar(modelos.size() == 1 && raiz["descartadas"].toArray().size() == 5, "JSON: 1 modelo y 5 descartadas");
        if (modelos.size() == 1) {
            const QJsonObject m = modelos.first().toObject();
            comprobar(m["clave"].toString() == "prueba" && m["interno"].toBool(), "JSON: modelo 'prueba' marcado como interno");
            const QJsonArray hiper = m["hiperparametros"].toArray();
            QSet<QString> tipos;
            for (const QJsonValue& h : hiper) tipos.insert(h.toObject()["tipo"].toString());
            comprobar(hiper.size() == 5 && tipos == QSet<QString>{"entero", "real", "opcion", "lista_enteros"},
                      "JSON: 5 hiperparametros con los tipos entero, real, opcion y lista_enteros");
            bool opcionesOk = false;
            for (const QJsonValue& h : hiper)
                if (h.toObject()["tipo"].toString() == "opcion")
                    opcionesOk = h.toObject()["opciones"].toArray().size() == 2;
            comprobar(opcionesOk, "JSON: el hiperparametro de opcion lista sus 2 opciones");
        }
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "CATALOGO OK" : "CATALOGO CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
