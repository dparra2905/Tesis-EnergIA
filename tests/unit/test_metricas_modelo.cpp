// test_metricas_modelo.cpp - Metricas de clasificacion del modelo (D-015, MetricasModelo.h).
//
// Compara metricas::calcular con valores calculados a mano, que son los mismos que da scikit-learn
// (precision_recall_fscore_support con labels = todas las clases y zero_division=0, version >= 1.3):
//   - caso binario sin divisiones por cero;
//   - caso multiclase con una clase que nunca se predice (precision indefinida, F1 = 0 definido) y
//     otra que ni aparece ni se predice (las tres indefinidas), incluida en los promedios;
//   - una prediccion fuera de rango: cuenta como error, no entra en la matriz.

#include "MetricasModelo.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const std::string& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.c_str());
    if (!condicion) ++g_fallos;
}

bool cerca(double a, double b) { return std::fabs(a - b) < 1e-12; }

bool matrizIgual(const QJsonArray& m, const std::vector<std::vector<int>>& esperada)
{
    if (m.size() != static_cast<int>(esperada.size())) return false;
    for (size_t i = 0; i < esperada.size(); ++i) {
        const QJsonArray fila = m[static_cast<int>(i)].toArray();
        if (fila.size() != static_cast<int>(esperada[i].size())) return false;
        for (size_t j = 0; j < esperada[i].size(); ++j)
            if (fila[static_cast<int>(j)].toInteger() != esperada[i][j]) return false;
    }
    return true;
}

// Metrica de una clase: valor e indefinida
bool metrica(const QJsonObject& clase, const char* nombre, double valor, bool indefinida)
{
    const QJsonObject m = clase[nombre].toObject();
    return cerca(m["valor"].toDouble(-1), valor) && m["indefinida"].toBool(!indefinida) == indefinida;
}

bool promedio(const QJsonObject& p, double precision, double recall, double f1)
{
    return cerca(p["precision"].toDouble(-1), precision) && cerca(p["recall"].toDouble(-1), recall)
           && cerca(p["f1"].toDouble(-1), f1);
}

} // namespace

