// Trabajador.cpp - ver Trabajador.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "Trabajador.h"

#include "DatosCsv.h"
#include "MetricasModelo.h"
#include "Reloj.h"
#include "energia_comun.h"

#include <QJsonArray>

#include <algorithm>
#include <cstring>
#include <random>

namespace {

// Encabezado de progreso.csv y progreso_inferencia.csv (formato ancho: una fila por paso)
QByteArray encabezadoProgreso()
{
    QByteArray enc = "timestamp_inicio,timestamp_fin,step_index,step_total";
    for (int i = 1; i <= ENERGIA_MAX_METRICAS; ++i) enc += QString(",metrica_%1_nombre,metrica_%1_valor").arg(i).toUtf8();
    return enc + '\n';
}

} // namespace

void Trabajador::preparar(const ConfigCorrida& c)
{
    cancelar_.store(false);
    listo_ = false;
    cfg_ = c;
    liberarDatos();

    QString error;
    QJsonObject resumen;
    std::vector<double> X, Y;
    int nCols = 0;

    if (c.usarSintetico) {
        datos::generarSinteticos(c.semilla, &X, &Y, &nCols, &resumen);
    } else if (!datos::cargarCsv(c.rutaCsv, c.columnaObjetivo, &X, &Y, &nCols, &resumen, &error)) {
        emit preparado(false, error, QJsonObject());
        return;
    }

    // Etiquetas originales -> indices 0 .. n_clases-1
    std::vector<double> clases(Y);
    std::sort(clases.begin(), clases.end());
    clases.erase(std::unique(clases.begin(), clases.end()), clases.end());
    if (clases.size() < 2) { emit preparado(false, "La columna objetivo tiene una sola clase", QJsonObject()); return; }
    if (static_cast<int>(clases.size()) > MAX_CLASES) {
        emit preparado(false, QString("La columna objetivo tiene %1 valores distintos (maximo %2); "
                                      "parece un problema de regresion").arg(clases.size()).arg(MAX_CLASES),
                       QJsonObject());
        return;
    }
    for (double& y : Y) y = static_cast<double>(std::lower_bound(clases.begin(), clases.end(), y) - clases.begin());

    // Particion reproducible: Fisher-Yates con mt19937 (mismo resultado en MinGW y MSVC)
    const int64_t n = static_cast<int64_t>(Y.size());
    if (n < 2) { emit preparado(false, "Se necesitan al menos 2 filas", QJsonObject()); return; }
    std::vector<int64_t> idx(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) idx[i] = i;
    std::mt19937 rng(c.semilla);
    for (int64_t i = n - 1; i > 0; --i) std::swap(idx[i], idx[rng() % static_cast<uint32_t>(i + 1)]);

    int64_t nTrain = (n * c.porcentajeTrain + 50) / 100;
    nTrain = std::max<int64_t>(1, std::min<int64_t>(n - 1, nTrain));

    auto copiar = [&](int64_t desde, int64_t hasta, std::vector<double>* xd, std::vector<double>* yd) {
        xd->reserve(static_cast<size_t>((hasta - desde) * nCols));
        yd->reserve(static_cast<size_t>(hasta - desde));
        for (int64_t k = desde; k < hasta; ++k) {
            const int64_t fila = idx[k];
            xd->insert(xd->end(), X.begin() + fila * nCols, X.begin() + (fila + 1) * nCols);
            yd->push_back(Y[fila]);
        }
    };
    copiar(0, nTrain, &Xtrain_, &Ytrain_);
    copiar(nTrain, n, &Xval_, &Yval_);
    nCols_ = nCols;
    nClases_ = static_cast<int32_t>(clases.size());

    // progreso.csv (formato ancho: una fila por paso)
    if (archivo_.isOpen()) archivo_.close();
    archivo_.setFileName(c.rutaProgreso);
    if (!archivo_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit preparado(false, "No se pudo crear " + c.rutaProgreso, QJsonObject());
        return;
    }
    archivo_.write(encabezadoProgreso());
    archivo_.flush();

    QJsonArray jClases;
    for (double v : clases) jClases.append(v);
    resumen["n_filas_total"] = static_cast<qint64>(n);
    resumen["n_cols"] = nCols;
    resumen["n_train"] = static_cast<qint64>(nTrain);
    resumen["n_val"] = static_cast<qint64>(n - nTrain);
    resumen["porcentaje_train"] = c.porcentajeTrain;
    resumen["semilla"] = static_cast<qint64>(c.semilla);
    resumen["n_clases"] = nClases_;
    resumen["clases_originales"] = jClases;  // la posicion en la lista es el indice 0..n-1

    listo_ = true;
    emit preparado(true, QString(), resumen);
}

