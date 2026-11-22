// Origen: Back-EnergIA (https://github.com/dparra2905/Back-EnergIA), commit 1ab19f1
// Autor original: David Parra. Adaptado para EnergIA (Una-Humilde-Tesis): esta cabecera y, para el
// contrato v4, las funciones inferir (D-014) y predecir (D-015).
//
// energia_dll_stub.cpp - DLL de PRUEBA (EnergIA_Prueba.dll) que cumple el contrato v4.
//
// No entrena nada real: simula cada paso con carga real de CPU (no con sleep), para que los
// medidores vean senal, y reporta una "train_loss" inventada con forma de curva.
// Declara un hiperparametro de cada tipo (entero, real, opcion y lista) para probar la
// ventana dinamica del orquestador sin instalar mlpack ni libtorch.
// evaluar() si calcula algo real y barato: la exactitud de predecir siempre la clase mas
// frecuente del entrenamiento, para validar que los datos llegan bien.
// inferir() simula la prediccion: en cada bloque lee las columnas de cada fila y "predice" la clase
// mas frecuente. Usa energia::inferirEnGrupos, como las DLL reales. Solo para las pruebas: si la
// variable de entorno ENERGIA_PRUEBA_INFERIR_FALLA vale N > 0, la pasada N (desde 1) devuelve
// ENERGIA_ERR_INTERNO, para probar el error de inferir sin cambiar la autodescripcion.
// predecir() devuelve, para cada fila, la clase mas frecuente del entrenamiento: la misma regla que
// evaluar(), asi la exactitud de las metricas del modelo (D-015) coincide con val_accuracy, y las
// clases que nunca predice quedan con la precision indefinida. Solo para las pruebas: si ENERGIA_PRUEBA_PREDECIR_FALLA
// vale N > 0, la llamada N a predecir (desde 1) devuelve ENERGIA_ERR_INTERNO. El orquestador predice
// primero validacion y despues entrenamiento: con 2 falla solo el entrenamiento.

#ifndef ENERGIA_DLL_EXPORTS
#define ENERGIA_DLL_EXPORTS
#endif
#include "energia_dll.h"
#include "energia_comun.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

struct ModeloFalso {
    int32_t pasos = 0;
    int32_t n_clases = 0;
    int32_t n_cols = 0;
    int64_t n_train = 0;
    int32_t clase_mayoritaria = 0;
    std::string capas;
};

energia::RegistroModelos<ModeloFalso> g_modelos;

const std::vector<EnergiaHiperparametro>& descripcion()
{
    static const std::vector<EnergiaHiperparametro> d = {
        energia::hiperEntero("pasos", "Pasos", "Cuantos pasos simula (como epocas o arboles)", 1, 1000, 10),
        energia::hiperEntero("ms_por_paso", "Duracion por paso (ms)", "Milisegundos de carga real de CPU por paso",
                             50, 60000, 500),
        energia::hiperReal("loss_final", "Loss final", "Valor al que tiende la train_loss simulada", 0.0, 5.0, 0.2),
        energia::hiperOpcion("curva", "Forma de la curva", "Como baja la train_loss simulada", "exponencial;lineal", 0),
        energia::hiperLista("capas", "Capas (prueba)", "Solo para probar el tipo lista; no afecta la simulacion",
                            1, 4096, 8, "64,32"),
    };
    return d;
}

void cargaCpu(int ms)
{
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    volatile double acumulado = 0.0;
    while (std::chrono::steady_clock::now() < fin) {
        for (int i = 1; i < 20000; ++i) acumulado = acumulado + std::sqrt(static_cast<double>(i));
    }
}

// Llama al callback; devuelve true si el orquestador pidio detener
bool avisar(EnergiaProgressFn cb, void* ud, const EnergiaProgreso& p)
{
    return cb && cb(&p, ud) == ENERGIA_DETENER;
}

} // namespace

extern "C" ENERGIA_API int32_t energia_api_version(void)
{
    return ENERGIA_API_VERSION;
}

