// test_contrato.cpp - Comprueba el contrato de DLL v4 (energia_dll.h) contra EnergIA_Prueba.dll.
//
// Carga la DLL en tiempo de ejecucion, como lo hara el orquestador, y verifica:
//   - que exporta las 10 funciones del contrato (con inferir y predecir) y declara la version 4;
//   - que se autodescribe (5 hiperparametros, uno de cada tipo);
//   - que energia::validarParams acepta los valores por defecto y rechaza uno fuera de rango;
//   - un ciclo minimo entrenar -> inferir -> evaluar -> liberar con callbacks START/END;
//   - inferir (D-014): grupos de pasadas de al menos paso_min_ms (con pasadas de microsegundos,
//     ~duracion pasos y no millones), ENERGIA_FINALIZAR en END y en START, ENERGIA_DETENER con
//     n_predicciones de las pasadas completas, sin callback, y argumentos invalidos;
//   - energia::inferirEnGrupos con una pasada que falla: n_predicciones sin la pasada interrumpida;
//   - predecir (D-015): una clase 0..n_clases-1 por fila, en el orden de las filas, igual con
//     cualquier lote, sin callbacks y coherente con val_accuracy; y argumentos invalidos.

#include "energia_dll.h"
#include "energia_comun.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const std::string& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.c_str());
    if (!condicion) ++g_fallos;
}

#ifdef _WIN32
using Biblioteca = HMODULE;
Biblioteca cargar(const char* rutaUtf8)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, rutaUtf8, -1, nullptr, 0);
    std::wstring ruta(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, rutaUtf8, -1, &ruta[0], n);
    return LoadLibraryW(ruta.c_str());
}
void* simbolo(Biblioteca b, const char* nombre) { return reinterpret_cast<void*>(GetProcAddress(b, nombre)); }
void descargar(Biblioteca b) { FreeLibrary(b); }
#else
using Biblioteca = void*;
Biblioteca cargar(const char* ruta) { return dlopen(ruta, RTLD_NOW); }
void* simbolo(Biblioteca b, const char* nombre) { return dlsym(b, nombre); }
void descargar(Biblioteca b) { dlclose(b); }
#endif

using FnVersion   = int32_t (*)();
using FnInfo      = int32_t (*)(EnergiaInfoModelo*);
using FnNHiper    = int32_t (*)();
using FnDescribir = int32_t (*)(int32_t, EnergiaHiperparametro*);
using FnEntrenar  = int32_t (*)(const EnergiaDatos*, const EnergiaDatos*, const EnergiaParams*,
                                EnergiaProgressFn, void*, int64_t*);
using FnEvaluar   = int32_t (*)(int64_t, const EnergiaDatos*, EnergiaMetrica*, int32_t, int32_t*);
using FnGuardar   = int32_t (*)(int64_t, const char*);
using FnLiberar   = int32_t (*)(int64_t);
using FnInferir   = int32_t (*)(int64_t, const EnergiaDatos*, const EnergiaParamsInferencia*, EnergiaProgressFn,
                                void*, int64_t*);
using FnPredecir  = int32_t (*)(int64_t, const EnergiaDatos*, int32_t, int32_t*);

// Valores por defecto que describe la DLL, en el formato de EnergiaParams
EnergiaParams paramsPorDefecto(const std::vector<EnergiaHiperparametro>& desc)
{
    EnergiaParams p{};
    p.n_clases = 2;
    p.semilla = 42;
    p.n_valores = static_cast<int32_t>(desc.size());
    for (size_t i = 0; i < desc.size(); ++i) {
        EnergiaValor& v = p.valores[i];
        energia::copiarTexto(v.clave, sizeof(v.clave), energia::leerTexto(desc[i].clave).c_str());
        if (desc[i].tipo == ENERGIA_TIPO_LISTA_ENTEROS)
            energia::copiarTexto(v.texto, sizeof(v.texto), energia::leerTexto(desc[i].defecto_texto).c_str());
        else
            v.numero = desc[i].por_defecto;
    }
    return p;
}

EnergiaValor* valor(EnergiaParams& p, const char* clave)
{
    for (int32_t i = 0; i < p.n_valores; ++i)
        if (energia::leerTexto(p.valores[i].clave) == clave) return &p.valores[i];
    return nullptr;
}

struct ConteoCallbacks {
    int start = 0;
    int end = 0;
};