void Trabajador::ejecutar()
{
    if (!listo_) { emit terminado(ENERGIA_ERR_INTERNO, 0, 0, 0, 0); return; }

    EnergiaDatos train{Xtrain_.data(), Ytrain_.data(), static_cast<int64_t>(Ytrain_.size()), nCols_};
    EnergiaDatos val{Xval_.data(), Yval_.data(), static_cast<int64_t>(Yval_.size()), nCols_};
    EnergiaParams params = cfg_.params;
    params.n_clases = nClases_;
    params.semilla = cfg_.semilla;
    int64_t modeloId = 0;
    tInicioPaso_ = 0;
    pasosRegistrados_ = 0;

    // ---- Frontera temporal: nada entre las marcas y la llamada ----
    const qint64 tAntes = reloj::ahoraMs();
    const int32_t status = cfg_.fn.entrenar(&train, &val, &params, &Trabajador::callback, this, &modeloId);
    const qint64 tDespues = reloj::ahoraMs();
    // ---------------------------------------------------------------

    archivo_.close();
    listo_ = false;
    emit terminado(status, tAntes, tDespues, static_cast<qint64>(modeloId), pasosRegistrados_);
}

void Trabajador::inferir(qint64 modeloId, int lote, qint64 duracionMinMs, const QString& rutaProgreso)
{
    const qint64 nFilas = static_cast<qint64>(Yval_.size());
    // progreso_inferencia.csv se abre antes de t_inf_antes: no entra en la medicion
    if (archivoInferencia_.isOpen()) archivoInferencia_.close();
    archivoInferencia_.setFileName(rutaProgreso);
    if (!archivoInferencia_.open(QIODevice::WriteOnly | QIODevice::Truncate) || !cfg_.fn.inferir) {
        emit inferenciaTerminada(ENERGIA_ERR_INTERNO, 0, 0, 0, 0, 0, nFilas);
        return;
    }
    archivoInferencia_.write(encabezadoProgreso());
    archivoInferencia_.flush();

    // La inferencia usa la particion de validacion, la misma de evaluar (D-014). Y se ignora.
    EnergiaDatos val{Xval_.data(), Yval_.data(), nFilas, nCols_};
    EnergiaParamsInferencia params;
    std::memset(&params, 0, sizeof(params));
    params.lote = lote;
    params.paso_min_ms = ENERGIA_INFERENCIA_PASO_MIN_MS;
    int64_t nPredicciones = 0;
    duracionMinInferenciaMs_ = duracionMinMs;
    pasadasInferencia_ = 0;
    gruposInferencia_ = 0;
    tInicioPaso_ = 0;

    // ---- Frontera temporal: nada entre las marcas y la llamada ----
    tInfAntes_ = reloj::ahoraMs();
    const int32_t status = cfg_.fn.inferir(modeloId, &val, &params, &Trabajador::callbackInferencia, this, &nPredicciones);
    const qint64 tInfDespues = reloj::ahoraMs();
    // ---------------------------------------------------------------

    archivoInferencia_.close();
    // Pasadas completas segun la DLL (n_predicciones = filas x pasadas completas): tambien cuenta las
    // de un grupo interrumpido por un error, que no llego a avisar su END
    const qint64 pasadas = nFilas > 0 ? static_cast<qint64>(nPredicciones) / nFilas : 0;
    emit inferenciaTerminada(status, tInfAntes_, tInfDespues, static_cast<qint64>(nPredicciones), pasadas,
                             gruposInferencia_, nFilas);
}

