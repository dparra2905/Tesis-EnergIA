// Origen: Back-EnergIA (https://github.com/dparra2905/Back-EnergIA), dlls/energia_mlp.cpp (sin publicar en
// el repositorio de origen: posterior al commit 1ab19f1). Autor original: David Parra. Adaptado para
// EnergIA (Una-Humilde-Tesis): esta cabecera, la ruta de compilacion (src/execution/models/mlp/; antes
// estaba en models/arbol/) y, para el contrato v4, las funciones inferir (D-014) y predecir (D-015).
// Ver docs/ORIGEN_BACKEND.md.
//
// energia_mlp.cpp - EnergIA_MLP.dll: perceptron multicapa (MLP) con libtorch, contrato v4.
//
// La red la construye y entrena libtorch: esta DLL solo traduce el contrato de EnergIA a la
// API de C++ de PyTorch. Se compila con MSVC desde src/execution/models (ver compilar_dlls.bat).
//
// - Un paso por epoca. Una cancelacion se atiende al terminar la epoca en curso.
// - Metricas de la epoca (salen gratis del entrenamiento): train_loss y train_accuracy,
//   calculadas con las salidas que la red ya produjo en cada lote.
// - evaluar(): val_accuracy y val_loss con el modelo final (medidores ya detenidos).
// - inferir(): sin gradientes (NoGradGuard), en modo eval() y con los mismos hilos que el
//   entrenamiento; forward + argmax por bloques de 'lote' filas. Los datos se convierten a float32
//   una vez, antes de la primera pasada (como en entrenar). Las predicciones se descartan.
// - predecir(): NoGradGuard, eval() y forward + argmax por bloques de 'lote' filas (medidores ya
//   detenidos); devuelve la clase de cada fila para las metricas del modelo (D-015).
// - Datos en float32: se convierten una vez, al inicio de entrenar(), antes de la primera epoca.
// - Semilla: fija la inicializacion de pesos y el orden de los lotes. Con 1 hilo el resultado
//   es identico entre corridas; con varios hilos puede variar en los ultimos decimales.

#ifndef ENERGIA_DLL_EXPORTS
#define ENERGIA_DLL_EXPORTS
#endif
#include "energia_dll.h"
#include "energia_comun.h"

#include <torch/torch.h>
#include <torch/version.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

// Opciones en el mismo orden que en la descripcion
constexpr int ACT_RELU = 0, ACT_TANH = 1, ACT_SIGMOIDE = 2;
constexpr int OPT_SGD = 0, OPT_ADAM = 1;

// Filas por bloque al evaluar (limita la memoria con datasets grandes)
constexpr int64_t FILAS_BLOQUE_EVALUAR = 65536;

struct ModeloMlp {
    torch::nn::Sequential red{nullptr};
    int32_t n_clases = 0;
    int32_t n_cols = 0;
    int hilos = 1;  // los que uso el entrenamiento: inferir usa los mismos
};

energia::RegistroModelos<ModeloMlp> g_modelos;

// Hilos que usa libtorch por defecto en esta maquina (lo que significa "0 = automatico").
// Se lee una sola vez, antes de que ninguna corrida lo cambie.
int hilosAutomaticos()
{
    static const int n = torch::get_num_threads();
    return n;
}

const std::vector<EnergiaHiperparametro>& descripcion()
{
    static const std::vector<EnergiaHiperparametro> d = {
        energia::hiperLista("capas", "Capas ocultas", "Neuronas de cada capa oculta, separadas por comas (ej. 64,32).",
                            1, 4096, 8, "64,32"),
        energia::hiperOpcion("activacion", "Activacion", "Funcion de activacion de las capas ocultas.",
                             "relu;tanh;sigmoide", ACT_RELU),
        energia::hiperOpcion("optimizador", "Optimizador",
                             "SGD simple (sin momentum) o Adam con sus parametros por defecto.", "sgd;adam", OPT_ADAM),
        energia::hiperReal("learning_rate", "Learning rate", "Tamano del paso de cada actualizacion de pesos.",
                           1e-6, 1.0, 0.001),
        energia::hiperEntero("batch_size", "Tamano de lote", "Filas por actualizacion de pesos.", 1, 1000000, 256),
        energia::hiperEntero("epocas", "Epocas", "Pasadas completas por los datos de entrenamiento.", 1, 100000, 10),
        energia::hiperEntero("hilos", "Hilos", "Hilos de CPU para libtorch. 0 = automatico (todos los nucleos fisicos).",
                             0, 256, 0),
    };
    return d;
}

torch::nn::Sequential construirRed(int64_t entradas, const std::vector<int64_t>& capas, int activacion, int64_t salidas)
{
    torch::nn::Sequential red;
    int64_t previas = entradas;
    for (int64_t neuronas : capas) {
        red->push_back(torch::nn::Linear(previas, neuronas));
        if (activacion == ACT_TANH) red->push_back(torch::nn::Tanh());
        else if (activacion == ACT_SIGMOIDE) red->push_back(torch::nn::Sigmoid());
        else red->push_back(torch::nn::ReLU());
        previas = neuronas;
    }
    red->push_back(torch::nn::Linear(previas, salidas));  // una salida por clase (logits)
    return red;
}

