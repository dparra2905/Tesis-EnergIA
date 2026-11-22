// CatalogoModelos.h - Descubrimiento de los tipos de modelo disponibles (D-009).
//
// Busca las DLL EnergIA_* de un directorio, verifica que cumplan el contrato v4
// (energia_dll.h), lee su descripcion (informacion del modelo e hiperparametros) y la
// descarga. No entrena ni evalua: solo describe lo que hay. Solo acepta la version 4: una DLL v3
// queda descartada con el motivo "contrato versión 3, el orquestador usa la 4" (D-014), y una v4
// compilada antes de predecir (D-015), con "no exporta predecir: recompila la DLL con el contrato
// v4 actual".
//
// Origen: adaptado de VentanaPrincipal::buscarModelos(), leerDescripcion() y
// valoresPorDefecto() de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra),
// sin la parte de Qt Widgets. Ver docs/ORIGEN_BACKEND.md.
#pragma once

#include "energia_dll.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <vector>

// Firmas de las funciones del contrato v4 (las de v3 son las mismas que usa el orquestador de
// Back-EnergIA; inferir y predecir son nuevas en v4)
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

// Un tipo de modelo encontrado: una DLL valida
struct ModeloDisponible {
    QString ruta;
    EnergiaInfoModelo info{};
    std::vector<EnergiaHiperparametro> hiper;
    // DLL de prueba (EnergIA_Prueba): herramienta interna, no es un modelo del alcance (D-009)
    bool interno = false;
};

// Una DLL que coincide con el patron pero no se pudo usar
struct DllDescartada {
    QString ruta;
    QString motivo;
};

class CatalogoModelos {
public:
    // Clave de la DLL de prueba (energia_dll_stub.cpp)
    static const char* const CLAVE_PRUEBA;

    // Patron de nombre de las DLL de modelos (EnergIA_*.dll en Windows)
    static QString patron();

    // Explora el directorio. Devuelve false si el directorio no existe.
    // Una DLL invalida no detiene la exploracion: queda en descartadas().
    bool explorar(const QString& directorio);

    const QList<ModeloDisponible>& modelos() const { return modelos_; }
    const QList<DllDescartada>& descartadas() const { return descartadas_; }
    QString directorio() const { return directorio_; }

    // Catalogo completo en JSON (modelos con sus hiperparametros y DLL descartadas)
    QJsonObject aJson() const;
    static QJsonObject modeloAJson(const ModeloDisponible& m);

    // Carga la DLL, verifica el contrato, copia su descripcion y la descarga
    static bool leerDescripcion(ModeloDisponible* m, QString* error);

    // Valores por defecto que describe la DLL, en el formato de EnergiaParams
    static void valoresPorDefecto(const ModeloDisponible& m, EnergiaParams* p);

private:
    QString directorio_;
    QList<ModeloDisponible> modelos_;
    QList<DllDescartada> descartadas_;
};