void Trabajador::cerrarModelo(qint64 modeloId, bool evaluar, bool guardar, const QString& ruta)
{
    if (archivo_.isOpen()) archivo_.close();  // corrida cancelada antes de entrenar
    if (archivoInferencia_.isOpen()) archivoInferencia_.close();
    int stGuardar = -1;
    int stLiberar = -1;
    QJsonObject evaluacion, metricas;
    evaluacion["ejecutada"] = false;
    if (modeloId > 0) {
        if (evaluar && cfg_.fn.evaluar) {
            EnergiaDatos val{Xval_.data(), Yval_.data(), static_cast<int64_t>(Yval_.size()), nCols_};
            EnergiaMetrica metricas[ENERGIA_MAX_METRICAS];
            std::memset(metricas, 0, sizeof(metricas));
            int32_t n = 0;
            const qint64 t0 = reloj::ahoraMs();
            const int32_t st = cfg_.fn.evaluar(modeloId, &val, metricas, ENERGIA_MAX_METRICAS, &n);
            const qint64 t1 = reloj::ahoraMs();
            QJsonObject jm;
            n = std::max(0, std::min<int32_t>(n, ENERGIA_MAX_METRICAS));
            for (int i = 0; i < n; ++i)
                jm[QString::fromStdString(energia::leerTexto(metricas[i].nombre))] = metricas[i].valor;
            evaluacion["ejecutada"] = true;
            evaluacion["status"] = st;
            evaluacion["datos"] = "validacion";
            evaluacion["n_filas"] = static_cast<qint64>(Yval_.size());
            evaluacion["duracion_ms"] = t1 - t0;
            evaluacion["metricas"] = jm;
        }
        // Metricas del modelo (D-015): en las mismas corridas que evaluar, antes de soltar los datos
        if (evaluar) {
            metricas["validacion"] = metricasConjunto(modeloId, Xval_, Yval_);
            metricas["entrenamiento"] = metricasConjunto(modeloId, Xtrain_, Ytrain_);
        }
        if (guardar && cfg_.fn.guardar) stGuardar = cfg_.fn.guardar(modeloId, ruta.toUtf8().constData());
        if (cfg_.fn.liberar) stLiberar = cfg_.fn.liberar(modeloId);
    }
    liberarDatos();
    emit modeloCerrado(stGuardar, stLiberar, evaluacion, metricas);
}

QJsonObject Trabajador::metricasConjunto(qint64 modeloId, const std::vector<double>& X, const std::vector<double>& Y) const
{
    const int64_t n = static_cast<int64_t>(Y.size());
    QJsonObject j;
    const qint64 t0 = reloj::ahoraMs();
    // Sin predecir (no deberia pasar: el catalogo la exige) se informa como un error interno
    int32_t st = ENERGIA_ERR_INTERNO;
    std::vector<int32_t> predicciones;
    if (cfg_.fn.predecir && n > 0) {
        predicciones.assign(static_cast<size_t>(n), -1);
        const EnergiaDatos datos{X.data(), nullptr, n, nCols_};  // predecir ignora Y
        st = cfg_.fn.predecir(modeloId, &datos, metricas::LOTE_PREDECIR, predicciones.data());
    } else if (n == 0) {
        st = ENERGIA_ERR_DATOS;
    }
    const qint64 t1 = reloj::ahoraMs();
    if (st == ENERGIA_OK) {
        QJsonArray clases;
        for (int i = 0; i < nClases_; ++i) clases.append(i);  // el orquestador pone las etiquetas originales
        j = metricas::calcular(predicciones.data(), Y.data(), n, nClases_, clases);
    }
    const qint64 t2 = reloj::ahoraMs();
    j["status_dll"] = st;
    j["n_filas"] = static_cast<qint64>(n);
    j["duracion_predecir_ms"] = t1 - t0;
    j["duracion_ms"] = t2 - t0;
    return j;
}