int32_t alProgreso(const EnergiaProgreso* p, void* userData)
{
    auto* c = static_cast<ConteoCallbacks*>(userData);
    if (p->fase == ENERGIA_FASE_START) ++c->start;
    if (p->fase == ENERGIA_FASE_END) ++c->end;
    return ENERGIA_CONTINUAR;
}

// Callback de inferir que imita al orquestador: anota cada grupo y decide en START o END
using Reloj = std::chrono::steady_clock;
struct Inferencia {
    // Regla del orquestador: FINALIZAR en el primer END con (ahora - inicio) >= duracionMinMs
    int64_t duracionMinMs = -1;
    int finalizarEnEnd = -1;     // FINALIZAR en el END de este grupo
    int finalizarEnStart = -1;   // FINALIZAR en el START de este grupo
    int detenerEnEnd = -1;       // DETENER en el END de este grupo
    int detenerEnStart = -1;     // DETENER en el START de este grupo
    Reloj::time_point inicio = Reloj::now(), inicioGrupo;
    int starts = 0, ends = 0;
    bool indicesOk = true, totalCero = true, metricasOk = true;
    int64_t pasadas = 0, predicciones = 0, nFilas = 0;
    double grupoMinimoMs = 1e18;  // duracion del grupo mas corto, START -> END

    double metrica(const EnergiaProgreso* p, const char* nombre) const
    {
        for (int i = 0; i < p->n_metricas; ++i)
            if (energia::leerTexto(p->metricas[i].nombre) == nombre) return p->metricas[i].valor;
        return -1;
    }
};

int32_t alInferir(const EnergiaProgreso* p, void* userData)
{
    auto* c = static_cast<Inferencia*>(userData);
    const Reloj::time_point ahora = Reloj::now();
    if (p->step_total != 0) c->totalCero = false;
    if (p->fase == ENERGIA_FASE_START) {
        if (p->step_index != c->starts) c->indicesOk = false;
        c->inicioGrupo = ahora;
        const int grupo = c->starts++;
        if (grupo == c->detenerEnStart) return ENERGIA_DETENER;
        if (grupo == c->finalizarEnStart) return ENERGIA_FINALIZAR;
        return ENERGIA_CONTINUAR;
    }
    if (p->step_index != c->ends) c->indicesOk = false;
    const int grupo = c->ends++;
    const double pasadas = c->metrica(p, "pasadas"), predicciones = c->metrica(p, "predicciones");
    if (p->n_metricas != 2 || pasadas < 1 || predicciones != pasadas * static_cast<double>(c->nFilas)) c->metricasOk = false;
    c->pasadas += static_cast<int64_t>(pasadas);
    c->predicciones += static_cast<int64_t>(predicciones);
    c->grupoMinimoMs = std::min(c->grupoMinimoMs,
                                std::chrono::duration<double, std::milli>(ahora - c->inicioGrupo).count());
    if (grupo == c->detenerEnEnd) return ENERGIA_DETENER;
    if (grupo == c->finalizarEnEnd) return ENERGIA_FINALIZAR;
    if (c->duracionMinMs >= 0 && ahora - c->inicio >= std::chrono::milliseconds(c->duracionMinMs)) return ENERGIA_FINALIZAR;
    return ENERGIA_CONTINUAR;
}

} // namespace

