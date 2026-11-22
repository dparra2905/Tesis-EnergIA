/*
 * energia_dll.h - Contrato C entre el orquestador de EnergIA y las DLL de modelos.
 * Version del contrato: 4
 *
 * Una DLL por modelo (EnergIA_Arbol.dll, EnergIA_MLP.dll, ...). Cada DLL se describe a si
 * misma (nombre, libreria, hiperparametros) y el orquestador arma la ventana con eso.
 *
 * Reglas del contrato:
 *   - Solo tipos simples (POD) cruzan la frontera: nada de std::string, std::vector ni clases.
 *   - Ninguna excepcion sale de la DLL; los errores vuelven como codigo de estado.
 *   - Tamanos fijos (int32_t, int64_t, double, char[N]) para que la ABI sea igual en MSVC y
 *     MinGW. Los tamanos esperados de cada estructura se comprueban al compilar (ver abajo).
 *   - La memoria se libera en el mismo lado donde se reservo: el modelo entrenado vive
 *     dentro de la DLL y solo la DLL lo libera (liberar_modelo).
 *   - Problema de clasificacion: las etiquetas de Y llegan ya convertidas a 0 .. n_clases-1.
 *   - Textos en UTF-8, terminados en '\0' y truncados al tamano del arreglo.
 *
 * Ampliacion de la version 4 sin cambiar su numero (D-015, metricas del modelo):
 *   - Nueva: predecir (clase predicha de cada fila, FUERA de la medicion). No hubo ningun Release
 *     con DLL v4 antes de esta ampliacion, asi que ENERGIA_API_VERSION sigue en 4; el catalogo
 *     exige tambien predecir: una DLL v4 compilada antes se descarta ("no exporta predecir:
 *     recompila la DLL con el contrato v4 actual").
 *
 * Cambios respecto a la version 3 (D-014, medir la inferencia):
 *   - Nueva: inferir (predecir con el modelo entrenado, dentro de la medicion).
 *   - Nueva estructura EnergiaParamsInferencia y la constante ENERGIA_INFERENCIA_PASO_MIN_MS.
 *   - Nuevo valor de retorno del callback: ENERGIA_FINALIZAR (para inferir).
 *   - Todo lo demas sigue igual que en v3. El orquestador solo acepta DLL de la version 4: una
 *     DLL v3 se descarta del catalogo ("contrato version 3, el orquestador usa la 4").
 *
 * Cambios de la version 3 respecto a la version 2:
 *   - Nuevas: energia_info_modelo, energia_n_hiperparametros, energia_describir_hiperparametro,
 *     evaluar.
 *   - EnergiaParams pasa a ser una lista de pares clave/valor (antes: un campo por parametro).
 *   - Se eliminan ENERGIA_MODELO_*: cada DLL es un solo modelo y se identifica con su info.
 */
#ifndef ENERGIA_DLL_H
#define ENERGIA_DLL_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(ENERGIA_DLL_EXPORTS)
#    define ENERGIA_API __declspec(dllexport)
#  else
#    define ENERGIA_API __declspec(dllimport)
#  endif
#else
#  define ENERGIA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ENERGIA_API_VERSION 4

/* ---- Codigos de estado (int32_t para fijar su tamano en la ABI) ---- */
#define ENERGIA_OK                   0
#define ENERGIA_ERR_PARAMETROS       1  /* parametros invalidos para el modelo            */
#define ENERGIA_ERR_DATOS            2  /* datos vacios, tamanos incoherentes, etiquetas  */
#define ENERGIA_ERR_INTERNO          3  /* fallo inesperado dentro de la DLL              */
#define ENERGIA_CANCELADO            4  /* el orquestador pidio detener el entrenamiento  */
#define ENERGIA_ERR_MODELO_INVALIDO  5  /* identificador de modelo que no existe          */
#define ENERGIA_ERR_ARCHIVO          6  /* no se pudo escribir el archivo del modelo      */

/* ---- Fases del callback de progreso ---- */
#define ENERGIA_FASE_START  0
#define ENERGIA_FASE_END    1

/* ---- Lo que devuelve el callback ---- */
#define ENERGIA_CONTINUAR  0
#define ENERGIA_DETENER    1  /* cancelar: la DLL se detiene y retorna ENERGIA_CANCELADO         */
#define ENERGIA_FINALIZAR  2  /* inferir: terminar despues del grupo en curso y retornar        */
                              /* ENERGIA_OK. En entrenar() se trata como ENERGIA_CONTINUAR      */

