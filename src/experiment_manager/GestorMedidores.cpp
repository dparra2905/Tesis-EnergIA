// GestorMedidores.cpp - ver GestorMedidores.h.
// Origen: adaptado de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "GestorMedidores.h"

#include "Protocolo.h"
#include "Reloj.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

GestorMedidores::GestorMedidores(QObject* parent) : QObject(parent)
{
    timerForzar_.setSingleShot(true);
    connect(&timerForzar_, &QTimer::timeout, this, &GestorMedidores::forzarCierre);
}

GestorMedidores::~GestorMedidores()
{
    // Un medidor no debe quedar vivo si el orquestador termina sin detenerlo
    for (Estado& e : medidores_) {
        if (e.proceso && e.proceso->state() != QProcess::NotRunning) {
            e.proceso->disconnect(this);
            e.proceso->kill();
            e.proceso->waitForFinished(2000);
        }
    }
}

DefinicionMedidor GestorMedidores::sistema()
{
    DefinicionMedidor d;
    d.nombre = "sistema";
#ifdef Q_OS_WIN
    d.programa = "system_meter.exe";
#else
    d.programa = "system_meter";
#endif
    d.argumentos = {"--t-start", "{T_START_UTC}", "--carpeta", "{CARPETA_CORRIDA}",
                    "--duration", "{DURACION_MAX_S}", "--pid", "{PID_ORQUESTADOR}"};
    d.archivos = {"sistema_metrics.csv", "sistema_info.json"};
    return d;
}

DefinicionMedidor GestorMedidores::energia(const QString& python, const QString& script, const QString& mac)
{
    DefinicionMedidor d;
    d.nombre = "energia";
    d.programa = python;
    d.argumentos = {script, "medir", "--t-start-utc", "{T_START_UTC}", "--carpeta", "{CARPETA_CORRIDA}",
                    "--duracion-max-s", "{DURACION_MAX_S}", "--pid", "{PID_ORQUESTADOR}"};
    if (!mac.isEmpty()) d.argumentos << "--mac" << mac;
    d.archivoListo = "energia_lista.flag";  // con la primera trama valida del Atorch
    d.archivos = {"energia_atorch.csv", "energia_tramas.csv", "energia_info.json", "energia_log.txt"};
    d.ayudaSiFalta = "Crea el entorno de Python del medidor con src/monitoring/hardware_monitor/crear_entorno.bat.";
    return d;
}

QStringList GestorMedidores::nombres() const
{
    QStringList n;
    for (const DefinicionMedidor& d : definiciones_) n << d.nombre;
    return n;
}

bool GestorMedidores::lanzar(qint64 tStartMs, const QString& dirCorrida, qint64 pidMedido, QString* error)
{
    dirCorrida_ = dirCorrida;
    const QString tStart = QString::fromLatin1(reloj::isoUtc(tStartMs));
    const QString carpeta = QDir::toNativeSeparators(dirCorrida);
    const QString dirPropio = QCoreApplication::applicationDirPath();

    for (const DefinicionMedidor& def : definiciones_) {
        Estado e;
        e.def = def;
        e.listoEnTStart = def.archivoListo.isEmpty() ? -1 : 0;

        QStringList args;
        for (QString a : def.argumentos) {
            a.replace("{T_START_UTC}", tStart);
            a.replace("{CARPETA_CORRIDA}", carpeta);
            a.replace("{DURACION_MAX_S}", QString::number(DURACION_MAXIMA_S));
            a.replace("{PID_ORQUESTADOR}", QString::number(pidMedido));
            args << a;
        }

        // El programa junto al orquestador tiene prioridad sobre el PATH
        QString programa = def.programa;
        const QFileInfo local(QDir(dirPropio).filePath(programa));
        if (QFileInfo(programa).isRelative() && local.exists()) programa = local.absoluteFilePath();
        if (QFileInfo(programa).isAbsolute() && !QFileInfo::exists(programa)) {
            *error = QString("El medidor '%1' no arranco: no existe %2").arg(def.nombre, QDir::toNativeSeparators(programa));
            if (!def.ayudaSiFalta.isEmpty()) *error += ". " + def.ayudaSiFalta;
            return false;
        }

        e.proceso = new QProcess(this);
        e.proceso->setWorkingDirectory(dirPropio);
        // Los medidores en Python escriben en UTF-8 (stdout y stderr) para un diagnostico legible
        QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
        entorno.insert("PYTHONIOENCODING", "utf-8");
        e.proceso->setProcessEnvironment(entorno);
        // stdout de EnergIA es solo para eventos del protocolo: la salida del medidor va al diagnostico
        e.proceso->setProcessChannelMode(QProcess::MergedChannels);
        const int indice = medidores_.size();
        QProcess* proc = e.proceso;
        const QString nombre = def.nombre;
        connect(proc, &QProcess::readyReadStandardOutput, this, [proc, nombre] {
            const QList<QByteArray> lineas = proc->readAllStandardOutput().split('\n');
            for (const QByteArray& l : lineas)
                if (!l.trimmed().isEmpty())
                    protocolo::diagnostico(QString("[medidor %1] %2").arg(nombre, QString::fromUtf8(l.trimmed())));
        });
        connect(proc, &QProcess::finished, this,
                [this, indice](int codigo, QProcess::ExitStatus st) { alTerminar(indice, codigo, st); });

        proc->start(programa, args);
        medidores_.append(e);
        if (!proc->waitForStarted(ESPERA_ARRANQUE_MS)) {
            *error = QString("El medidor '%1' no arranco: %2").arg(def.nombre, proc->errorString());
            return false;
        }
        medidores_[indice].pid = proc->processId();
        QJsonObject det;
        det["pid"] = medidores_[indice].pid;
        det["archivos"] = QJsonArray::fromStringList(def.archivos);
        emit evento(def.nombre, "lanzado", det);
        protocolo::diagnostico(QString("Medidor '%1' lanzado (PID %2)").arg(def.nombre).arg(medidores_[indice].pid));
    }
    return true;
}

