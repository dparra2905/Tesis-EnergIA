// dll_contrato_invalido.cpp - DLL que NO cumple el contrato v4, solo para probar el catalogo.
//
// Se compila dos veces:
//   EnergIA_Incompleta.dll   solo exporta energia_api_version (faltan las demas funciones)
//   EnergIA_VersionVieja.dll una DLL del contrato v3: exporta sus 8 funciones (sin inferir) y
//                            declara la version 3. El orquestador solo acepta la 4 (D-014).
//   EnergIA_SinPredecir.dll  una DLL v4 compilada antes de predecir (D-015): exporta las 9
//                            funciones de la v4 original y declara la version 4.
#include <cstdint>

#if defined(_WIN32)
#  define EXPORTAR extern "C" __declspec(dllexport)
#else
#  define EXPORTAR extern "C" __attribute__((visibility("default")))
#endif

#ifdef VERSION_VIEJA
EXPORTAR int32_t energia_api_version(void) { return 3; }
EXPORTAR int32_t energia_info_modelo(void*) { return 3; }
EXPORTAR int32_t energia_n_hiperparametros(void) { return 0; }
EXPORTAR int32_t energia_describir_hiperparametro(int32_t, void*) { return 1; }
EXPORTAR int32_t entrenar(const void*, const void*, const void*, void*, void*, int64_t*) { return 3; }
EXPORTAR int32_t evaluar(int64_t, const void*, void*, int32_t, int32_t*) { return 3; }
EXPORTAR int32_t guardar_modelo(int64_t, const char*) { return 3; }
EXPORTAR int32_t liberar_modelo(int64_t) { return 3; }
#elif defined(SIN_PREDECIR)
EXPORTAR int32_t energia_api_version(void) { return 4; }
EXPORTAR int32_t energia_info_modelo(void*) { return 3; }
EXPORTAR int32_t energia_n_hiperparametros(void) { return 0; }
EXPORTAR int32_t energia_describir_hiperparametro(int32_t, void*) { return 1; }
EXPORTAR int32_t entrenar(const void*, const void*, const void*, void*, void*, int64_t*) { return 3; }
EXPORTAR int32_t evaluar(int64_t, const void*, void*, int32_t, int32_t*) { return 3; }
EXPORTAR int32_t guardar_modelo(int64_t, const char*) { return 3; }
EXPORTAR int32_t liberar_modelo(int64_t) { return 3; }
EXPORTAR int32_t inferir(int64_t, const void*, const void*, void*, void*, int64_t*) { return 3; }
#else
EXPORTAR int32_t energia_api_version(void) { return 4; }
#endif
