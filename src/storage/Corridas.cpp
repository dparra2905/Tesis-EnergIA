// Corridas.cpp - ver Corridas.h.
// Origen: Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "Corridas.h"

#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace corridas {

QString nombreSeguro(const QString& experimento)
{
    QString s = experimento.trimmed();
    s.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return s;
}

int siguienteNumero(const QString& dirExperimento)
{
    int maximo = 0;
    const QRegularExpression re("^corrida_(\\d+)$");
    for (const QString& d : QDir(dirExperimento).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto m = re.match(d);
        if (m.hasMatch()) maximo = std::max(maximo, m.captured(1).toInt());
    }
    return maximo + 1;
}

bool crearCarpeta(const QString& dirSalida, const QString& experimento, QString* dirCorrida, int* numero,
                  QString* error)
{
    const QString nombre = nombreSeguro(experimento);
    if (nombre.isEmpty()) { *error = "Falta el nombre del experimento"; return false; }
    const QString dirExp = QDir(dirSalida).filePath(QString(CARPETA_CORRIDAS) + "/" + nombre);
    *numero = siguienteNumero(dirExp);
    *dirCorrida = QDir(dirExp).filePath(QString("corrida_%1").arg(*numero, 3, 10, QChar('0')));
    if (!QDir().mkpath(*dirCorrida)) { *error = "No se pudo crear " + QDir::toNativeSeparators(*dirCorrida); return false; }
    return true;
}

bool escribirJson(const QString& ruta, const QJsonObject& objeto, QString* error)
{
    QSaveFile f(ruta);  // escribe en un temporal y reemplaza al confirmar
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = "No se pudo escribir " + QDir::toNativeSeparators(ruta);
        return false;
    }
    f.write(QJsonDocument(objeto).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = "No se pudo escribir " + QDir::toNativeSeparators(ruta);
        return false;
    }
    return true;
}

} // namespace corridas
