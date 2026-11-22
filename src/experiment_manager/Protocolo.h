// Protocolo.h - Comunicacion del orquestador con quien lo lanza (ui.exe), D-010.
//
// JSON Lines en UTF-8, un objeto por linea:
//   stdout  eventos del orquestador  {"v":1,"evento":"...","t":"<ISO UTC>", ...}
//   stdin   ordenes                  {"v":1,"orden":"cancelar"}
//   stderr  diagnostico tecnico (texto libre, no forma parte del protocolo)
// Esquema documentado en docs/sdd/PROTOCOLO_ORQUESTADOR.md. Codigo nuevo de EnergIA.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace protocolo {

constexpr int VERSION = 1;

// Prepara stdout para JSON Lines (modo binario: '\n' sin traducir a "\r\n")
void inicializar();

// Escribe un evento en stdout (una linea JSON). Usar solo desde el hilo principal.
void emitir(const QString& evento, const QJsonObject& datos = QJsonObject());

// Escribe un mensaje de diagnostico en stderr
void diagnostico(const QString& mensaje);

// Lee ordenes de stdin en un hilo propio y las entrega en el hilo del objeto.
// Si stdin se cierra (por ejemplo, porque ui.exe termino), solo se avisa: la corrida sigue.
class LectorOrdenes : public QObject {
    Q_OBJECT
public:
    // Arranca la lectura. El objeto debe vivir hasta que termine el proceso.
    void iniciar();

signals:
    void ordenRecibida(const QString& orden, const QJsonObject& mensaje);
    void entradaCerrada();

private:
    void procesarLinea(const QByteArray& linea);
};

} // namespace protocolo