/* ---- Inferencia ---- */
/* Duracion minima de un paso de inferencia (grupo de pasadas): la DLL repite pasadas sin llamar
 * al callback hasta cubrir este tiempo. El orquestador la pasa en EnergiaParamsInferencia. */
#define ENERGIA_INFERENCIA_PASO_MIN_MS  1000

/* ---- Tipos de hiperparametro ---- */
#define ENERGIA_TIPO_ENTERO         0  /* numero entero entre minimo y maximo                */
#define ENERGIA_TIPO_REAL           1  /* numero real entre minimo y maximo                  */
#define ENERGIA_TIPO_OPCION         2  /* una de varias opciones; el valor es su indice 0..n */
#define ENERGIA_TIPO_LISTA_ENTEROS  3  /* texto "64,32": cada numero entre minimo y maximo,  */
                                       /* de 1 a max_elementos numeros                       */

/* ---- Tamanos fijos ---- */
#define ENERGIA_MAX_METRICAS     4
#define ENERGIA_MAX_HIPER        16
#define ENERGIA_LEN_NOMBRE       32   /* nombre de metrica y clave de hiperparametro */
#define ENERGIA_LEN_TEXTO        64
#define ENERGIA_LEN_AYUDA        160
#define ENERGIA_LEN_OPCIONES     128  /* opciones separadas por ';': "gini;entropia" */

typedef struct {
    char   nombre[ENERGIA_LEN_NOMBRE];  /* ej. "train_loss" */
    double valor;
} EnergiaMetrica;

typedef struct {
    int32_t        fase;          /* ENERGIA_FASE_START o ENERGIA_FASE_END */
    int32_t        step_index;    /* 0 .. step_total-1; en inferir, el numero de grupo */
    int32_t        step_total;    /* en inferir, 0 (no se conoce de antemano)          */
    int32_t        n_metricas;    /* 0 en START; 0..ENERGIA_MAX_METRICAS en END */
    EnergiaMetrica metricas[ENERGIA_MAX_METRICAS];
} EnergiaProgreso;

/*
 * Callback de progreso. La DLL lo llama al inicio (START) y al final (END) de cada paso
 * (una vez por arbol, una vez por epoca, o un solo paso). En END solo se reportan metricas
 * que salen gratis del entrenamiento: nada de evaluar en validacion dentro de la medicion.
 * Devuelve ENERGIA_CONTINUAR o ENERGIA_DETENER. Si recibe ENERGIA_DETENER, la DLL deja de
 * entrenar lo antes posible, libera lo que haya creado y retorna ENERGIA_CANCELADO.
 * En inferir() tambien puede devolver ENERGIA_FINALIZAR (ver inferir); entrenar() lo trata como
 * ENERGIA_CONTINUAR.
 */
typedef int32_t (*EnergiaProgressFn)(const EnergiaProgreso* progreso, void* user_data);

typedef struct {
    const double* X;        /* n_filas * n_cols, orden por filas (row-major) */
    const double* Y;        /* n_filas, etiquetas 0 .. n_clases-1            */
    int64_t       n_filas;
    int32_t       n_cols;
} EnergiaDatos;

/* ---- Autodescripcion ---- */

typedef struct {
    char    clave[ENERGIA_LEN_NOMBRE];      /* identificador corto: "arbol_decision"         */
    char    nombre[ENERGIA_LEN_TEXTO];      /* lo que ve el usuario: "Arbol de decision"      */
    char    libreria[ENERGIA_LEN_TEXTO];    /* "mlpack 4.5.0", "libtorch 2.5.1 (CPU)"         */
    char    unidad_paso[ENERGIA_LEN_NOMBRE];/* "arbol", "epoca": texto de la barra de avance  */
    int32_t hilos;                          /* hilos que usara al entrenar                    */
} EnergiaInfoModelo;

typedef struct {
    char    clave[ENERGIA_LEN_NOMBRE];      /* "max_depth": asi viaja en EnergiaParams        */
    char    etiqueta[ENERGIA_LEN_TEXTO];    /* "Profundidad maxima": lo que ve el usuario     */
    char    ayuda[ENERGIA_LEN_AYUDA];       /* explicacion corta (tooltip)                    */
    int32_t tipo;                           /* ENERGIA_TIPO_*                                 */
    int32_t max_elementos;                  /* solo LISTA_ENTEROS                             */
    double  minimo;                         /* ENTERO, REAL y cada numero de LISTA_ENTEROS    */
    double  maximo;
    double  por_defecto;                    /* ENTERO, REAL; en OPCION es el indice           */
    char    opciones[ENERGIA_LEN_OPCIONES]; /* solo OPCION: "gini;entropia"                   */
    char    defecto_texto[ENERGIA_LEN_TEXTO];/* solo LISTA_ENTEROS: "64,32"                   */
} EnergiaHiperparametro;