int main()
{
    std::printf("DLL: %s\n", ENERGIA_DLL_PRUEBA);
    Biblioteca dll = cargar(ENERGIA_DLL_PRUEBA);
    comprobar(dll != nullptr, "la DLL de prueba se carga");
    if (!dll) return 1;

    auto fnVersion   = reinterpret_cast<FnVersion>(simbolo(dll, "energia_api_version"));
    auto fnInfo      = reinterpret_cast<FnInfo>(simbolo(dll, "energia_info_modelo"));
    auto fnNHiper    = reinterpret_cast<FnNHiper>(simbolo(dll, "energia_n_hiperparametros"));
    auto fnDescribir = reinterpret_cast<FnDescribir>(simbolo(dll, "energia_describir_hiperparametro"));
    auto fnEntrenar  = reinterpret_cast<FnEntrenar>(simbolo(dll, "entrenar"));
    auto fnEvaluar   = reinterpret_cast<FnEvaluar>(simbolo(dll, "evaluar"));
    auto fnGuardar   = reinterpret_cast<FnGuardar>(simbolo(dll, "guardar_modelo"));
    auto fnLiberar   = reinterpret_cast<FnLiberar>(simbolo(dll, "liberar_modelo"));
    auto fnInferir   = reinterpret_cast<FnInferir>(simbolo(dll, "inferir"));
    auto fnPredecir  = reinterpret_cast<FnPredecir>(simbolo(dll, "predecir"));
    const bool completa = fnVersion && fnInfo && fnNHiper && fnDescribir && fnEntrenar && fnEvaluar && fnGuardar && fnLiberar
                          && fnInferir && fnPredecir;
    comprobar(completa, "exporta las 10 funciones del contrato (con inferir y predecir)");
    if (!completa) return 1;

    comprobar(fnVersion() == ENERGIA_API_VERSION && fnVersion() == 4, "energia_api_version() == 4");

    EnergiaInfoModelo info{};
    comprobar(fnInfo(&info) == ENERGIA_OK, "energia_info_modelo devuelve OK");
    comprobar(energia::leerTexto(info.clave) == "prueba", "clave del modelo = 'prueba'");

    // Autodescripcion de hiperparametros
    const int32_t n = fnNHiper();
    comprobar(n == 5, "5 hiperparametros (obtenidos: " + std::to_string(n) + ")");
    std::vector<EnergiaHiperparametro> desc;
    std::set<int32_t> tipos;
    for (int32_t i = 0; i < n; ++i) {
        EnergiaHiperparametro h{};
        if (fnDescribir(i, &h) == ENERGIA_OK) {
            desc.push_back(h);
            tipos.insert(h.tipo);
        }
    }
    comprobar(static_cast<int32_t>(desc.size()) == n, "todos los hiperparametros se describen");
    comprobar(tipos == std::set<int32_t>{ENERGIA_TIPO_ENTERO, ENERGIA_TIPO_REAL, ENERGIA_TIPO_OPCION,
                                         ENERGIA_TIPO_LISTA_ENTEROS},
              "hay un hiperparametro de cada tipo (entero, real, opcion, lista)");
    EnergiaHiperparametro fuera{};
    comprobar(fnDescribir(n, &fuera) == ENERGIA_ERR_PARAMETROS, "describir un indice fuera de rango da ERR_PARAMETROS");

    // Validacion compartida (la misma que usaran orquestador y DLL)
    EnergiaParams params = paramsPorDefecto(desc);
    comprobar(energia::validarParams(params, desc, nullptr).empty(), "validarParams acepta los valores por defecto");

    EnergiaParams invalido = params;
    if (EnergiaValor* v = valor(invalido, "pasos")) v->numero = 0;  // rango 1..1000
    const std::string error = energia::validarParams(invalido, desc, nullptr);
    comprobar(!error.empty(), "validarParams rechaza 'pasos' = 0 (" + error + ")");

    // Ciclo minimo: entrenar -> evaluar -> liberar
    const std::vector<double> X = {0.1, 0.2, 0.9, 0.8, 0.2, 0.1, 0.8, 0.9};  // 4 filas x 2 columnas
    const std::vector<double> Y = {0, 1, 0, 1};
    const EnergiaDatos datos{X.data(), Y.data(), 4, 2};
    EnergiaParams corto = params;
    if (EnergiaValor* v = valor(corto, "pasos")) v->numero = 2;
    if (EnergiaValor* v = valor(corto, "ms_por_paso")) v->numero = 50;

    ConteoCallbacks conteo;
    int64_t modeloId = 0;
    comprobar(fnEntrenar(&datos, &datos, &corto, &alProgreso, &conteo, &modeloId) == ENERGIA_OK && modeloId > 0,
              "entrenar devuelve OK y un modelo_id > 0");
    comprobar(conteo.start == 2 && conteo.end == 2, "callbacks START/END una vez por paso (2 pasos)");

    // ---- inferir (D-014) ----
    // Pasadas de microsegundos (4 filas, lote grande): con grupos de >= 1 s y la regla de duracion
    // del orquestador (3 s), salen ~3 grupos, no millones de pasos
    const EnergiaDatos sinY{X.data(), nullptr, 4, 2};  // inferir ignora Y
    EnergiaParamsInferencia pinf{};
    pinf.lote = 65536;
    pinf.paso_min_ms = ENERGIA_INFERENCIA_PASO_MIN_MS;
    {
        Inferencia c;
        c.nFilas = 4;
        c.duracionMinMs = 3000;
        int64_t nPred = -1;
        const int32_t st = fnInferir(modeloId, &sinY, &pinf, &alInferir, &c, &nPred);
        const double totalMs = std::chrono::duration<double, std::milli>(Reloj::now() - c.inicio).count();
        comprobar(st == ENERGIA_OK, "inferir con la regla de 3 s devuelve OK");
        comprobar(c.ends >= 3 && c.ends <= 4, "3 s con pasadas de microsegundos: " + std::to_string(c.ends) + " grupos (3 o 4)");
        comprobar(c.starts == c.ends && c.indicesOk && c.totalCero, "un START y un END por grupo, step_index 0..n-1, step_total 0");
        comprobar(c.grupoMinimoMs >= ENERGIA_INFERENCIA_PASO_MIN_MS - 1,
                  "cada grupo dura al menos paso_min_ms (el mas corto: " + std::to_string(static_cast<int>(c.grupoMinimoMs)) + " ms)");
        comprobar(c.metricasOk && c.pasadas > 100, "END con 'pasadas' y 'predicciones' del grupo; muchas pasadas por grupo ("
                                                       + std::to_string(c.pasadas) + ")");
        comprobar(nPred == c.pasadas * 4 && nPred == c.predicciones, "n_predicciones = filas x pasadas completas");
        comprobar(totalMs >= 3000 && totalMs < 5000, "la inferencia dura entre la duracion minima y ~1 s mas ("
                                                         + std::to_string(static_cast<int>(totalMs)) + " ms)");
    }
    EnergiaParamsInferencia unaPasada = pinf;  // paso_min_ms = 0: cada grupo es una sola pasada
    unaPasada.paso_min_ms = 0;
    {
        Inferencia c;
        c.nFilas = 4;
        c.finalizarEnEnd = 2;
        int64_t nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_OK && c.ends == 3
                      && c.pasadas == 3 && nPred == 12,
                  "paso_min_ms 0: una pasada por grupo; FINALIZAR en el END del grupo 2 -> 3 grupos, 12 predicciones");
    }
    {
        Inferencia c;
        c.nFilas = 4;
        c.finalizarEnStart = 0;
        int64_t nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_OK && c.starts == 1 && c.ends == 1
                      && nPred == 4,
                  "FINALIZAR en START: la DLL hace ese grupo y termina (1 grupo, OK)");
    }
    {
        Inferencia c;
        c.nFilas = 4;
        c.detenerEnEnd = 1;
        int64_t nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_CANCELADO && nPred == 8,
                  "DETENER en el END del grupo 1: CANCELADO con las 2 pasadas completas (8 predicciones)");
    }
    {
        Inferencia c;
        c.nFilas = 4;
        c.detenerEnStart = 0;
        int64_t nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_CANCELADO && nPred == 0,
                  "DETENER en el primer START: CANCELADO sin predicciones");
    }
    {
        int64_t nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &unaPasada, nullptr, nullptr, &nPred) == ENERGIA_OK && nPred == 4,
                  "sin callback: un solo grupo y OK");
        EnergiaParamsInferencia loteUno = unaPasada;
        loteUno.lote = 1;
        Inferencia c;
        c.nFilas = 4;
        c.finalizarEnEnd = 0;
        comprobar(fnInferir(modeloId, &sinY, &loteUno, &alInferir, &c, &nPred) == ENERGIA_OK && nPred == 4,
                  "lote 1 (consulta por consulta): una pasada = 4 predicciones");
        EnergiaParamsInferencia loteCero = unaPasada;
        loteCero.lote = 0;
        nPred = -1;
        comprobar(fnInferir(modeloId, &sinY, &loteCero, &alInferir, &c, &nPred) == ENERGIA_ERR_PARAMETROS && nPred == 0,
                  "lote 0: ERR_PARAMETROS y n_predicciones = 0");
        nPred = -1;
        comprobar(fnInferir(modeloId + 1000, &sinY, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_ERR_MODELO_INVALIDO
                      && nPred == 0,
                  "modelo inexistente: ERR_MODELO_INVALIDO");
        const EnergiaDatos otraForma{X.data(), nullptr, 2, 4};  // 4 columnas: el modelo tiene 2
        comprobar(fnInferir(modeloId, &otraForma, &unaPasada, &alInferir, &c, &nPred) == ENERGIA_ERR_DATOS,
                  "columnas distintas de las del entrenamiento: ERR_DATOS");
    }
    {
        // El helper con una pasada que falla en la tercera: el error sale tal cual y
        // n_predicciones cuenta solo las dos pasadas completas
        int llamadas = 0;
        int64_t nPred = -1;
        Inferencia c;
        c.nFilas = 10;
        const int32_t st = energia::inferirEnGrupos(
            unaPasada, 10, &alInferir, &c, [&llamadas]() { return ++llamadas == 3 ? ENERGIA_ERR_INTERNO : ENERGIA_OK; }, &nPred);
        comprobar(st == ENERGIA_ERR_INTERNO && nPred == 20 && c.ends == 2,
                  "inferirEnGrupos: una pasada que falla devuelve su error; n_predicciones sin la pasada interrumpida");
    }

    EnergiaMetrica metricas[ENERGIA_MAX_METRICAS]{};
    int32_t nMetricas = 0;
    const int32_t stEval = fnEvaluar(modeloId, &datos, metricas, ENERGIA_MAX_METRICAS, &nMetricas);
    comprobar(stEval == ENERGIA_OK && nMetricas >= 1 && energia::leerTexto(metricas[0].nombre) == "val_accuracy",
              "evaluar devuelve val_accuracy");

    // ---- predecir (D-015) ----
    {
        const int32_t centinela = -7;
        std::vector<int32_t> pred(4, centinela);
        comprobar(fnPredecir(modeloId, &sinY, 65536, pred.data()) == ENERGIA_OK, "predecir con datos validos devuelve OK (Y ignorada)");
        comprobar(std::all_of(pred.begin(), pred.end(), [](int32_t c) { return c >= 0 && c < 2; }),
                  "predecir escribe una clase 0..n_clases-1 en cada fila");
        int64_t aciertos = 0;
        for (size_t i = 0; i < pred.size(); ++i) aciertos += pred[i] == static_cast<int32_t>(Y[i]) ? 1 : 0;
        comprobar(stEval == ENERGIA_OK && static_cast<double>(aciertos) / 4.0 == metricas[0].valor,
                  "la exactitud de las predicciones coincide con val_accuracy de evaluar");
        std::vector<int32_t> porFila(4, centinela);
        comprobar(fnPredecir(modeloId, &sinY, 1, porFila.data()) == ENERGIA_OK && porFila == pred,
                  "lote 1: las mismas predicciones, en el mismo orden");
        std::vector<int32_t> loteTres(4, centinela);
        comprobar(fnPredecir(modeloId, &sinY, 3, loteTres.data()) == ENERGIA_OK && loteTres == pred,
                  "lote 3 (un bloque incompleto al final): las mismas predicciones");

        std::vector<int32_t> nada(4, centinela);
        comprobar(fnPredecir(modeloId, &sinY, 0, nada.data()) == ENERGIA_ERR_PARAMETROS, "lote 0: ERR_PARAMETROS");
        comprobar(fnPredecir(modeloId, &sinY, 4, nullptr) == ENERGIA_ERR_PARAMETROS, "predicciones NULL: ERR_PARAMETROS");
        comprobar(fnPredecir(modeloId, nullptr, 4, nada.data()) == ENERGIA_ERR_DATOS, "datos NULL: ERR_DATOS");
        const EnergiaDatos vacios{X.data(), nullptr, 0, 2};
        comprobar(fnPredecir(modeloId, &vacios, 4, nada.data()) == ENERGIA_ERR_DATOS, "0 filas: ERR_DATOS");
        const EnergiaDatos otraForma{X.data(), nullptr, 2, 4};
        comprobar(fnPredecir(modeloId, &otraForma, 4, nada.data()) == ENERGIA_ERR_DATOS,
                  "columnas distintas de las del entrenamiento: ERR_DATOS");
        comprobar(fnPredecir(modeloId + 1000, &sinY, 4, nada.data()) == ENERGIA_ERR_MODELO_INVALIDO,
                  "modelo inexistente: ERR_MODELO_INVALIDO");
    }
    comprobar(fnLiberar(modeloId) == ENERGIA_OK, "liberar_modelo devuelve OK");
    comprobar(fnLiberar(modeloId) == ENERGIA_ERR_MODELO_INVALIDO, "liberar dos veces da ERR_MODELO_INVALIDO");

    descargar(dll);
    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "CONTRATO OK" : "CONTRATO CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