QStringList GestorMedidores::sinConfirmarListos()
{
    QStringList faltan;
    for (Estado& e : medidores_) {
        // Un medidor que ya termino no va a medir la corrida, haya creado o no su archivo listo
        if (e.terminado) {
            faltan << QString("'%1' (termino antes de t_start, codigo %2)").arg(e.def.nombre).arg(e.codigoSalida);
            continue;
        }
        if (e.def.archivoListo.isEmpty()) continue;
        const bool listo = QFile::exists(QDir(dirCorrida_).filePath(e.def.archivoListo));
        e.listoEnTStart = listo ? 1 : 0;
        if (!listo) faltan << QString("'%1' (falta %2)").arg(e.def.nombre, e.def.archivoListo);
    }
    return faltan;
}

void GestorMedidores::detener()
{
    if (deteniendo_) return;
    deteniendo_ = true;
    if (medidores_.isEmpty()) { avisarDetenidos(); return; }

    QFile bandera(QDir(dirCorrida_).filePath(ARCHIVO_STOP));
    if (bandera.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        bandera.write(reloj::isoUtc(reloj::ahoraMs()));
        bandera.close();
    } else {
        protocolo::diagnostico("No se pudo crear stop.flag; se forzara el cierre de los medidores");
    }
    if (todosTerminados()) { avisarDetenidos(); return; }
    timerForzar_.start(ESPERA_CIERRE_MS);
}

void GestorMedidores::alTerminar(int indice, int codigo, QProcess::ExitStatus status)
{
    if (indice < 0 || indice >= medidores_.size()) return;
    Estado& e = medidores_[indice];
    e.terminado = true;
    e.codigoSalida = (status == QProcess::NormalExit) ? codigo : -2;
    QJsonObject det;
    det["codigo"] = e.codigoSalida;
    if (!deteniendo_) {
        e.terminoAntes = true;
        protocolo::diagnostico(QString("El medidor '%1' termino antes de tiempo (codigo %2)").arg(e.def.nombre).arg(codigo));
        emit evento(e.def.nombre, "termino_antes", det);
    } else if (!e.forzado) {
        emit evento(e.def.nombre, "terminado", det);
    }
    if (deteniendo_ && todosTerminados()) avisarDetenidos();
}

void GestorMedidores::forzarCierre()
{
    if (matados_) {
        // Ni stop.flag ni kill() lo cerraron a tiempo: se sigue sin esperarlo mas
        for (const Estado& e : medidores_)
            if (e.proceso && e.proceso->state() != QProcess::NotRunning)
                protocolo::diagnostico(QString("El medidor '%1' sigue en ejecucion tras cerrarlo a la fuerza").arg(e.def.nombre));
        avisarDetenidos();
        return;
    }
    matados_ = true;
    for (Estado& e : medidores_) {
        if (e.proceso && e.proceso->state() != QProcess::NotRunning) {
            e.forzado = true;
            protocolo::diagnostico(QString("El medidor '%1' no respondio a stop.flag; se cierra a la fuerza").arg(e.def.nombre));
            emit evento(e.def.nombre, "forzado", QJsonObject());
            e.proceso->kill();  // sin espera bloqueante: el fin llega por QProcess::finished
        }
    }
    if (todosTerminados()) { avisarDetenidos(); return; }
    timerForzar_.start(ESPERA_KILL_MS);
}

bool GestorMedidores::todosTerminados() const
{
    for (const Estado& e : medidores_)
        if (e.proceso && e.proceso->state() != QProcess::NotRunning) return false;
    return true;
}

void GestorMedidores::avisarDetenidos()
{
    if (avisado_) return;
    avisado_ = true;
    timerForzar_.stop();
    QMetaObject::invokeMethod(this, [this] { emit todosDetenidos(); }, Qt::QueuedConnection);
}

QJsonArray GestorMedidores::resumen() const
{
    QJsonArray a;
    for (const Estado& e : medidores_) {
        QJsonObject m;
        m["nombre"] = e.def.nombre;
        m["programa"] = QFileInfo(e.def.programa).fileName();
        m["pid"] = e.pid;
        m["archivos"] = QJsonArray::fromStringList(e.def.archivos);
        m["codigo_salida"] = e.terminado ? QJsonValue(e.codigoSalida) : QJsonValue(QJsonValue::Null);
        m["termino_antes"] = e.terminoAntes;
        m["forzado"] = e.forzado;
        m["listo_en_t_start"] = e.listoEnTStart < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(e.listoEnTStart == 1);
        a.append(m);
    }
    return a;
}
