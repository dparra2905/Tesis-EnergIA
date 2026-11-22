// Historial.h - Corridas guardadas, su detalle y su exportacion, para la interfaz (F9).
//
// Lee <dir_salida>/corridas/<experimento>/corrida_NNN/ desde ui.exe (D-010: historial y
// resultados son responsabilidad de la interfaz). La fuente de verdad son los archivos de cada
// corrida: metadata.json, resultados.json y los CSV de los medidores. No modifica nada.
#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QJsonObject>
#include <QVariantMap>

class Historial : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList corridas READ corridas NOTIFY cambiado)
    Q_PROPERTY(QString dirSalida READ dirSalida NOTIFY cambiado)
    // Hay una exportacion del modelo en curso (exportarModelo)
    Q_PROPERTY(bool exportandoModelo READ exportandoModelo NOTIFY exportacionModeloCambiada)

public:
    explicit Historial(const QString& dirSalida, QObject* parent = nullptr);
    ~Historial() override;

    QVariantList corridas() const { return corridas_; }
    QString dirSalida() const { return dirSalida_; }
    // Cambiar la raíz no mueve ni elimina corridas anteriores: recarga solo la nueva ubicación.
    void establecerDirSalida(const QString& dir);

    // Vuelve a leer las corridas (la mas reciente primero)
    Q_INVOKABLE void recargar();
    // Resumen + estadisticas por ventana (entrenamiento y medicion) + series para graficas
    Q_INVOKABLE QVariantMap detalle(const QString& dirCorrida) const;
    // Exporta la rejilla de 1 s de la corrida a un CSV. "" si salio bien; si no, el motivo.
    Q_INVOKABLE QString exportarCsv(const QString& dirCorrida, const QString& destino) const;

    // Exportar el modelo (D-013, ExportacionModelo.h). infoExportacion: {exportable, motivo,
    // formato, archivo, columnas_conocidas, datos_sinteticos, avisos}. exportarModelo escribe el
    // .zip en otro hilo (puede calcular el sha256 de un CSV grande) y termina con modeloExportado.
    Q_INVOKABLE QVariantMap infoExportacion(const QString& dirCorrida) const;
    Q_INVOKABLE void exportarModelo(const QString& dirCorrida, const QString& destino);
    bool exportandoModelo() const { return hiloExportacion_ != nullptr; }
    // Cache de huellas de la cola de subidas (huellas_datasets.json): solo se lee
    void setRutaHuellas(const QString& ruta) { rutaHuellas_ = ruta; }
    Q_INVOKABLE QString rutaLocal(const QUrl& url) const;

    // Nota de condiciones de la corrida (nota.txt en su carpeta, D-012): texto libre opcional.
    // guardarNota devuelve "" si salio bien; con el texto vacio borra nota.txt.
    Q_INVOKABLE QString nota(const QString& dirCorrida) const;
    Q_INVOKABLE QString guardarNota(const QString& dirCorrida, const QString& texto);
    static QString leerNota(const QString& dirCorrida);
    static constexpr const char* ARCHIVO_NOTA = "nota.txt";

    // Resumen de una corrida a partir de su metadata.json y su resultados.json
    static QVariantMap leerResumen(const QString& dirCorrida);
    // El mismo resumen desde el contenido de ambos archivos (p. ej. las copias guardadas en la
    // base de datos, D-011). 'dir' es la carpeta local o un identificador ("base:<id>").
    static QVariantMap resumenDesdeJson(const QJsonObject& metadata, const QJsonObject& resultados, const QString& dir);

signals:
    void cambiado();
    void exportacionModeloCambiada();
    // error "" si salio bien; avisos: los de modelo.json (columnas desconocidas, datos sinteticos,
    // sha256 calculado al exportar)
    void modeloExportado(const QString& dirCorrida, const QString& destino, const QString& error, const QVariantList& avisos);

private:
    QString dirSalida_;
    QVariantList corridas_;
    QString rutaHuellas_;
    QThread* hiloExportacion_ = nullptr;
};