int32_t Trabajador::callback(const EnergiaProgreso* p, void* userData)
{
    if (!p || !userData) return ENERGIA_CONTINUAR;
    return static_cast<Trabajador*>(userData)->alProgreso(p);
}

QByteArray Trabajador::filaProgreso(qint64 tFinMs, const EnergiaProgreso* p, QJsonObject* metricas) const
{
    QByteArray linea = reloj::isoUtc(tInicioPaso_) + ',' + reloj::isoUtc(tFinMs) + ','
                     + QByteArray::number(p->step_index) + ',' + QByteArray::number(p->step_total);
    const int n = std::max(0, std::min<int>(p->n_metricas, ENERGIA_MAX_METRICAS));
    for (int i = 0; i < ENERGIA_MAX_METRICAS; ++i) {
        if (i < n) {
            char nombre[ENERGIA_LEN_NOMBRE + 1];
            std::memcpy(nombre, p->metricas[i].nombre, ENERGIA_LEN_NOMBRE);
            nombre[ENERGIA_LEN_NOMBRE] = '\0';  // defensa si la DLL no termino la cadena
            QByteArray nom(nombre);
            nom.replace(',', '_').replace('\n', '_').replace('\r', '_');
            const QByteArray val = QByteArray::number(p->metricas[i].valor, 'g', 12);
            linea += ',' + nom + ',' + val;
            (*metricas)[QString::fromUtf8(nom)] = p->metricas[i].valor;
        } else {
            linea += ",,";
        }
    }
    return linea + '\n';
}

int32_t Trabajador::alProgreso(const EnergiaProgreso* p)
{
    const qint64 ts = reloj::ahoraMs();  // primero el tiempo, despues todo lo demas

    QJsonObject metricas;
    if (p->fase == ENERGIA_FASE_START) {
        tInicioPaso_ = ts;
    } else if (p->fase == ENERGIA_FASE_END) {
        archivo_.write(filaProgreso(ts, p, &metricas));
        archivo_.flush();
        ++pasosRegistrados_;
    }
    emit progreso(p->fase, p->step_index, p->step_total, ts, metricas);
    return cancelar_.load() ? ENERGIA_DETENER : ENERGIA_CONTINUAR;
}

int32_t Trabajador::callbackInferencia(const EnergiaProgreso* p, void* userData)
{
    if (!p || !userData) return ENERGIA_CONTINUAR;
    return static_cast<Trabajador*>(userData)->alProgresoInferencia(p);
}

int32_t Trabajador::alProgresoInferencia(const EnergiaProgreso* p)
{
    const qint64 ts = reloj::ahoraMs();  // primero el tiempo, despues todo lo demas
    if (p->fase == ENERGIA_FASE_START) {
        tInicioPaso_ = ts;
        return cancelar_.load() ? ENERGIA_DETENER : ENERGIA_CONTINUAR;
    }
    if (p->fase != ENERGIA_FASE_END) return cancelar_.load() ? ENERGIA_DETENER : ENERGIA_CONTINUAR;

    QJsonObject metricas;
    archivoInferencia_.write(filaProgreso(ts, p, &metricas));
    archivoInferencia_.flush();
    ++gruposInferencia_;
    const qint64 pasadasGrupo = static_cast<qint64>(metricas["pasadas"].toDouble());
    pasadasInferencia_ += pasadasGrupo;
    emit progresoInferencia(p->step_index, ts, pasadasGrupo, pasadasInferencia_,
                            pasadasInferencia_ * static_cast<qint64>(Yval_.size()));
    if (cancelar_.load()) return ENERGIA_DETENER;
    // Regla de duracion (D-014): el primer END con al menos la duracion minima desde t_inf_antes
    return ts - tInfAntes_ >= duracionMinInferenciaMs_ ? ENERGIA_FINALIZAR : ENERGIA_CONTINUAR;
}

void Trabajador::liberarDatos()
{
    std::vector<double>().swap(Xtrain_);
    std::vector<double>().swap(Ytrain_);
    std::vector<double>().swap(Xval_);
    std::vector<double>().swap(Yval_);
}
