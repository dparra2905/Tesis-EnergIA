// DllModelo.h - DLL del modelo elegido para una corrida.
//
// Carga solo la DLL elegida, resuelve las funciones del contrato v4 que usa la corrida
// (entrenar, inferir, evaluar, predecir, guardar_modelo, liberar_modelo) y verifica la version.
//
// Al terminar una corrida la DLL NO se descarga: se suelta (soltarHastaSalir) y Windows la
// libera al terminar el proceso del orquestador, que ejecuta una sola corrida (D-010).
// Descargarla con FreeLibrary puede bloquear el proceso para siempre: libtorch (MLP) deja
// vivos los hilos de su pool de OpenMP (libiomp5md.dll) cuando entrena con mas de un hilo, y
// la descarga espera a esos hilos bajo el bloqueo del cargador de Windows. En Back-EnergIA la
// descarga servia para que un modelo no arrastrara la memoria de otro en el mismo proceso.
//
// Origen: FuncionesDll, VentanaPrincipal::cargarDll() y descargarDll() de Back-EnergIA
// (orquestador.cpp, commit 1ab19f1, autor David Parra). Adaptacion: clase propia sin
// dependencias de la ventana; los mensajes se devuelven en lugar de escribirse en el log.
#pragma once

#include "CatalogoModelos.h"

#include <QLibrary>
#include <QString>

// Funciones de la DLL cargada para la corrida en curso
struct FuncionesDll {
    FnEntrenar entrenar = nullptr;
    FnEvaluar evaluar = nullptr;
    FnGuardar guardar = nullptr;
    FnLiberar liberar = nullptr;
    FnInferir inferir = nullptr;
    FnPredecir predecir = nullptr;
};

class DllModelo {
public:
    ~DllModelo() { if (!soltada_) descargar(); }

    // Carga la DLL y resuelve las funciones. Si falla, la deja descargada y explica por que.
    bool cargar(const QString& ruta, QString* error);
    // Devuelve true si la DLL quedo descargada (o no estaba cargada)
    bool descargar(QString* error = nullptr);
    // Deja de usar la DLL sin descargarla: se libera al terminar el proceso (ver arriba).
    // Despues de esto, ni descargar() ni el destructor la descargan.
    void soltarHastaSalir();
    bool soltada() const { return soltada_; }

    bool cargada() const { return dll_.isLoaded(); }
    const FuncionesDll& funciones() const { return fn_; }
    QString ruta() const { return dll_.fileName(); }

private:
    QLibrary dll_;
    FuncionesDll fn_;
    bool soltada_ = false;
};