extern "C" ENERGIA_API int32_t energia_info_modelo(EnergiaInfoModelo* info)
{
    if (!info) return ENERGIA_ERR_PARAMETROS;
    std::memset(info, 0, sizeof(*info));
    energia::copiarTexto(info->clave, sizeof(info->clave), "prueba");
    energia::copiarTexto(info->nombre, sizeof(info->nombre), "Prueba (simulado)");
    energia::copiarTexto(info->libreria, sizeof(info->libreria), "ninguna (carga de CPU simulada)");
    energia::copiarTexto(info->unidad_paso, sizeof(info->unidad_paso), "paso");
    info->hilos = 1;
    return ENERGIA_OK;
}

extern "C" ENERGIA_API int32_t energia_n_hiperparametros(void)
{
    return static_cast<int32_t>(descripcion().size());
}

extern "C" ENERGIA_API int32_t energia_describir_hiperparametro(int32_t i, EnergiaHiperparametro* out)
{
    const auto& d = descripcion();
    if (!out || i < 0 || i >= static_cast<int32_t>(d.size())) return ENERGIA_ERR_PARAMETROS;
    *out = d[static_cast<size_t>(i)];
    return ENERGIA_OK;
}

extern "C" ENERGIA_API int32_t entrenar(const EnergiaDatos* train, const EnergiaDatos* val,
                                        const EnergiaParams* params, EnergiaProgressFn callback,
                                        void* user_data, int64_t* modelo_id)
{
    try {
        if (modelo_id) *modelo_id = 0;
        if (!params || !modelo_id) return ENERGIA_ERR_PARAMETROS;

        energia::ValoresLeidos v;
        if (!energia::validarParams(*params, descripcion(), &v).empty()) return ENERGIA_ERR_PARAMETROS;
        if (params->n_clases < 2) return ENERGIA_ERR_DATOS;
        if (!energia::datosValidos(train, params->n_clases) || !energia::datosValidos(val, params->n_clases))
            return ENERGIA_ERR_DATOS;
        if (train->n_cols != val->n_cols) return ENERGIA_ERR_DATOS;

        const int32_t total = static_cast<int32_t>(v["pasos"].entero());
        const int msPorPaso = static_cast<int>(v["ms_por_paso"].entero());
        const double lossFinal = v["loss_final"].numero;
        const bool lineal = v["curva"].opcion == "lineal";
        const double lossInicial = lossFinal + 1.0;

        for (int32_t i = 0; i < total; ++i) {
            EnergiaProgreso p;
            std::memset(&p, 0, sizeof(p));
            p.fase = ENERGIA_FASE_START;
            p.step_index = i;
            p.step_total = total;
            if (avisar(callback, user_data, p)) return ENERGIA_CANCELADO;

            cargaCpu(msPorPaso);

            const double t = (total > 1) ? static_cast<double>(i) / (total - 1) : 1.0;
            const double avance = lineal ? t : (1.0 - std::exp(-4.0 * t)) / (1.0 - std::exp(-4.0));
            std::memset(&p, 0, sizeof(p));
            p.fase = ENERGIA_FASE_END;
            p.step_index = i;
            p.step_total = total;
            energia::ponerMetrica(p, "train_loss", lossInicial + (lossFinal - lossInicial) * avance);
            if (avisar(callback, user_data, p)) return ENERGIA_CANCELADO;
        }

        // "Modelo": la clase mas frecuente del entrenamiento
        std::vector<int64_t> conteo(static_cast<size_t>(params->n_clases), 0);
        for (int64_t i = 0; i < train->n_filas; ++i) ++conteo[static_cast<size_t>(train->Y[i])];

        auto m = std::make_unique<ModeloFalso>();
        m->pasos = total;
        m->n_clases = params->n_clases;
        m->n_cols = train->n_cols;
        m->n_train = train->n_filas;
        m->clase_mayoritaria = static_cast<int32_t>(std::max_element(conteo.begin(), conteo.end()) - conteo.begin());
        m->capas = v["capas"].texto;
        *modelo_id = g_modelos.agregar(std::move(m));
        return ENERGIA_OK;
    } catch (...) {
        if (modelo_id) *modelo_id = 0;
        return ENERGIA_ERR_INTERNO;  // ninguna excepcion cruza la frontera
    }
}

