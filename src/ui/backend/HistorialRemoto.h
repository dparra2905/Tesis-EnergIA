// HistorialRemoto.h - Corridas de la base de datos visibles para el usuario (D-011).
//
// Lee la tabla corrida (las reglas RLS de la base deciden que se ve: las propias, las
// compartidas y, para un admin, todas), muestra el detalle de una corrida (con la misma forma
// que Historial::detalle, calculado desde la serie unida), compara varias, cambia la
// visibilidad o borra una corrida propia y exporta su serie unida a un CSV con las mismas columnas y formato que la exportacion local
// (analisis::escribirRejillaCsv). Desde otro equipo solo se descarga la serie unida: los CSV
// crudos de los medidores nunca se suben.
//
// Solo Qt Core + Network.
#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class ClienteSupabase;

class HistorialRemoto : public QObject {
    Q_OBJECT
    // [{id, experimento, numero, t_creacion, equipo, estado, modelo_dll, visibilidad, propia, dueno,
    //   t_start, subida_en, duracion_entrenamiento_s, energia_entrenamiento_wh,
    //   energia_total_wh, potencia_media_w, accuracy_val}] (la mas reciente primero)
    Q_PROPERTY(QVariantList corridas READ corridas NOTIFY cambiado)
    // "inactivo" | "cargando" | "listo" | "error"
    Q_PROPERTY(QString estado READ estado NOTIFY cambiado)
    Q_PROPERTY(QString error READ error NOTIFY cambiado)
    // id de la corrida que se esta exportando (0 = ninguna)
    Q_PROPERTY(qint64 exportando READ exportando NOTIFY exportacionCambiada)
    // Detalle de una corrida (cargarDetalle): misma forma que Historial::detalle, con
    // dir = "base:<id>" y ademas origen, id_remoto, propia, dueno, visibilidad, equipo,
    // hardware {cpu, nucleos_logicos, ram_total_bytes, sistema_operativo} y dataset
    Q_PROPERTY(QVariantMap detalle READ detalle NOTIFY detalleCambiado)
    // "inactivo" | "cargando" | "listo" | "error" (con errorDetalle)
    Q_PROPERTY(QString estadoDetalle READ estadoDetalle NOTIFY detalleCambiado)
    Q_PROPERTY(QString errorDetalle READ errorDetalle NOTIFY detalleCambiado)
    // Comparacion (cargarComparacion): lista de detalles, en el orden pedido
    Q_PROPERTY(QVariantList comparacion READ comparacion NOTIFY comparacionCambiada)
    Q_PROPERTY(QString estadoComparacion READ estadoComparacion NOTIFY comparacionCambiada)
    Q_PROPERTY(QString errorComparacion READ errorComparacion NOTIFY comparacionCambiada)

public:
    // Filas por pagina al descargar la serie (Supabase limita cada respuesta a 1000 filas)
    static constexpr int FILAS_POR_PAGINA = 1000;
    // Corridas que trae la lista (las mas recientes); sin paginacion
    static constexpr int LIMITE_LISTA = 500;

    explicit HistorialRemoto(ClienteSupabase* sesion, QObject* parent = nullptr);

    QVariantList corridas() const { return corridas_; }
    QString estado() const { return estado_; }
    QString error() const { return error_; }
    qint64 exportando() const { return exportando_; }
    QVariantMap detalle() const { return detalle_; }
    QString estadoDetalle() const { return estadoDetalle_; }
    QString errorDetalle() const { return errorDetalle_; }
    QVariantList comparacion() const { return comparacion_; }
    QString estadoComparacion() const { return estadoComparacion_; }
    QString errorComparacion() const { return errorComparacion_; }

    Q_INVOKABLE void recargar();
    // Solo el dueno puede cambiarla (lo impone la base). Resultado: visibilidadCambiada.
    Q_INVOKABLE void cambiarVisibilidad(qint64 id, const QString& visibilidad);
    // Nota de condiciones (D-012): editable despues de subida solo por el dueno (lo impone la
    // base, como la visibilidad). Texto vacio = sin nota (null). Resultado: notaCambiada.
    Q_INVOKABLE void cambiarNota(qint64 id, const QString& nota);
    // Descarga la serie unida y la escribe en 'destino'. Resultado: exportacionTerminada.
    Q_INVOKABLE void exportarCsv(qint64 id, const QString& destino);
    // Descarga la corrida y su serie unida y arma 'detalle'
    Q_INVOKABLE void cargarDetalle(qint64 id);
    // Borra una corrida propia (con su detalle, en cascada). Resultado: corridaBorrada.
    Q_INVOKABLE void borrar(qint64 id);
    // Carga el detalle de varias corridas (2 a MAX_COMPARAR) para compararlas
    Q_INVOKABLE void cargarComparacion(const QVariantList& ids);

    static constexpr int MAX_COMPARAR = 4;

    // Detalle desde la fila de la tabla corrida (con experimento, perfil, hardware_equipo y
    // dataset embebidos) y su serie unida. Publico para las pruebas.
    static QVariantMap construirDetalle(const QJsonObject& corrida, const QJsonArray& serie, const QString& usuarioId);
    Q_INVOKABLE QString rutaLocal(const QUrl& url) const;

signals:
    // Al recargar la lista se sabe si la base tiene la migracion 008 (D-014): ColaSubidas lo usa
    void migracion008(bool aplicada);
    // Lo mismo con la migracion 009 (metricas del modelo, D-015)
    void migracion009(bool aplicada);
    void cambiado();
    // La lista se cargo sin error. 'completa' = false si trajo LIMITE_LISTA corridas (puede haber
    // mas en la base que no vinieron). La usa ColaSubidas para corregir las marcas "En la base"
    // de las corridas borradas por fuera de la aplicacion (D3).
    void listaCargada(const QVariantList& corridas, bool completa);
    void exportacionCambiada();
    // error = "" si salio bien
    void visibilidadCambiada(qint64 id, const QString& visibilidad, const QString& error);
    void notaCambiada(qint64 id, const QString& nota, const QString& error);
    void exportacionTerminada(qint64 id, const QString& destino, int filas, const QString& error);
    void detalleCambiado();
    void comparacionCambiada();
    // error = "" si se borro
    void corridaBorrada(qint64 id, const QString& error);

private:
    using AlTenerSerie = std::function<void(const QJsonArray& serie, const QString& error)>;
    using AlTenerDetalle = std::function<void(const QVariantMap& detalle, const QString& error)>;
    // Serie unida completa, en paginas de FILAS_POR_PAGINA
    void descargarSerie(qint64 id, int desde, QJsonArray acumulado, AlTenerSerie alTerminar);
    void obtenerDetalle(qint64 id, AlTenerDetalle alTerminar);
    void siguienteComparacion(QVariantList pendientes);
    void ponerEstado(const QString& estado, const QString& error);

    ClienteSupabase* sesion_;
    QVariantList corridas_;
    QString estado_ = "inactivo";
    QString error_;
    qint64 exportando_ = 0;
    QVariantMap detalle_;
    QString estadoDetalle_ = "inactivo";
    QString errorDetalle_;
    qint64 pedidoDetalle_ = 0;  // ultimo id pedido (descarta respuestas viejas)
    QVariantList comparacion_;
    QString estadoComparacion_ = "inactivo";
    QString errorComparacion_;
    int generacionComparacion_ = 0;
};
