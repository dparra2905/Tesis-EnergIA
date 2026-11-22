// FormatosModelo.h - Formato del modelo.bin de cada modelo y su nombre al exportarlo (D-013).
//
// El artefacto modelo.bin lo escribe guardar_modelo() de la DLL y su formato depende del modelo.
// EnergIA no puede volver a cargarlo (contrato v4, D-009), pero el Arbol y el MLP se pueden usar
// FUERA de EnergIA mediante la exportacion: un .zip con el modelo renombrado segun su formato,
// modelo.json y LEEME.txt (src/ui/backend/ExportacionModelo.h).
//
// Lo usan el orquestador (metadata.artefactos.modelo.exportable) y la interfaz (la exportacion).
// La DLL de prueba guarda una linea de texto, no un modelo: no es exportable.
//
// Codigo nuevo de EnergIA. Solo Qt Core.
#pragma once

#include <QString>

namespace modelos {

struct FormatoExportacion {
    const char* clave;     // metadata.modelo.clave
    const char* formato;   // identificador del formato en modelo.json
    const char* archivo;   // nombre del modelo dentro del .zip (en la carpeta sigue siendo modelo.bin)
    const char* libreria;  // con que se carga fuera de EnergIA
};

inline const FormatoExportacion* formatoExportacion(const QString& clave)
{
    static const FormatoExportacion formatos[] = {
        {"arbol_decision", "mlpack_cereal_binario", "modelo_mlpack.bin", "mlpack (cereal, archivo binario)"},
        {"mlp", "libtorch_torch_save", "modelo_libtorch.pt", "libtorch (torch::save / torch::load)"},
    };
    for (const FormatoExportacion& f : formatos)
        if (clave == QLatin1String(f.clave)) return &f;
    return nullptr;
}

} // namespace modelos
