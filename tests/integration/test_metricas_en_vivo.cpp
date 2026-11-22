// test_metricas_en_vivo.cpp - Metricas en vivo del monitor de software en el cliente (F5).
//
// Usa el system_meter.exe real sobre una carpeta temporal y le entrega al ClienteOrquestador
// los mismos eventos que emitiria el orquestador ('inicio' con medidores y 'medidor'
// lanzado). Comprueba que el cliente lee sistema_metrics.csv cada segundo y llena
// metricasSistema y historialSistema. El proceso medido es este mismo programa, que ocupa
// un nucleo mientras dura la medicion.

#include "ClienteOrquestador.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

namespace {

int g_fallos = 0;

void comprobar(bool condicion, const QString& descripcion)
{
    std::printf("[%s] %s\n", condicion ? "OK  " : "FALLA", descripcion.toUtf8().constData());
    std::fflush(stdout);
    if (!condicion) ++g_fallos;
}

QByteArray evento(const QString& json) { return json.toUtf8() + '\n'; }

// Nucleos de CPU del proceso medido en cada fila de sistema_metrics.csv: orq_cpu_s / intervalo
QList<double> nucleosPorFila(const QString& ruta)
{
    QList<double> r;
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return r;
    const QStringList cab = QString::fromUtf8(f.readLine()).trimmed().split(',');
    const int cOrq = cab.indexOf("orq_cpu_s"), cInt = cab.indexOf("intervalo_ms");
    while (!f.atEnd()) {
        const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split(',');
        if (cOrq < 0 || cInt < 0 || c.size() <= std::max(cOrq, cInt)) continue;
        const double intervalo = c[cInt].toDouble();
        if (intervalo > 0 && !c[cOrq].isEmpty()) r.append(c[cOrq].toDouble() / (intervalo / 1000.0));
    }
    return r;
}

double mediana(QList<double> v)
{
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    const qsizetype n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString dir = QDir::toNativeSeparators(tmp.path());

    ClienteOrquestador::Opciones o;
    o.rutaOrquestador = "no_se_usa";
    o.dirSalida = tmp.path();
    ClienteOrquestador cliente(o);

    QString dirJson = dir;
    dirJson.replace("\\", "\\\\");
    cliente.procesarSalida(evento(QString(R"({"v":1,"evento":"inicio","t":"x","experimento":"vivo","corrida":1,"dir_corrida":"%1","modelo":"prueba","modelo_interno":true,"medidores":["sistema"]})").arg(dirJson)));
    comprobar(cliente.medidoresCorrida().size() == 1
                  && cliente.medidoresCorrida()[0].toMap().value("estado").toString() == "pendiente",
              "evento inicio: medidor 'sistema' pendiente");

    // Carga de un nucleo en este proceso mientras se mide
    std::atomic<bool> seguir{true};
    std::thread carga([&seguir] {
        volatile double x = 0;
        while (seguir) for (int i = 1; i < 100000; ++i) x = x + std::sqrt(static_cast<double>(i));
    });

    QProcess medidor;
    medidor.start(QString::fromUtf8(ENERGIA_SYSTEM_METER),
                  {"--t-start", "ahora", "--carpeta", dir, "--duration", "5",
                   "--pid", QString::number(QCoreApplication::applicationPid())});
    comprobar(medidor.waitForStarted(5000), "system_meter arranca");
    cliente.procesarSalida(evento(R"({"v":1,"evento":"medidor","t":"x","nombre":"sistema","estado":"lanzado","pid":1})"));

    QElapsedTimer reloj;
    reloj.start();
    int maxHistorial = 0;
    while (medidor.state() != QProcess::NotRunning && reloj.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        maxHistorial = std::max<int>(maxHistorial, cliente.historialSistema().value("cpu_sistema_pct").toList().size());
        QThread::msleep(20);
    }
    medidor.waitForFinished(10000);
    seguir = false;
    carga.join();
    comprobar(medidor.exitCode() == 0, QString("system_meter termina con codigo 0 (%1)").arg(medidor.exitCode()));
    comprobar(maxHistorial >= 2, QString("el cliente acumula metricas mientras se mide (%1 puntos antes del fin)").arg(maxHistorial));

    cliente.procesarSalida(evento(R"({"v":1,"evento":"medidor","t":"x","nombre":"sistema","estado":"terminado","codigo":0})"));
    const QVariantMap ult = cliente.metricasSistema();
    const QVariantMap hist = cliente.historialSistema();
    comprobar(hist.value("cpu_sistema_pct").toList().size() == 5,
              QString("5 filas para 5 s de medicion (%1)").arg(hist.value("cpu_sistema_pct").toList().size()));
    // El hilo de carga ocupa un nucleo, pero en un equipo compartido y saturado (runner de
    // GitHub Actions con la CPU del sistema al 100 %) recibe menos: se midio 0,73. El limite
    // inferior de 0,5 sigue distinguiendo un nucleo de un proceso en reposo (~0) y de dos o mas.
    // Se juzga la mediana de las 5 filas, como en system_meter_py y corrida_orquestador_py: una
    // sola fila depende de lo que hizo el resto del equipo en ese segundo.
    const QList<double> porFila = nucleosPorFila(QDir(tmp.path()).filePath("sistema_metrics.csv"));
    const double nucleos = mediana(porFila);
    comprobar(porFila.size() == 5 && nucleos > 0.5 && nucleos < 1.6,
              QString("CPU del proceso medido ~1 nucleo (mediana %1 de %2 filas)").arg(nucleos, 0, 'f', 3).arg(porFila.size()));
    const double nucleosUltima = ult.value("cpu_proceso_nucleos").toDouble();
    comprobar(porFila.size() == 5 && std::abs(nucleosUltima - porFila.last()) < 1e-6,
              QString("el cliente muestra la CPU de la ultima fila (%1)").arg(nucleosUltima, 0, 'f', 3));
    const double sis = ult.value("cpu_sistema_pct").toDouble();
    comprobar(sis > 0 && sis <= 100, QString("CPU del sistema entre 0 y 100 % (%1)").arg(sis, 0, 'f', 1));
    const double ram = ult.value("ram_sistema_pct").toDouble();
    comprobar(ram > 0 && ram < 100, QString("RAM del sistema entre 0 y 100 % (%1)").arg(ram, 0, 'f', 1));
    comprobar(ult.value("ram_proceso_mb").toDouble() > 1, "RAM del proceso medido registrada");
    comprobar(cliente.medidoresCorrida()[0].toMap().value("estado").toString() == "terminado", "estado del medidor: terminado");

    std::printf("\n%s: %d fallo(s)\n", g_fallos == 0 ? "METRICAS EN VIVO OK" : "METRICAS EN VIVO CON FALLOS", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
