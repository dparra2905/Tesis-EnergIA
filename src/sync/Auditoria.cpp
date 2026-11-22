// Auditoria.cpp - ver Auditoria.h.
#include "Auditoria.h"

#include "AuditoriaReglas.h"
#include "FasesCorrida.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace auditoria {

namespace {

namespace R = reglas;

constexpr qint64 INTERVALO_MS = 1000;  // rejilla de 1 s (D-007)

QJsonObject leerJson(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

qint64 ms(const QJsonValue& v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

QString pct(double fraccion) { return QString::number(100.0 * fraccion, 'f', 1) + " %"; }
QString puntos(double v) { return QString::number(v, 'f', 1) + " %"; }

// Orden de gravedad: no_evaluable no cuenta
int gravedad(const QString& resultado)
{
    if (resultado == OK) return 0;
    if (resultado == ADVERTENCIA) return 1;
    if (resultado == NO_VALIDA) return 2;
    return -1;
}

QString peor(const QString& a, const QString& b) { return gravedad(b) > gravedad(a) ? b : a; }

struct Comprobacion {
    QString id;
    QString descripcion;
    QJsonValue valor = QJsonValue::Null;
    QString unidad;
    QString umbral;
    QString resultado = NO_EVALUABLE;
    QString motivo;
    QJsonArray detalle;

    QJsonObject aJson() const
    {
        QJsonObject o;
        o["id"] = id;
        o["descripcion"] = descripcion;
        o["valor"] = valor;
        o["unidad"] = unidad.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(unidad);
        o["umbral"] = umbral;
        o["resultado"] = resultado;
        o["motivo"] = motivo;
        if (!detalle.isEmpty()) o["detalle"] = detalle;
        return o;
    }
    Comprobacion& noEvaluable(const QString& m)
    {
        resultado = NO_EVALUABLE;
        motivo = m;
        return *this;
    }
};

// ------------------------------------------------------------------------- estado
QString describirCancelacion(const QJsonObject& m)
{
    const QJsonObject t = m["tiempos"].toObject();
    if (ms(t["t_start"]) <= 0) return "cancelada durante la preparación de los datos: no se midió nada";
    if (ms(t["t_stop"]) > 0 && ms(t["t_stop"]) < ms(t["t_start"])) return "cancelada en el margen de inicio: no se midió nada";
    if (ms(t["t_antes"]) <= 0) return "cancelada durante la línea base previa: no hubo entrenamiento";
    if (m["entrenamiento"].toObject()["status_dll"].toInt(-1) != 0)
        return "cancelada durante el entrenamiento: la ventana de entrenamiento es parcial";
    if (t["fase_posterior_iniciada"].toBool(false))
        return "cancelada durante la línea base posterior: el entrenamiento está completo";
    // Inferencia (D-014): reglas versión 2
    if (t["inferencia_iniciada"].toBool(false)) return "cancelada durante la inferencia: el entrenamiento está completo";
    if (t["fase_media_iniciada"].toBool(false))
        return "cancelada durante la línea base intermedia: el entrenamiento está completo";
    return "cancelada al terminar el entrenamiento: el entrenamiento está completo";
}

QString describirError(const QJsonObject& m)
{
    const QJsonObject e = m["error"].toObject();
    const QString tipo = e["tipo"].toString();
    const QString mensaje = e["mensaje"].toString();
    const QString sufijo = mensaje.isEmpty() ? QString() : " (" + mensaje + ")";
    if (tipo == "dll") return "error de la DLL durante el entrenamiento" + sufijo;
    if (tipo == "inferencia") return "error de inferir: el entrenamiento está completo" + sufijo;
    if (tipo == "medidor")
        return QString("error de un medidor") + (ms(m["tiempos"].toObject()["t_antes"]) > 0 ? "" : ": no hubo entrenamiento")
               + sufijo;
    if (tipo == "datos") return "error en los datos: no se midió nada" + sufijo;
    return "error" + (tipo.isEmpty() ? QString() : " de tipo " + tipo) + sufijo;
}

Comprobacion estado(const QJsonObject& m)
{
    Comprobacion c;
    c.id = "estado";
    c.descripcion = "Estado final de la corrida y en qué fase terminó";
    c.umbral = "completada = ok; cancelada, error o detención forzada = advertencia (nunca no válida)";
    const QString e = m["estado"].toString();
    c.valor = e;
    if (e == "completada") {
        c.resultado = OK;
        c.motivo = "corrida completada";
    } else {
        c.resultado = ADVERTENCIA;
        c.motivo = e == "cancelada" ? describirCancelacion(m)
                 : e == "error" ? describirError(m)
                 : e == "en_curso" ? QString("detención forzada: corrida incompleta")
                                   : "estado desconocido: " + e;
    }
    return c;
}

// ------------------------------------------------------------------------- pasos
// Ultimo step_total de progreso.csv (-1 si no hay filas)
int stepTotal(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
    QTextStream in(&f);
    const QStringList cab = in.readLine().trimmed().split(',');
    const int c = cab.indexOf("step_total");
    int total = -1;
    while (!in.atEnd()) {
        const QStringList fila = in.readLine().trimmed().split(',');
        bool ok = false;
        const int v = (c >= 0 && c < fila.size()) ? fila[c].toInt(&ok) : 0;
        if (ok) total = v;
    }
    return total;
}

Comprobacion pasos(const QJsonObject& m, const QString& dir)
{
    Comprobacion c;
    c.id = "pasos";
    c.descripcion = "Pasos del entrenamiento registrados";
    c.umbral = "corrida completada: pasos registrados = step_total del entrenamiento";
    if (m["estado"].toString() != "completada") return c.noEvaluable("la corrida no está completada");
    const int registrados = m["entrenamiento"].toObject()["pasos_registrados"].toInt();
    const int total = stepTotal(QDir(dir).filePath("progreso.csv"));
    c.valor = registrados;
    c.unidad = "pasos";
    const bool ok = total >= 0 && registrados == total;
    c.resultado = ok ? OK : NO_VALIDA;
    c.motivo = total < 0 ? "progreso.csv no tiene pasos"
             : ok ? QString("%1 de %2 pasos registrados").arg(registrados).arg(total)
                  : QString("se registraron %1 pasos y el entrenamiento declaraba %2").arg(registrados).arg(total);
    return c;
}

// ------------------------------------------------------------------------- inferencia (D-014)
bool conInferencia(const QJsonObject& m) { return m["inferencia"].toObject()["activada"].toBool(false); }

Comprobacion pasosInferencia(const QJsonObject& m)
{
    Comprobacion c;
    c.id = "pasos_inferencia";
    c.descripcion = "Pasadas completas de la inferencia";
    c.umbral = "con la inferencia completa (inferir devolvió OK): al menos una pasada completa";
    if (!conInferencia(m)) return c.noEvaluable("la corrida no tiene la inferencia activada");
    const QJsonObject inf = m["inferencia"].toObject();
    if (inf["status_dll"].toInt(-1) != 0) return c.noEvaluable("la inferencia no terminó completa");
    const qint64 pasadas = inf["pasadas"].toInteger(0);
    c.valor = static_cast<double>(pasadas);
    c.unidad = "pasadas";
    c.resultado = pasadas >= 1 ? OK : NO_VALIDA;
    c.motivo = pasadas >= 1 ? QString("%1 pasada(s) completa(s) en %2 grupo(s)").arg(pasadas).arg(inf["grupos_registrados"].toInt())
                            : QString("inferir terminó sin ninguna pasada completa");
    return c;
}

// ------------------------------------------------------------------------- medidores
QString significadoCodigo(const QString& medidor, int codigo)
{
    if (codigo == -2) return "fin anormal del proceso";
    if (medidor == "sistema") {
        switch (codigo) {
        case 0: return "terminó por stop.flag o por el tope de duración";
        case 2: return "argumentos inválidos";
        case 3: return "carpeta inexistente o no se pudo crear sistema_metrics.csv";
        case 5: return "el orquestador terminó antes de crear stop.flag";
        default: break;
        }
    } else if (medidor == "energia") {
        switch (codigo) {
        case 0: return "terminó por stop.flag y recibió datos";
        case 1: return "error inesperado";
        case 2: return "terminó por el tope de duración";
        case 3: return "nunca recibió una trama válida";
        case 4: return "argumentos o configuración inválidos";
        case 5: return "el orquestador terminó antes de crear stop.flag";
        default: break;
        }
    }
    return "código desconocido";
}

Comprobacion medidores(const QJsonObject& m)
{
    Comprobacion c;
    c.id = "medidores";
    c.descripcion = "Código de salida y cierre de cada medidor";
    c.umbral = "ok: código 0 sin cierre forzado; advertencia: código distinto de 0; no válida: cerrado a la fuerza, "
               "terminó antes de tiempo o sin código de salida";
    const QJsonArray lista = m["medidores"].toArray();
    if (lista.isEmpty()) return c.noEvaluable("la corrida no tuvo medidores");
    QStringList motivos;
    QString resultado = OK;
    int problemas = 0;
    for (const QJsonValue& v : lista) {
        const QJsonObject x = v.toObject();
        const QString nombre = x["nombre"].toString();
        const QJsonValue codigo = x["codigo_salida"];
        QString r = OK, motivo;
        if (x["forzado"].toBool()) {
            r = NO_VALIDA;
            motivo = "se cerró a la fuerza (no respondió a stop.flag)";
        } else if (x["termino_antes"].toBool()) {
            r = NO_VALIDA;
            motivo = codigo.isDouble() ? QString("terminó antes de tiempo (código %1: %2)").arg(codigo.toInt())
                                             .arg(significadoCodigo(nombre, codigo.toInt()))
                                       : QString("terminó antes de tiempo");
        } else if (!codigo.isDouble()) {
            r = NO_VALIDA;
            motivo = "no terminó (sin código de salida)";
        } else if (codigo.toInt() != 0) {
            r = ADVERTENCIA;
            motivo = QString("código %1: %2").arg(codigo.toInt()).arg(significadoCodigo(nombre, codigo.toInt()));
        } else {
            motivo = "código 0: " + significadoCodigo(nombre, 0);
        }
        if (r != OK) ++problemas;
        resultado = peor(resultado, r);
        motivos << nombre + ": " + motivo;
        c.detalle.append(QJsonObject{{"medidor", nombre}, {"valor", codigo}, {"forzado", x["forzado"].toBool()},
                                     {"termino_antes", x["termino_antes"].toBool()}, {"resultado", r}, {"motivo", motivo}});
    }
    c.valor = problemas;
    c.unidad = "medidores con problemas";
    c.resultado = resultado;
    c.motivo = motivos.join("; ");
    return c;
}

// ------------------------------------------------------------------------- coberturas
// Comprobacion de la inferencia en una corrida sin la inferencia activada (D-014)
Comprobacion sinInferencia(const QString& id, const QString& descripcion)
{
    Comprobacion c;
    c.id = id;
    c.descripcion = descripcion;
    c.umbral = QString("ok >= %1; advertencia de %2 a %1; no válida < %2").arg(pct(R::COBERTURA_OK), pct(R::COBERTURA_MINIMA));
    c.unidad = "%";
    return c.noEvaluable("la corrida no tiene la inferencia activada");
}

bool midio(const QJsonObject& m, const QString& medidor)
{
    for (const QJsonValue& v : m["medidores"].toArray())
        if (v.toObject()["nombre"].toString() == medidor) return true;
    return false;
}

// Cobertura sin los huecos de borde: segundos cubiertos / (duracion - huecos borde_*). < 0 si no
// se puede evaluar (la ventana solo tiene bordes)
double coberturaSinBordes(const QJsonObject& v)
{
    double bordes = 0;
    for (const QJsonValue& h : v["huecos"].toArray()) {
        const QString tipo = h.toObject()["tipo"].toString();
        if (tipo == "borde_inicial" || tipo == "borde_final") bordes += h.toObject()["duracion_s"].toDouble();
    }
    const double efectiva = v["duracion_s"].toDouble() - bordes;
    if (efectiva <= 0) return -1;
    return std::min(1.0, v["segundos_cubiertos"].toDouble() / efectiva);
}

Comprobacion cobertura(const QString& id, const QString& nombreVentana, const QJsonObject& m, const QJsonObject& res)
{
    Comprobacion c;
    c.id = id;
    c.descripcion = "Cobertura de energía en la ventana de " + nombreVentana + ", sin contar los huecos de borde";
    c.umbral = QString("ok >= %1; advertencia de %2 a %1; no válida < %2").arg(pct(R::COBERTURA_OK), pct(R::COBERTURA_MINIMA));
    c.unidad = "%";
    if (!midio(m, "energia")) return c.noEvaluable("la corrida no incluyó el monitor físico de energía");
    if (res.isEmpty()) return c.noEvaluable("no hay resultados.json");
    const QJsonObject v = res["energia_experimento"].toObject()["ventanas"].toObject()[nombreVentana].toObject();
    if (v.isEmpty()) return c.noEvaluable("la corrida no tiene ventana de " + nombreVentana);
    const double cob = coberturaSinBordes(v);
    if (cob < 0) return c.noEvaluable("la ventana de " + nombreVentana + " solo tiene huecos de borde");
    c.valor = 100.0 * cob;
    c.resultado = cob >= R::COBERTURA_OK ? OK : cob >= R::COBERTURA_MINIMA ? ADVERTENCIA : NO_VALIDA;
    c.motivo = "cobertura de " + pct(cob) + " sin los huecos de borde";
    return c;
}

Comprobacion coberturaLineasBase(const QJsonObject& m, const QJsonObject& res)
{
    Comprobacion c;
    c.id = "cobertura_lineas_base";
    c.descripcion = "Cobertura de energía de cada ventana de línea base (base_pre, base_media, base_post), sin contar "
                    "los huecos de borde";
    c.umbral = QString("ok >= %1; advertencia < %1").arg(pct(R::COBERTURA_LINEA_BASE_OK));
    c.unidad = "%";
    if (!midio(m, "energia")) return c.noEvaluable("la corrida no incluyó el monitor físico de energía");
    if (res.isEmpty()) return c.noEvaluable("no hay resultados.json");
    const QJsonObject ventanas = res["energia_experimento"].toObject()["ventanas"].toObject();
    QString resultado = NO_EVALUABLE;
    double minima = 2;
    QStringList motivos;
    for (const char* nombre : {fases::BASE_PRE, fases::BASE_MEDIA, fases::BASE_POST}) {
        if (!ventanas.contains(nombre)) continue;
        const double cob = coberturaSinBordes(ventanas[nombre].toObject());
        QJsonObject d{{"ventana", nombre}};
        if (cob < 0) {
            d["valor"] = QJsonValue::Null;
            d["resultado"] = NO_EVALUABLE;
            motivos << QString(nombre) + ": solo huecos de borde";
        } else {
            const QString r = cob >= R::COBERTURA_LINEA_BASE_OK ? OK : ADVERTENCIA;
            d["valor"] = 100.0 * cob;
            d["resultado"] = r;
            resultado = resultado == NO_EVALUABLE ? r : peor(resultado, r);
            minima = std::min(minima, cob);
            motivos << QString(nombre) + ": " + pct(cob);
        }
        c.detalle.append(d);
    }
    if (c.detalle.isEmpty()) return c.noEvaluable("la corrida no tiene ventanas de línea base");
    if (resultado == NO_EVALUABLE) return c.noEvaluable("las ventanas de línea base solo tienen huecos de borde");
    c.valor = 100.0 * minima;
    c.resultado = resultado;
    c.motivo = motivos.join("; ");
    return c;
}

// ------------------------------------------------------------------------- medidores: info
Comprobacion caidasBle(const QJsonObject& m, const QString& dir)
{
    Comprobacion c;
    c.id = "caidas_ble";
    c.descripcion = "Caídas de la conexión Bluetooth con el Atorch (energia_info.json, cierre.caidas)";
    c.umbral = QString("ok de 0 a %1; advertencia >= %2").arg(R::CAIDAS_BLE_MAXIMAS_OK).arg(R::CAIDAS_BLE_MAXIMAS_OK + 1);
    c.unidad = "caídas";
    if (!midio(m, "energia")) return c.noEvaluable("la corrida no incluyó el monitor físico de energía");
    const QJsonObject info = leerJson(QDir(dir).filePath("energia_info.json"));
    const QJsonValue caidas = info["cierre"].toObject()["caidas"];
    if (!caidas.isDouble()) return c.noEvaluable("energia_info.json no tiene cierre.caidas");
    c.valor = caidas.toInt();
    c.resultado = caidas.toInt() <= R::CAIDAS_BLE_MAXIMAS_OK ? OK : ADVERTENCIA;
    c.motivo = QString("%1 caída(s) de la conexión").arg(caidas.toInt());
    return c;
}

Comprobacion muestrasSaltadas(const QJsonObject& m, const QString& dir)
{
    Comprobacion c;
    c.id = "muestras_saltadas";
    c.descripcion = "Puntos de la rejilla que el monitor de software saltó por atraso (sistema_info.json: muestras_saltadas / "
                    "filas_escritas)";
    c.umbral = QString("ok <= %1; advertencia de %1 a %2; no válida > %2").arg(pct(R::SALTADAS_OK), pct(R::SALTADAS_MAXIMA));
    c.unidad = "%";
    if (!midio(m, "sistema")) return c.noEvaluable("la corrida no incluyó el monitor de software");
    const QJsonObject info = leerJson(QDir(dir).filePath("sistema_info.json"));
    if (!info["muestras_saltadas"].isDouble() || !info["filas_escritas"].isDouble())
        return c.noEvaluable("sistema_info.json no tiene muestras_saltadas o filas_escritas");
    const double filas = info["filas_escritas"].toDouble(), saltadas = info["muestras_saltadas"].toDouble();
    if (filas <= 0) return c.noEvaluable("el monitor de software no escribió filas");
    const double f = saltadas / filas;
    c.valor = 100.0 * f;
    c.resultado = f <= R::SALTADAS_OK ? OK : f <= R::SALTADAS_MAXIMA ? ADVERTENCIA : NO_VALIDA;
    c.motivo = QString("%1 muestra(s) saltada(s) en %2 filas (%3)").arg(saltadas).arg(filas).arg(pct(f));
    return c;
}

// ------------------------------------------------------------------------- CPU ajena
struct FilaCpu { qint64 ms = 0; double residuo = 0; };

QList<FilaCpu> leerResiduos(const QString& ruta)
{
    QList<FilaCpu> filas;
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return filas;
    QTextStream in(&f);
    const QStringList cab = in.readLine().trimmed().split(',');
    const int cT = cab.indexOf("timestamp"), cO = cab.indexOf("cpu_sis_ocupado_s"), cTot = cab.indexOf("cpu_sis_total_s"),
              cQ = cab.indexOf("orq_cpu_s"), cM = cab.indexOf("med_cpu_s");
    if (cT < 0 || cO < 0 || cTot < 0 || cQ < 0) return filas;
    while (!in.atEnd()) {
        const QStringList c = in.readLine().trimmed().split(',');
        auto num = [&c](int i, double* v) {
            if (i < 0 || i >= c.size() || c[i].isEmpty()) return false;
            bool ok = false;
            *v = c[i].toDouble(&ok);
            return ok;
        };
        double ocupado = 0, total = 0, orq = 0, med = 0;
        if (cT >= c.size() || !num(cO, &ocupado) || !num(cTot, &total) || !num(cQ, &orq)) continue;
        num(cM, &med);  // sin el consumo propio del medidor se toma 0
        FilaCpu fila;
        fila.ms = ms(QJsonValue(c[cT]));
        if (fila.ms > 0 && residuoCpu(ocupado, total, orq, med, &fila.residuo)) filas.append(fila);
    }
    return filas;
}

struct Referencia { double valor = R::RESIDUO_REFERENCIA_DEFECTO; int filas = 0; double mediaBase = -1; };

Referencia referencia(const QList<FilaCpu>& filas, const fases::Marcas& marcas)
{
    Referencia r;
    double suma = 0;
    for (const FilaCpu& f : filas)
        if (fases::faseFila(marcas, f.ms) == fases::BASE_PRE) {
            suma += f.residuo;
            ++r.filas;
        }
    if (r.filas > 0) r.mediaBase = suma / r.filas;
    if (r.filas >= R::FILAS_REFERENCIA) r.valor = r.mediaBase;
    return r;
}

Comprobacion actividadAjena(const QJsonObject& m, const QList<FilaCpu>& filas, const fases::Marcas& marcas, const Referencia& ref)
{
    Comprobacion c;
    c.id = "actividad_ajena";
    c.descripcion = "Minutos de entrenamiento e inferencia con CPU ajena al proceso medido: residuo de CPU medio del "
                    "minuto mayor que la referencia + " + puntos(R::MARGEN_ACTIVIDAD);
    c.umbral = QString("ok <= %1 de los minutos; advertencia > %1 (nunca no válida)").arg(pct(R::MINUTOS_AJENOS_OK));
    c.unidad = "% de los minutos";
    if (!midio(m, "sistema")) return c.noEvaluable("la corrida no incluyó el monitor de software");
    const double umbral = ref.valor + R::MARGEN_ACTIVIDAD;
    QString resultado = NO_EVALUABLE;
    double maximo = -1;
    QStringList motivos;
    // Una fase por elemento: una fase medida nueva se agrega a esta lista
    struct Fase { const char* nombre; fases::Intervalo intervalo; };
    for (const Fase& fase : {Fase{fases::ENTRENAMIENTO, fases::entrenamiento(marcas)},
                             Fase{fases::INFERENCIA, fases::inferencia(marcas)}}) {
        if (!fase.intervalo.existe) continue;
        // Minuto de cada fila: segun el inicio de su intervalo (t - 1 s), desde el inicio de la fase
        QMap<qint64, QPair<double, int>> minutos;
        for (const FilaCpu& f : filas) {
            if (fases::faseFila(marcas, f.ms) != fase.nombre) continue;
            auto& x = minutos[(f.ms - INTERVALO_MS - fase.intervalo.desdeMs) / R::MINUTO_MS];
            x.first += f.residuo;
            ++x.second;
        }
        if (minutos.isEmpty()) continue;
        int conActividad = 0;
        for (const auto& x : minutos)
            if (x.first / x.second > umbral) ++conActividad;
        const double f = static_cast<double>(conActividad) / static_cast<double>(minutos.size());
        const QString r = f <= R::MINUTOS_AJENOS_OK ? OK : ADVERTENCIA;
        resultado = resultado == NO_EVALUABLE ? r : peor(resultado, r);
        maximo = std::max(maximo, f);
        c.detalle.append(QJsonObject{{"fase", fase.nombre}, {"minutos", static_cast<int>(minutos.size())},
                                     {"minutos_con_actividad", conActividad}, {"valor", 100.0 * f}, {"resultado", r}});
        motivos << QString("%1: %2 de %3 minutos (%4)").arg(fase.nombre).arg(conActividad).arg(minutos.size()).arg(pct(f));
    }
    if (resultado == NO_EVALUABLE)
        return c.noEvaluable("no hay filas del monitor de software en el entrenamiento ni en la inferencia");
    c.valor = 100.0 * maximo;
    c.resultado = resultado;
    c.motivo = motivos.join("; ") + QString(" con residuo mayor que %1 (referencia %2 %3)")
                                        .arg(puntos(umbral), puntos(ref.valor),
                                             ref.filas >= R::FILAS_REFERENCIA ? QString("de la base previa")
                                                                              : QString("por defecto: la base previa tiene menos de %1 filas")
                                                                                    .arg(R::FILAS_REFERENCIA));
    return c;
}

Comprobacion reposoBasePrevia(const QJsonObject& m, const Referencia& ref)
{
    Comprobacion c;
    c.id = "reposo_base_previa";
    c.descripcion = "Residuo de CPU medio en la línea base previa (el equipo debería estar en reposo)";
    c.umbral = QString("ok <= %1; advertencia > %1").arg(puntos(R::RESIDUO_REPOSO_OK));
    c.unidad = "%";
    if (!midio(m, "sistema")) return c.noEvaluable("la corrida no incluyó el monitor de software");
    if (ref.filas == 0) return c.noEvaluable("la base previa no tiene filas del monitor de software");
    c.valor = ref.mediaBase;
    c.resultado = ref.mediaBase <= R::RESIDUO_REPOSO_OK ? OK : ADVERTENCIA;
    c.motivo = QString("residuo medio de %1 en %2 filas de la base previa").arg(puntos(ref.mediaBase)).arg(ref.filas);
    return c;
}

} // namespace

bool residuoCpu(double ocupadoS, double totalS, double orquestadorS, double medidorS, double* residuo)
{
    if (!(totalS > 0)) return false;
    *residuo = 100.0 * (ocupadoS - orquestadorS - medidorS) / totalS;
    return true;
}

bool auditar(const QString& dirCorrida, QJsonObject* auditoria, QString* error)
{
    const QDir dir(dirCorrida);
    const QJsonObject m = leerJson(dir.filePath("metadata.json"));
    if (m.isEmpty()) {
        *error = "La carpeta no contiene una corrida (falta metadata.json)";
        return false;
    }
    const QJsonObject res = leerJson(dir.filePath("resultados.json"));
    const fases::Marcas marcas = fases::desdeMetadata(m);
    const QList<FilaCpu> filas = midio(m, "sistema") ? leerResiduos(dir.filePath("sistema_metrics.csv")) : QList<FilaCpu>();
    const Referencia ref = referencia(filas, marcas);

    // Para agregar una comprobacion: una funcion mas que devuelve Comprobacion y una linea aqui
    const QList<Comprobacion> comprobaciones = {
        estado(m),
        pasos(m, dirCorrida),
        medidores(m),
        cobertura("cobertura_entrenamiento", fases::ENTRENAMIENTO, m, res),
        conInferencia(m) ? cobertura("cobertura_inferencia", fases::INFERENCIA, m, res)
                         : sinInferencia("cobertura_inferencia", "Cobertura de energía en la ventana de inferencia, sin contar "
                                                                 "los huecos de borde"),
        pasosInferencia(m),
        coberturaLineasBase(m, res),
        caidasBle(m, dirCorrida),
        muestrasSaltadas(m, dirCorrida),
        actividadAjena(m, filas, marcas, ref),
        reposoBasePrevia(m, ref),
    };

    QString veredicto = VALIDA;
    QJsonArray lista;
    for (const Comprobacion& c : comprobaciones) {
        if (gravedad(c.resultado) == 2) veredicto = NO_VALIDA;
        else if (gravedad(c.resultado) == 1 && veredicto == VALIDA) veredicto = CON_ADVERTENCIAS;
        lista.append(c.aJson());
    }
    QJsonObject a;
    a["descripcion"] = "Auditoría de la corrida (D-012): informa; no bloquea la corrida ni la subida";
    a["version_reglas"] = reglas::VERSION_REGLAS;
    a["umbrales"] = "provisionales: se ajustarán con más corridas";
    a["calculada"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    a["id_corrida"] = m["id_corrida"];
    a["veredicto"] = veredicto;
    a["comprobaciones"] = lista;
    *auditoria = a;
    return true;
}

bool auditarYGuardar(const QString& dirCorrida, QJsonObject* auditoria, QString* error)
{
    if (!auditar(dirCorrida, auditoria, error)) return false;
    const QString ruta = QDir(dirCorrida).filePath(ARCHIVO);
    QSaveFile f(ruta);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(*auditoria).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        *error = "No se pudo escribir " + QDir::toNativeSeparators(ruta) + ": " + f.errorString();
        return false;
    }
    return true;
}

} // namespace auditoria
