// ConfigExperimento.h - Lectura y validacion de config.json (entrada de una corrida).
//
// Codigo nuevo de EnergIA. En Back-EnergIA esta configuracion salia de la ventana Qt Widgets;
// aqui llega en un archivo JSON (D-010). El formato esta documentado en
// docs/sdd/PROTOCOLO_ORQUESTADOR.md.
#pragma once

#include "CatalogoModelos.h"
#include "energia_dll.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>

struct ConfigExperimento {
    static constexpr int VERSION = 1;

    QString experimento;
    QString dirSalida;           // raiz: las corridas quedan en <dirSalida>/corridas/...
    QString rutaDll;             // DLL del tipo de modelo (D-009)
    QJsonObject hiperparametros; // solicitados; los que falten toman su valor por defecto

    bool usarSintetico = true;   // datos sinteticos de prueba (no son un dataset real)
    QString rutaCsv;
    QString columnaObjetivo;     // nombre de la columna; o su indice si llega como numero
    int columnaObjetivoIndice = -1;

    int porcentajeTrain = 80;    // 50..95, como en Back-EnergIA
    uint32_t semilla = 42;
    bool guardarModelo = false;

    // Medidores de la corrida ("medidores": {"sistema": true}). Sin la clave: ninguno (como en F3).
    bool medidorSistema = false;   // monitor de software: system_meter (F5)
    bool medidorEnergia = false;   // monitor fisico de energia: energia_ble.py (F6)
    // "energia_ble": {"python": ..., "script": ..., "mac": ...}; vacios = valores por defecto
    QString energiaPython;
    QString energiaScript;
    QString energiaMac;

    // Duraciones de la linea de tiempo. La linea base (fases previa y posterior) es de 20 s si
    // config.json no la indica; las pruebas que necesitan 0 lo piden explicitamente. La
    // correccion de la energia por linea base no esta decidida (D-008).
    static constexpr qint64 LINEA_BASE_DEFECTO_MS = 20000;
    qint64 margenInicioMs = 0;
    qint64 fasePreviaMs = LINEA_BASE_DEFECTO_MS;
    qint64 fasePosteriorMs = LINEA_BASE_DEFECTO_MS;
    // Linea base intermedia, entre el entrenamiento y la inferencia (D-014). tiempos.fase_media_ms
    // es opcional: si falta, dura lo mismo que la fase previa. Solo se usa con la inferencia.
    qint64 faseMediaMs = LINEA_BASE_DEFECTO_MS;

    // Inferencia (D-014, seccion "inferencia" de config.json): opcional por corrida. Sin activarla,
    // la corrida es como antes. Predice sobre la particion de validacion, en lotes de
    // inferenciaLote filas, durante al menos inferenciaDuracionMinS segundos.
    static constexpr int INFERENCIA_LOTE_DEFECTO = 65536;     // como los bloques de evaluar en el MLP
    static constexpr int INFERENCIA_LOTE_MAXIMO = 1000000;
    static constexpr int INFERENCIA_DURACION_DEFECTO_S = 60;
    static constexpr int INFERENCIA_DURACION_MAXIMA_S = 3600;
    bool inferenciaActivada = false;
    int inferenciaLote = INFERENCIA_LOTE_DEFECTO;
    int inferenciaDuracionMinS = INFERENCIA_DURACION_DEFECTO_S;

    QJsonObject original;        // config tal como llego, para guardarla en la corrida
};

namespace config {

// Lee y valida config.json. Devuelve false con el motivo si algo es invalido.
bool cargar(const QString& ruta, ConfigExperimento* cfg, QString* error);
bool desdeJson(const QJsonObject& raiz, ConfigExperimento* cfg, QString* error);

// Arma EnergiaParams a partir de los valores por defecto del modelo y los solicitados.
// "usados" queda con los valores finales en forma legible (opciones por nombre).
bool resolverHiperparametros(const ModeloDisponible& modelo, const QJsonObject& solicitados, EnergiaParams* params,
                             QJsonObject* usados, QString* error);

} // namespace config