int main()
{
    // ---- Binario ----
    {
        const std::vector<double> y = {0, 0, 0, 0, 0, 1, 1, 1};
        const std::vector<int32_t> pred = {0, 0, 0, 1, 1, 1, 1, 0};
        const QJsonObject r = metricas::calcular(pred.data(), y.data(), 8, 2, QJsonArray{3.0, 7.0});
        comprobar(r["n_filas"].toInteger() == 8 && matrizIgual(r["matriz_confusion"].toArray(), {{3, 2}, {1, 2}}),
                  "binario: matriz [[3,2],[1,2]] (filas = clase real, columnas = predicha)");
        comprobar(cerca(r["exactitud"].toDouble(), 5.0 / 8), "binario: exactitud 5/8");
        const QJsonArray pc = r["por_clase"].toArray();
        const QJsonObject c0 = pc[0].toObject(), c1 = pc[1].toObject();
        comprobar(pc.size() == 2 && c0["indice"].toInt() == 0 && c0["etiqueta"].toDouble() == 3.0
                      && c1["etiqueta"].toDouble() == 7.0 && c0["soporte"].toInteger() == 5 && c1["soporte"].toInteger() == 3,
                  "binario: etiquetas originales y soporte (5 y 3)");
        comprobar(metrica(c0, "precision", 0.75, false) && metrica(c0, "recall", 0.6, false)
                      && metrica(c0, "f1", 2.0 / 3, false),
                  "binario, clase 0: precision 3/4, recall 3/5, F1 2/3");
        comprobar(metrica(c1, "precision", 0.5, false) && metrica(c1, "recall", 2.0 / 3, false)
                      && metrica(c1, "f1", 4.0 / 7, false),
                  "binario, clase 1: precision 1/2, recall 2/3, F1 4/7");
        comprobar(promedio(r["macro"].toObject(), 0.625, (0.6 + 2.0 / 3) / 2, 13.0 / 21),
                  "binario: macro precision 0.625, recall 0.6333, F1 13/21");
        comprobar(promedio(r["ponderado"].toObject(), 0.65625, 0.625, 106.0 / 168),
                  "binario: ponderado precision 0.65625, recall 0.625, F1 106/168");
        comprobar(r["predicciones_fuera_de_rango"].toInteger() == 0, "binario: ninguna prediccion fuera de rango");
    }

    // ---- Multiclase con division por cero ----
    {
        // La clase 2 nunca se predice; la clase 3 ni aparece ni se predice
        const std::vector<double> y = {0, 0, 1, 1, 1, 2, 2, 2, 2, 0};
        const std::vector<int32_t> pred = {0, 1, 1, 1, 0, 0, 0, 1, 0, 0};
        const QJsonObject r = metricas::calcular(pred.data(), y.data(), 10, 4, QJsonArray{"a", "b", "c", "d"});
        comprobar(matrizIgual(r["matriz_confusion"].toArray(), {{2, 1, 0, 0}, {1, 2, 0, 0}, {3, 1, 0, 0}, {0, 0, 0, 0}}),
                  "multiclase: matriz 4x4 con la clase 3 vacia");
        comprobar(cerca(r["exactitud"].toDouble(), 0.4), "multiclase: exactitud 4/10");
        const QJsonArray pc = r["por_clase"].toArray();
        comprobar(pc.size() == 4 && pc[3].toObject()["etiqueta"].toString() == "d", "multiclase: 4 clases con su etiqueta");
        comprobar(metrica(pc[0].toObject(), "precision", 1.0 / 3, false) && metrica(pc[0].toObject(), "recall", 2.0 / 3, false)
                      && metrica(pc[0].toObject(), "f1", 4.0 / 9, false),
                  "multiclase, clase 0: precision 1/3, recall 2/3, F1 4/9");
        comprobar(metrica(pc[1].toObject(), "precision", 0.5, false) && metrica(pc[1].toObject(), "recall", 2.0 / 3, false)
                      && metrica(pc[1].toObject(), "f1", 4.0 / 7, false),
                  "multiclase, clase 1: precision 1/2, recall 2/3, F1 4/7");
        comprobar(metrica(pc[2].toObject(), "precision", 0, true) && metrica(pc[2].toObject(), "recall", 0, false)
                      && metrica(pc[2].toObject(), "f1", 0, false) && pc[2].toObject()["soporte"].toInteger() == 4,
                  "multiclase, clase 2 nunca predicha: precision 0 indefinida; recall y F1 0 definidos");
        comprobar(metrica(pc[3].toObject(), "precision", 0, true) && metrica(pc[3].toObject(), "recall", 0, true)
                      && metrica(pc[3].toObject(), "f1", 0, true) && pc[3].toObject()["soporte"].toInteger() == 0,
                  "multiclase, clase 3 sin filas ni predicciones: las tres indefinidas (0), soporte 0");
        comprobar(promedio(r["macro"].toObject(), 5.0 / 24, 1.0 / 3, 16.0 / 63),
                  "multiclase: macro sobre las 4 clases (precision 5/24, recall 1/3, F1 16/63)");
        comprobar(promedio(r["ponderado"].toObject(), 0.25, 0.4, 64.0 / 210),
                  "multiclase: ponderado por el soporte (precision 0.25, recall 0.4, F1 64/210)");
    }

    // ---- Prediccion fuera de rango (DLL defectuosa) ----
    {
        const std::vector<double> y = {0, 1, 1};
        const std::vector<int32_t> pred = {0, 1, 5};
        const QJsonObject r = metricas::calcular(pred.data(), y.data(), 3, 2, QJsonArray{0.0, 1.0});
        const QJsonObject c1 = r["por_clase"].toArray()[1].toObject();
        comprobar(r["predicciones_fuera_de_rango"].toInteger() == 1 && matrizIgual(r["matriz_confusion"].toArray(), {{1, 0}, {0, 1}})
                      && cerca(r["exactitud"].toDouble(), 2.0 / 3) && c1["soporte"].toInteger() == 2
                      && metrica(c1, "recall", 0.5, false),
                  "fuera de rango: cuenta como error (exactitud 2/3, soporte 2, recall 1/2) y no entra en la matriz");
    }

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "METRICAS OK" : "METRICAS CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