/* ---- Parametros de la inferencia (los llena el orquestador) ---- */

typedef struct {
    int32_t lote;          /* filas por bloque de prediccion (>= 1)                             */
    int32_t paso_min_ms;   /* duracion minima de un grupo de pasadas: ENERGIA_INFERENCIA_PASO_MIN_MS */
    int32_t reservado[6];  /* en 0; para versiones futuras                                      */
} EnergiaParamsInferencia;

/* ---- Valores elegidos por el usuario ---- */

typedef struct {
    char   clave[ENERGIA_LEN_NOMBRE];
    double numero;                   /* ENTERO, REAL, y OPCION como indice */
    char   texto[ENERGIA_LEN_TEXTO]; /* solo LISTA_ENTEROS                 */
} EnergiaValor;

typedef struct {
    int32_t      n_clases;   /* lo calcula el orquestador (>= 2)                          */
    uint32_t     semilla;    /* la misma de la particion                                  */
    int32_t      n_valores;  /* uno por cada hiperparametro que describe la DLL           */
    EnergiaValor valores[ENERGIA_MAX_HIPER];
} EnergiaParams;

/* Devuelve ENERGIA_API_VERSION con la que se compilo la DLL. */
ENERGIA_API int32_t energia_api_version(void);

/* Llena la informacion del modelo. */
ENERGIA_API int32_t energia_info_modelo(EnergiaInfoModelo* info);

/* Cuantos hiperparametros tiene el modelo (0 .. ENERGIA_MAX_HIPER). */
ENERGIA_API int32_t energia_n_hiperparametros(void);

/* Describe el hiperparametro i (0 .. n-1). ENERGIA_ERR_PARAMETROS si i esta fuera de rango. */
ENERGIA_API int32_t energia_describir_hiperparametro(int32_t i, EnergiaHiperparametro* out);

/*
 * Entrena el modelo. Llamada BLOQUEANTE: retorna cuando termina, falla o se cancela.
 * params debe traer exactamente un valor por hiperparametro descrito, cada uno dentro de su
 * rango; si no, retorna ENERGIA_ERR_PARAMETROS sin entrenar.
 * El callback se invoca desde el mismo hilo que llamo a entrenar().
 * Si retorna ENERGIA_OK, *modelo_id queda con un identificador > 0 del modelo entrenado,
 * que sigue vivo dentro de la DLL hasta liberar_modelo(). En cualquier otro caso, *modelo_id = 0.
 * La DLL no conserva punteros a train, val, params ni progreso despues de retornar.
 */
ENERGIA_API int32_t entrenar(const EnergiaDatos* train,
                             const EnergiaDatos* val,
                             const EnergiaParams* params,
                             EnergiaProgressFn callback,
                             void* user_data,
                             int64_t* modelo_id);

/*
 * Evalua el modelo sobre datos (el orquestador pasa validacion). El orquestador la llama
 * DESPUES de detener los medidores: su costo no entra en la medicion. No modifica el modelo.
 * Escribe hasta max_metricas metricas (al menos "val_accuracy") y su cantidad en *n_metricas.
 */
ENERGIA_API int32_t evaluar(int64_t modelo_id,
                            const EnergiaDatos* datos,
                            EnergiaMetrica* metricas,
                            int32_t max_metricas,
                            int32_t* n_metricas);

/* Escribe el modelo en un archivo (ruta en UTF-8). No lo libera. */
ENERGIA_API int32_t guardar_modelo(int64_t modelo_id, const char* ruta_utf8);

/* Libera la memoria del modelo dentro de la DLL. El identificador deja de ser valido. */
ENERGIA_API int32_t liberar_modelo(int64_t modelo_id);

