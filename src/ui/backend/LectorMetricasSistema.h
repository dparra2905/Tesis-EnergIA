// LectorMetricasSistema.h - Metricas en vivo del monitor de software para la interfaz (F5).
//
// Lee, desde ui.exe, las filas nuevas de sistema_metrics.csv y los datos fijos de
// sistema_info.json que escribe system_meter en la carpeta de la corrida. La lectura ocurre en
// el proceso de la interfaz, que no se mide, para no agregar trabajo al proceso medido
// (D-010). Los archivos de la corrida siguen siendo la fuente de verdad: aqui solo se derivan
// valores para mostrar.
//
// Valores derivados de cada fila (intervalo de ~1 s, D-007):
//   cpu_sistema_pct     = cpu_sis_ocupado_s / cpu_sis_total_s * 100
//   cpu_proceso_pct     = orq_cpu_s / cpu_sis_total_s * 100   (parte de la capacidad total del equipo)
//   cpu_proceso_nucleos = orq_cpu_s / (intervalo_ms / 1000)   (nucleos ocupados en promedio)
//   ram_sistema_pct     = ram_sis_usada_bytes / ram_total_bytes * 100
//   ram_proceso_mb      = orq_ws_bytes / 2^20                 (working set)
// Un valor que no se puede calcular (columna vacia: proceso terminado) queda ausente.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class LectorMetricasSistema {
public:
    static constexpr const char* ARCHIVO_CSV = "sistema_metrics.csv";
    static constexpr const char* ARCHIVO_INFO = "sistema_info.json";

    void iniciar(const QString& dirCorrida);
    void reiniciar();
    bool activo() const { return !dir_.isEmpty(); }

    // Lee lo nuevo. Devuelve las filas completas que aparecieron desde la ultima lectura.
    QVariantList actualizar();

    // Deriva los valores de una fila (columnas por nombre). Publico para las pruebas.
    static QVariantMap derivar(const QVariantMap& fila, double ramTotalBytes);

    qint64 ramTotalBytes() const { return ramTotal_; }

private:
    void leerInfo();

    QString dir_;
    qint64 posicion_ = 0;     // bytes del CSV ya consumidos
    QStringList columnas_;
    qint64 ramTotal_ = 0;
};