// Datos del contrato (double, por filas) -> tensores de libtorch (copia)
torch::Tensor aTensorX(const EnergiaDatos* d)
{
    return torch::from_blob(const_cast<double*>(d->X), {d->n_filas, static_cast<int64_t>(d->n_cols)}, torch::kFloat64)
        .to(torch::kFloat32);
}

torch::Tensor aTensorY(const EnergiaDatos* d)
{
    return torch::from_blob(const_cast<double*>(d->Y), {d->n_filas}, torch::kFloat64).to(torch::kInt64);
}

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
    try {
        if (!info) return ENERGIA_ERR_PARAMETROS;
        std::memset(info, 0, sizeof(*info));
        energia::copiarTexto(info->clave, sizeof(info->clave), "mlp");
        energia::copiarTexto(info->nombre, sizeof(info->nombre), "MLP (perceptron multicapa)");
        const std::string libreria = std::string("libtorch ") + TORCH_VERSION + " (CPU)";
        energia::copiarTexto(info->libreria, sizeof(info->libreria), libreria.c_str());
        energia::copiarTexto(info->unidad_paso, sizeof(info->unidad_paso), "epoca");
        info->hilos = hilosAutomaticos();  // con "Hilos" = 0; el valor elegido queda en hiperparametros
        return ENERGIA_OK;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
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

        const std::vector<int64_t> capas = v["capas"].lista;
        const int activacion = static_cast<int>(v["activacion"].entero());
        const int optimizador = static_cast<int>(v["optimizador"].entero());
        const double lr = v["learning_rate"].numero;
        const int64_t lote = v["batch_size"].entero();
        const int32_t epocas = static_cast<int32_t>(v["epocas"].entero());
        const int hilos = static_cast<int>(v["hilos"].entero());

        // Hilos: se fija en cada corrida (el ajuste de libtorch es global al proceso)
        const int automaticos = hilosAutomaticos();
        torch::set_num_threads(hilos > 0 ? hilos : automaticos);

        // Misma semilla -> mismos pesos iniciales y mismo orden de lotes
        torch::manual_seed(params->semilla);

        // ---- Preparacion (dentro de la medicion total, antes de la primera epoca) ----
        const torch::Tensor X = aTensorX(train);
        const torch::Tensor Y = aTensorY(train);
        const int64_t n = train->n_filas;

        auto m = std::make_unique<ModeloMlp>();
        m->n_clases = params->n_clases;
        m->n_cols = train->n_cols;
        m->red = construirRed(train->n_cols, capas, activacion, params->n_clases);
        m->hilos = hilos > 0 ? hilos : automaticos;

        std::unique_ptr<torch::optim::Optimizer> opt;
        if (optimizador == OPT_SGD)
            opt = std::make_unique<torch::optim::SGD>(m->red->parameters(), torch::optim::SGDOptions(lr));
        else
            opt = std::make_unique<torch::optim::Adam>(m->red->parameters(), torch::optim::AdamOptions(lr));

        // ---- Epocas ----
        for (int32_t e = 0; e < epocas; ++e) {
            EnergiaProgreso p;
            std::memset(&p, 0, sizeof(p));
            p.fase = ENERGIA_FASE_START;
            p.step_index = e;
            p.step_total = epocas;
            if (avisar(callback, user_data, p)) return ENERGIA_CANCELADO;

            m->red->train();
            const torch::Tensor orden = torch::randperm(n, torch::kInt64);
            double sumaLoss = 0.0;
            int64_t aciertos = 0;
            for (int64_t ini = 0; ini < n; ini += lote) {
                const int64_t fin = std::min(ini + lote, n);
                const torch::Tensor idx = orden.slice(0, ini, fin);
                const torch::Tensor xb = X.index_select(0, idx);
                const torch::Tensor yb = Y.index_select(0, idx);

                opt->zero_grad();
                const torch::Tensor salida = m->red->forward(xb);
                const torch::Tensor loss = torch::nn::functional::cross_entropy(salida, yb);
                loss.backward();
                opt->step();

                // Gratis: se usan las salidas que ya se calcularon para este lote
                sumaLoss += loss.item<double>() * static_cast<double>(fin - ini);
                aciertos += salida.argmax(1).eq(yb).sum().item<int64_t>();
            }

            std::memset(&p, 0, sizeof(p));
            p.fase = ENERGIA_FASE_END;
            p.step_index = e;
            p.step_total = epocas;
            energia::ponerMetrica(p, "train_loss", sumaLoss / static_cast<double>(n));
            energia::ponerMetrica(p, "train_accuracy", static_cast<double>(aciertos) / static_cast<double>(n));
            if (avisar(callback, user_data, p)) return ENERGIA_CANCELADO;  // m se libera solo
        }

        *modelo_id = g_modelos.agregar(std::move(m));
        return ENERGIA_OK;
    } catch (...) {
        if (modelo_id) *modelo_id = 0;
        return ENERGIA_ERR_INTERNO;  // ninguna excepcion (ni c10::Error) cruza la frontera
    }
}