/*
 * Inferencia (contrato v4, D-014). Predice sobre 'datos' con el modelo entrenado, DENTRO de la
 * medicion: el orquestador toma t_inf_antes y t_inf_despues justo alrededor de esta llamada.
 * BLOQUEANTE, desde el mismo hilo que llamo, como entrenar(); el callback se invoca desde ese hilo.
 *
 *   - Una pasada = recorrer todas las filas de 'datos' en bloques de params->lote filas. Las
 *     predicciones se calculan y se descartan: no se calcula ninguna metrica de calidad dentro de
 *     la medicion. datos->Y se ignora (puede ser NULL).
 *   - Un paso de inferencia = un grupo de pasadas completas que dura al menos
 *     params->paso_min_ms, medido por la DLL con un reloj monotonico. La DLL repite pasadas sin
 *     llamar al callback hasta alcanzar ese tiempo; si una pasada dura mas, el grupo tiene una
 *     sola. Callback START al empezar cada grupo y END al terminarlo (step_index = numero de
 *     grupo desde 0, step_total = 0). En END, dos metricas del grupo: "pasadas" y "predicciones"
 *     (filas x pasadas del grupo).
 *   - Lo que devuelve el callback: ENERGIA_FINALIZAR en END termina ahi y retorna ENERGIA_OK; en
 *     START, la DLL hace ese grupo y termina despues de el. ENERGIA_DETENER (en START o END)
 *     cancela: retorna ENERGIA_CANCELADO. Asi la inferencia siempre termina con grupos y pasadas
 *     completas. Sin callback (NULL) hace un solo grupo y retorna ENERGIA_OK.
 *   - *n_predicciones = filas x pasadas completas. Se escribe SIEMPRE, tambien al cancelar o al
 *     fallar (sin contar la pasada interrumpida).
 *   - No modifica el modelo. La DLL no conserva punteros a datos, params ni al progreso.
 * La logica de los grupos esta en energia::inferirEnGrupos (energia_comun.h): cada DLL solo le da
 * una funcion que hace una pasada, asi todas se comportan igual.
 */
ENERGIA_API int32_t inferir(int64_t modelo_id,
                            const EnergiaDatos* datos,
                            const EnergiaParamsInferencia* params,
                            EnergiaProgressFn callback,
                            void* user_data,
                            int64_t* n_predicciones);

/*
 * Prediccion (contrato v4, D-015). Escribe en predicciones[i] la clase predicha de la fila i de
 * 'datos', como indice 0 .. n_clases-1, en el mismo orden de las filas. El arreglo lo reserva el
 * orquestador con datos->n_filas enteros. Procesa en bloques de 'lote' filas (>= 1).
 * El orquestador la llama DESPUES de detener los medidores, como evaluar: su costo no entra en la
 * medicion. Con las predicciones calcula las metricas de clasificacion (matriz de confusion,
 * precision, recall, F1) del conjunto de validacion y del de entrenamiento.
 *   - No modifica el modelo y no llama a ningun callback. datos->Y se ignora (puede ser NULL).
 *   - Errores por codigo de estado: ENERGIA_ERR_PARAMETROS (lote < 1 o predicciones NULL),
 *     ENERGIA_ERR_DATOS (datos vacios o n_cols distinto del entrenamiento),
 *     ENERGIA_ERR_MODELO_INVALIDO. Si no devuelve ENERGIA_OK, el contenido de predicciones no
 *     se usa.
 */
ENERGIA_API int32_t predecir(int64_t modelo_id,
                             const EnergiaDatos* datos,
                             int32_t lote,
                             int32_t* predicciones);

#ifdef __cplusplus
}

/* Si algun compilador acomoda las estructuras distinto, esto falla al compilar en vez de
 * corromper datos en ejecucion. Valores para Windows/Linux de 64 bits (MSVC, MinGW, GCC). */
static_assert(sizeof(void*) == 8, "EnergIA solo soporta 64 bits");
static_assert(sizeof(EnergiaMetrica) == 40, "ABI: EnergiaMetrica");
static_assert(sizeof(EnergiaProgreso) == 176, "ABI: EnergiaProgreso");
static_assert(sizeof(EnergiaDatos) == 32, "ABI: EnergiaDatos");
static_assert(sizeof(EnergiaInfoModelo) == 196, "ABI: EnergiaInfoModelo");
static_assert(sizeof(EnergiaHiperparametro) == 480, "ABI: EnergiaHiperparametro");
static_assert(sizeof(EnergiaValor) == 104, "ABI: EnergiaValor");
static_assert(sizeof(EnergiaParams) == 1680, "ABI: EnergiaParams");
static_assert(sizeof(EnergiaParamsInferencia) == 32, "ABI: EnergiaParamsInferencia");
#endif

#endif /* ENERGIA_DLL_H */