extern "C" ENERGIA_API int32_t evaluar(int64_t modelo_id, const EnergiaDatos* datos, EnergiaMetrica* metricas,
                                       int32_t max_metricas, int32_t* n_metricas)
{
    try {
        if (n_metricas) *n_metricas = 0;
        if (!metricas || !n_metricas || max_metricas <= 0) return ENERGIA_ERR_PARAMETROS;
        const ModeloFalso* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (!energia::datosValidos(datos, m->n_clases) || datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;

        int64_t aciertos = 0;
        for (int64_t i = 0; i < datos->n_filas; ++i)
            if (static_cast<int32_t>(datos->Y[i]) == m->clase_mayoritaria) ++aciertos;
        energia::ponerMetrica(metricas, max_metricas, n_metricas, "val_accuracy",
                              static_cast<double>(aciertos) / static_cast<double>(datos->n_filas));
        return ENERGIA_OK;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

extern "C" ENERGIA_API int32_t inferir(int64_t modelo_id, const EnergiaDatos* datos,
                                       const EnergiaParamsInferencia* params, EnergiaProgressFn callback,
                                       void* user_data, int64_t* n_predicciones)
{
    try {
        const int32_t st = energia::validarInferencia(datos, params, n_predicciones);
        if (st != ENERGIA_OK) return st;
        const ModeloFalso* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;

        const int64_t n = datos->n_filas, lote = params->lote;
        const int32_t cols = datos->n_cols;
        const char* falla = std::getenv("ENERGIA_PRUEBA_INFERIR_FALLA");
        const long pasadaQueFalla = falla ? std::strtol(falla, nullptr, 10) : 0;
        long pasada = 0;
        std::vector<int32_t> prediccion;
        auto unaPasada = [&]() -> int32_t {
            if (pasadaQueFalla > 0 && ++pasada >= pasadaQueFalla) return ENERGIA_ERR_INTERNO;
            for (int64_t ini = 0; ini < n; ini += lote) {
                const int64_t fin = std::min(ini + lote, n);
                prediccion.assign(static_cast<size_t>(fin - ini), 0);
                for (int64_t i = ini; i < fin; ++i) {
                    volatile double suma = 0.0;  // lee la fila, como un modelo de verdad
                    for (int32_t c = 0; c < cols; ++c) suma = suma + datos->X[i * cols + c];
                    prediccion[static_cast<size_t>(i - ini)] = m->clase_mayoritaria;
                }
            }
            return ENERGIA_OK;  // las predicciones se descartan
        };
        return energia::inferirEnGrupos(*params, n, callback, user_data, unaPasada, n_predicciones);
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

extern "C" ENERGIA_API int32_t predecir(int64_t modelo_id, const EnergiaDatos* datos, int32_t lote,
                                        int32_t* predicciones)
{
    try {
        const int32_t st = energia::validarPrediccion(datos, lote, predicciones);
        if (st != ENERGIA_OK) return st;
        const ModeloFalso* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;
        static std::atomic<long> llamadas{0};
        const char* falla = std::getenv("ENERGIA_PRUEBA_PREDECIR_FALLA");
        const long llamadaQueFalla = falla ? std::strtol(falla, nullptr, 10) : 0;
        if (++llamadas == llamadaQueFalla) return ENERGIA_ERR_INTERNO;

        const int64_t n = datos->n_filas;
        for (int64_t ini = 0; ini < n; ini += lote) {
            const int64_t fin = std::min<int64_t>(ini + lote, n);
            for (int64_t i = ini; i < fin; ++i) predicciones[i] = m->clase_mayoritaria;
        }
        return ENERGIA_OK;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

extern "C" ENERGIA_API int32_t guardar_modelo(int64_t modelo_id, const char* ruta_utf8)
{
    try {
        if (!ruta_utf8) return ENERGIA_ERR_PARAMETROS;
        const ModeloFalso* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        std::ofstream f;
        if (!energia::abrirSalida(ruta_utf8, &f)) return ENERGIA_ERR_ARCHIVO;
        f << "modelo_falso pasos=" << m->pasos << " n_clases=" << m->n_clases << " n_cols=" << m->n_cols
          << " n_train=" << m->n_train << " clase_mayoritaria=" << m->clase_mayoritaria
          << " capas=" << m->capas << "\n";
        f.close();
        return f ? ENERGIA_OK : ENERGIA_ERR_ARCHIVO;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

extern "C" ENERGIA_API int32_t liberar_modelo(int64_t modelo_id)
{
    try {
        return g_modelos.quitar(modelo_id) ? ENERGIA_OK : ENERGIA_ERR_MODELO_INVALIDO;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}