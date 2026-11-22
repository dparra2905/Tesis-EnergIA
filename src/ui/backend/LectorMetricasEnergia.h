// LectorMetricasEnergia.h - Lecturas en vivo del monitor fisico de energia para la interfaz (F6).
//
// Lee, desde ui.exe, las filas nuevas de energia_atorch.csv que escribe energia_ble.py en la
// carpeta de la corrida (rejilla de 1 s, D-007). Como el monitor de software, la lectura ocurre
// en el proceso de la interfaz, que no se mide (D-010).
//
// Solo se muestran potencia, voltaje y corriente de la ultima trama vigente y si hay conexion.
// El contador energy_kwh del medidor NO se expone: no es la energia del experimento (D-008).
// La energia del experimento se calcula al terminar la corrida (resultados.json).
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class LectorMetricasEnergia {
public:
    static constexpr const char* ARCHIVO_CSV = "energia_atorch.csv";

    void iniciar(const QString& dirCorrida);
    void reiniciar();
    bool activo() const { return !dir_.isEmpty(); }

    // Filas nuevas completas: {timestamp, conectado, y si hay trama vigente: timestamp_trama,
    // potencia_w, voltaje_v, corriente_a}
    QVariantList actualizar();

    static QVariantMap derivar(const QVariantMap& fila);

private:
    QString dir_;
    qint64 posicion_ = 0;
    QStringList columnas_;
};