extern "C" ENERGIA_API int32_t evaluar(int64_t modelo_id, const EnergiaDatos* datos, EnergiaMetrica* metricas,
                                       int32_t max_metricas, int32_t* n_metricas)
{
    try {
        if (n_metricas) *n_metricas = 0;
        if (!metricas || !n_metricas || max_metricas <= 0) return ENERGIA_ERR_PARAMETROS;
        ModeloMlp* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (!energia::datosValidos(datos, m->n_clases) || datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;

        torch::NoGradGuard sinGradientes;
        m->red->eval();
        const torch::Tensor X = aTensorX(datos);
        const torch::Tensor Y = aTensorY(datos);
        const int64_t n = datos->n_filas;
        double sumaLoss = 0.0;
        int64_t aciertos = 0;
        for (int64_t ini = 0; ini < n; ini += FILAS_BLOQUE_EVALUAR) {
            const int64_t fin = std::min(ini + FILAS_BLOQUE_EVALUAR, n);
            const torch::Tensor salida = m->red->forward(X.slice(0, ini, fin));
            const torch::Tensor yb = Y.slice(0, ini, fin);
            sumaLoss += torch::nn::functional::cross_entropy(
                            salida, yb, torch::nn::functional::CrossEntropyFuncOptions().reduction(torch::kSum))
                            .item<double>();
            aciertos += salida.argmax(1).eq(yb).sum().item<int64_t>();
        }
        energia::ponerMetrica(metricas, max_metricas, n_metricas, "val_accuracy",
                              static_cast<double>(aciertos) / static_cast<double>(n));
        energia::ponerMetrica(metricas, max_metricas, n_metricas, "val_loss", sumaLoss / static_cast<double>(n));
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
        ModeloMlp* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;

        torch::set_num_threads(m->hilos);  // los mismos hilos que el entrenamiento
        torch::NoGradGuard sinGradientes;
        m->red->eval();
        const torch::Tensor X = aTensorX(datos);
        const int64_t n = datos->n_filas, lote = params->lote;
        auto unaPasada = [&]() -> int32_t {
            for (int64_t ini = 0; ini < n; ini += lote) {
                const int64_t fin = std::min(ini + lote, n);
                const torch::Tensor prediccion = m->red->forward(X.slice(0, ini, fin)).argmax(1);
                (void)prediccion;  // se calcula y se descarta
            }
            return ENERGIA_OK;
        };
        return energia::inferirEnGrupos(*params, n, callback, user_data, unaPasada, n_predicciones);
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

// Clase predicha de cada fila (D-015), fuera de la medicion: NoGradGuard, eval() y argmax por bloques
// de 'lote' filas
extern "C" ENERGIA_API int32_t predecir(int64_t modelo_id, const EnergiaDatos* datos, int32_t lote,
                                        int32_t* predicciones)
{
    try {
        const int32_t st = energia::validarPrediccion(datos, lote, predicciones);
        if (st != ENERGIA_OK) return st;
        ModeloMlp* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        if (datos->n_cols != m->n_cols) return ENERGIA_ERR_DATOS;

        torch::NoGradGuard sinGradientes;
        m->red->eval();
        const torch::Tensor X = aTensorX(datos);
        const int64_t n = datos->n_filas;
        for (int64_t ini = 0; ini < n; ini += lote) {
            const int64_t fin = std::min<int64_t>(ini + lote, n);
            const torch::Tensor clase = m->red->forward(X.slice(0, ini, fin)).argmax(1).to(torch::kInt64).contiguous();
            const int64_t* p = clase.data_ptr<int64_t>();
            for (int64_t i = ini; i < fin; ++i) predicciones[i] = static_cast<int32_t>(p[i - ini]);
        }
        return ENERGIA_OK;
    } catch (...) {
        return ENERGIA_ERR_INTERNO;
    }
}

// Formato nativo de libtorch (torch::save). Para recargarlo hay que construir la misma red
// (capas, activacion, n_cols, n_clases: quedan en metadata.json) y usar torch::load.
extern "C" ENERGIA_API int32_t guardar_modelo(int64_t modelo_id, const char* ruta_utf8)
{
    try {
        if (!ruta_utf8) return ENERGIA_ERR_PARAMETROS;
        ModeloMlp* m = g_modelos.buscar(modelo_id);
        if (!m) return ENERGIA_ERR_MODELO_INVALIDO;
        std::ofstream f;
        if (!energia::abrirSalida(ruta_utf8, &f)) return ENERGIA_ERR_ARCHIVO;
        torch::save(m->red, f);
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
