// Protocolo.cpp - ver Protocolo.h.
#include "Protocolo.h"

#include "Reloj.h"

#include <QJsonDocument>
#include <QMetaObject>

#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace protocolo {

void inicializar()
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void emitir(const QString& evento, const QJsonObject& datos)
{
    QJsonObject o = datos;
    o["v"] = VERSION;
    o["evento"] = evento;
    o["t"] = QString::fromLatin1(reloj::isoUtc(reloj::ahoraMs()));
    const QByteArray linea = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(linea.constData(), 1, static_cast<size_t>(linea.size()), stdout);
    std::fflush(stdout);
}

void diagnostico(const QString& mensaje)
{
    const QByteArray linea = "[energia_orquestador " + reloj::isoUtc(reloj::ahoraMs()) + "] " + mensaje.toUtf8() + '\n';
    std::fwrite(linea.constData(), 1, static_cast<size_t>(linea.size()), stderr);
    std::fflush(stderr);
}

void LectorOrdenes::iniciar()
{
    // Hilo desacoplado: una lectura bloqueante de stdin no debe impedir que el proceso termine.
    std::thread([this] {
        char buf[4096];
        while (std::fgets(buf, sizeof(buf), stdin)) {
            const QByteArray linea = QByteArray(buf, static_cast<int>(std::strlen(buf))).trimmed();
            if (linea.isEmpty()) continue;
            QMetaObject::invokeMethod(this, [this, linea] { procesarLinea(linea); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this] { emit entradaCerrada(); }, Qt::QueuedConnection);
    }).detach();
}

void LectorOrdenes::procesarLinea(const QByteArray& linea)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(linea, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        diagnostico("Orden ignorada (no es un objeto JSON): " + QString::fromUtf8(linea.left(200)));
        return;
    }
    const QJsonObject o = doc.object();
    if (o["v"].toInt(-1) != VERSION) {
        diagnostico(QString("Orden ignorada (version de protocolo distinta de %1)").arg(VERSION));
        return;
    }
    const QString orden = o["orden"].toString();
    if (orden.isEmpty()) { diagnostico("Orden ignorada (falta 'orden')"); return; }
    emit ordenRecibida(orden, o);
}

} // namespace protocolo
