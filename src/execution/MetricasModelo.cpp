// MetricasModelo.cpp - ver MetricasModelo.h.
#include "MetricasModelo.h"

#include <vector>

namespace metricas {

namespace {

// Una metrica por clase: su valor (0 si no esta definida) y si esta definida
QJsonObject metrica(double numerador, double denominador)
{
    const bool indefinida = denominador <= 0.0;
    return QJsonObject{{"valor", indefinida ? 0.0 : numerador / denominador}, {"indefinida", indefinida}};
}

} // namespace

QJsonObject calcular(const int32_t* predicciones, const double* reales, int64_t n, int32_t nClases,
                     const QJsonArray& clases)
{
    const size_t k = nClases > 0 ? static_cast<size_t>(nClases) : 0;
    std::vector<std::vector<qint64>> matriz(k, std::vector<qint64>(k, 0));
    // Filas de cada clase real con una prediccion fuera de rango: cuentan en su soporte como error
    std::vector<qint64> fuera(k, 0);
    qint64 aciertos = 0, fueraDeRango = 0;
    for (int64_t i = 0; i < n; ++i) {
        const int64_t real = static_cast<int64_t>(reales[i]);
        const int64_t pred = predicciones[i];
        if (pred < 0 || pred >= nClases) {
            ++fueraDeRango;
            if (real >= 0 && real < nClases) ++fuera[static_cast<size_t>(real)];
            continue;
        }
        if (real < 0 || real >= nClases) continue;
        ++matriz[static_cast<size_t>(real)][static_cast<size_t>(pred)];
        if (real == pred) ++aciertos;
    }

    QJsonArray jMatriz, porClase;
    double macroP = 0, macroR = 0, macroF = 0, pondP = 0, pondR = 0, pondF = 0;
    qint64 soporteTotal = 0;
    for (size_t c = 0; c < k; ++c) {
        QJsonArray fila;
        qint64 soporte = fuera[c], predichas = 0;
        for (size_t j = 0; j < k; ++j) {
            fila.append(matriz[c][j]);
            soporte += matriz[c][j];
            predichas += matriz[j][c];
        }
        jMatriz.append(fila);
        const qint64 tp = matriz[c][c];
        const qint64 fp = predichas - tp, fn = soporte - tp;
        const QJsonObject p = metrica(tp, tp + fp);
        const QJsonObject r = metrica(tp, tp + fn);
        const QJsonObject f = metrica(2.0 * tp, 2.0 * tp + fp + fn);
        const double vp = p["valor"].toDouble(), vr = r["valor"].toDouble(), vf = f["valor"].toDouble();
        macroP += vp;
        macroR += vr;
        macroF += vf;
        pondP += vp * soporte;
        pondR += vr * soporte;
        pondF += vf * soporte;
        soporteTotal += soporte;
        porClase.append(QJsonObject{{"indice", static_cast<int>(c)},
                                    {"etiqueta", c < static_cast<size_t>(clases.size()) ? clases[static_cast<int>(c)] : QJsonValue()},
                                    {"precision", p}, {"recall", r}, {"f1", f}, {"soporte", soporte}});
    }
    const double kd = k > 0 ? static_cast<double>(k) : 1.0;
    const double st = soporteTotal > 0 ? static_cast<double>(soporteTotal) : 1.0;

    QJsonObject j;
    j["n_filas"] = static_cast<qint64>(n);
    j["matriz_confusion"] = jMatriz;
    j["exactitud"] = n > 0 ? static_cast<double>(aciertos) / static_cast<double>(n) : 0.0;
    j["por_clase"] = porClase;
    j["macro"] = QJsonObject{{"precision", macroP / kd}, {"recall", macroR / kd}, {"f1", macroF / kd}};
    j["ponderado"] = QJsonObject{{"precision", pondP / st}, {"recall", pondR / st}, {"f1", pondF / st}};
    j["predicciones_fuera_de_rango"] = fueraDeRango;
    return j;
}

} // namespace metricas
