// DllModelo.cpp - ver DllModelo.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "DllModelo.h"

bool DllModelo::cargar(const QString& ruta, QString* error)
{
    if (dll_.isLoaded()) descargar();
    fn_ = FuncionesDll();
    dll_.setFileName(ruta);
    if (!dll_.load()) { *error = "No se pudo cargar la DLL: " + dll_.errorString(); return false; }

    auto fnVersion = reinterpret_cast<FnVersion>(dll_.resolve("energia_api_version"));
    fn_.entrenar = reinterpret_cast<FnEntrenar>(dll_.resolve("entrenar"));
    fn_.evaluar = reinterpret_cast<FnEvaluar>(dll_.resolve("evaluar"));
    fn_.guardar = reinterpret_cast<FnGuardar>(dll_.resolve("guardar_modelo"));
    fn_.liberar = reinterpret_cast<FnLiberar>(dll_.resolve("liberar_modelo"));
    fn_.inferir = reinterpret_cast<FnInferir>(dll_.resolve("inferir"));
    fn_.predecir = reinterpret_cast<FnPredecir>(dll_.resolve("predecir"));
    const int32_t versionDll = fnVersion ? fnVersion() : -1;
    if (fnVersion && versionDll != ENERGIA_API_VERSION) {
        *error = QString("Version de contrato distinta: DLL=%1, orquestador=%2").arg(versionDll).arg(ENERGIA_API_VERSION);
        descargar();
        return false;
    }
    if (!fnVersion || !fn_.entrenar || !fn_.evaluar || !fn_.guardar || !fn_.liberar || !fn_.inferir || !fn_.predecir) {
        *error = "La DLL no exporta todas las funciones del contrato (revisar extern \"C\")";
        descargar();
        return false;
    }
    return true;
}

void DllModelo::soltarHastaSalir()
{
    fn_ = FuncionesDll();
    soltada_ = true;
}

bool DllModelo::descargar(QString* error)
{
    fn_ = FuncionesDll();
    if (soltada_) return false;
    if (!dll_.isLoaded()) return true;
    if (dll_.unload()) return true;
    if (error) *error = dll_.errorString();
    return false;
}
