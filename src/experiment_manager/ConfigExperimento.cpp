// ConfigExperimento.cpp - ver ConfigExperimento.h.
#include "ConfigExperimento.h"

#include "energia_comun.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>

namespace config {

namespace {

constexpr qint64 MAX_DURACION_FASE_MS = 3600 * 1000;  // tope de cordura para cada fase

bool leerDuracion(const QJsonObject& tiempos, const char* clave, qint64* destino, QString* error)
{
    if (!tiempos.contains(clave)) return true;
    const QJsonValue v = tiempos[clave];
    if (!v.isDouble() || v.toDouble() < 0 || v.toDouble() > MAX_DURACION_FASE_MS
        || v.toDouble() != std::floor(v.toDouble())) {
        *error = QString("tiempos.%1 debe ser un entero de 0 a %2 ms").arg(clave).arg(MAX_DURACION_FASE_MS);
        return false;
    }
    *destino = static_cast<qint64>(v.toDouble());
    return true;
}

bool leerEntero(const QJsonObject& objeto, const char* seccion, const char* clave, int minimo, int maximo, int* destino,
                QString* error)
{
    if (!objeto.contains(clave)) return true;
    const QJsonValue v = objeto[clave];
    if (!v.isDouble() || v.toDouble() < minimo || v.toDouble() > maximo || v.toDouble() != std::floor(v.toDouble())) {
        *error = QString("%1.%2 debe ser un entero de %3 a %4").arg(seccion, clave).arg(minimo).arg(maximo);
        return false;
    }
    *destino = static_cast<int>(v.toDouble());
    return true;
}

} // namespace

bool cargar(const QString& ruta, ConfigExperimento* cfg, QString* error)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) { *error = "No se pudo abrir la configuracion: " + QDir::toNativeSeparators(ruta); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = "La configuracion no es un objeto JSON valido: " + pe.errorString();
        return false;
    }
    return desdeJson(doc.object(), cfg, error);
}

bool desdeJson(const QJsonObject& raiz, ConfigExperimento* cfg, QString* error)
{
    *cfg = ConfigExperimento();
    cfg->original = raiz;

    if (raiz["version_config"].toInt(-1) != ConfigExperimento::VERSION) {
        *error = QString("version_config debe ser %1").arg(ConfigExperimento::VERSION);
        return false;
    }

    cfg->experimento = raiz["experimento"].toString().trimmed();
    if (cfg->experimento.isEmpty()) { *error = "Falta 'experimento'"; return false; }

    cfg->dirSalida = raiz["dir_salida"].toString();
    if (cfg->dirSalida.isEmpty()) { *error = "Falta 'dir_salida' (raiz donde se crean las corridas)"; return false; }
    if (!QFileInfo(cfg->dirSalida).isDir()) {
        *error = "dir_salida no existe: " + QDir::toNativeSeparators(cfg->dirSalida);
        return false;
    }

    const QJsonObject modelo = raiz["modelo"].toObject();
    cfg->rutaDll = modelo["dll"].toString();
    if (cfg->rutaDll.isEmpty()) { *error = "Falta 'modelo.dll'"; return false; }
    if (!QFileInfo(cfg->rutaDll).isFile()) { *error = "No existe la DLL del modelo: " + QDir::toNativeSeparators(cfg->rutaDll); return false; }
    if (modelo.contains("hiperparametros") && !modelo["hiperparametros"].isObject()) {
        *error = "'modelo.hiperparametros' debe ser un objeto";
        return false;
    }
    cfg->hiperparametros = modelo["hiperparametros"].toObject();

    const QJsonObject datos = raiz["datos"].toObject();
    const QString origen = datos["origen"].toString();
    if (origen == "sintetico") {
        cfg->usarSintetico = true;
    } else if (origen == "csv") {
        cfg->usarSintetico = false;
        cfg->rutaCsv = datos["ruta"].toString();
        if (cfg->rutaCsv.isEmpty()) { *error = "Falta 'datos.ruta'"; return false; }
        const QJsonValue col = datos["columna_objetivo"];
        if (col.isString() && !col.toString().isEmpty()) cfg->columnaObjetivo = col.toString();
        else if (col.isDouble() && col.toDouble() >= 0) cfg->columnaObjetivoIndice = col.toInt();
        else { *error = "Falta 'datos.columna_objetivo' (nombre o indice)"; return false; }
    } else {
        *error = "'datos.origen' debe ser \"sintetico\" o \"csv\"";
        return false;
    }

    const QJsonObject particion = raiz["particion"].toObject();
    if (particion.contains("porcentaje_entrenamiento")) {
        const int p = particion["porcentaje_entrenamiento"].toInt(-1);
        if (p < 50 || p > 95) { *error = "particion.porcentaje_entrenamiento debe estar entre 50 y 95"; return false; }
        cfg->porcentajeTrain = p;
    }
    if (particion.contains("semilla")) {
        const double s = particion["semilla"].toDouble(-1);
        if (s < 0 || s > 4294967295.0 || s != std::floor(s)) { *error = "particion.semilla debe ser un entero de 0 a 4294967295"; return false; }
        cfg->semilla = static_cast<uint32_t>(s);
    }

    if (raiz.contains("guardar_modelo")) {
        if (!raiz["guardar_modelo"].isBool()) { *error = "'guardar_modelo' debe ser true o false"; return false; }
        cfg->guardarModelo = raiz["guardar_modelo"].toBool();
    }

    if (raiz.contains("medidores")) {
        if (!raiz["medidores"].isObject()) { *error = "'medidores' debe ser un objeto"; return false; }
        const QJsonObject med = raiz["medidores"].toObject();
        for (auto it = med.begin(); it != med.end(); ++it) {
            if (!it.value().isBool()) { *error = QString("medidores.%1 debe ser true o false").arg(it.key()); return false; }
            if (it.key() == "sistema") {
                cfg->medidorSistema = it.value().toBool();
            } else if (it.key() == "energia") {
                cfg->medidorEnergia = it.value().toBool();
            } else {
                *error = "Medidor desconocido: " + it.key();
                return false;
            }
        }
    }

    if (raiz.contains("energia_ble")) {
        if (!raiz["energia_ble"].isObject()) { *error = "'energia_ble' debe ser un objeto"; return false; }
        const QJsonObject ble = raiz["energia_ble"].toObject();
        for (auto it = ble.begin(); it != ble.end(); ++it) {
            if (!it.value().isString()) { *error = QString("energia_ble.%1 debe ser texto").arg(it.key()); return false; }
            if (it.key() == "python") cfg->energiaPython = it.value().toString();
            else if (it.key() == "script") cfg->energiaScript = it.value().toString();
            else if (it.key() == "mac") cfg->energiaMac = it.value().toString().trimmed().toUpper();
            else { *error = "Clave desconocida en energia_ble: " + it.key(); return false; }
        }
        static const QRegularExpression formatoMac("^([0-9A-F]{2}:){5}[0-9A-F]{2}$");
        if (!cfg->energiaMac.isEmpty() && !formatoMac.match(cfg->energiaMac).hasMatch()) {
            *error = "energia_ble.mac invalida (formato AA:BB:CC:DD:EE:FF): " + cfg->energiaMac;
            return false;
        }
    }

    const QJsonObject tiempos = raiz["tiempos"].toObject();
    if (!leerDuracion(tiempos, "margen_inicio_ms", &cfg->margenInicioMs, error)) return false;
    if (!leerDuracion(tiempos, "fase_previa_ms", &cfg->fasePreviaMs, error)) return false;
    if (!leerDuracion(tiempos, "fase_posterior_ms", &cfg->fasePosteriorMs, error)) return false;
    // Linea base intermedia (D-014): si falta, la misma duracion que la fase previa
    cfg->faseMediaMs = cfg->fasePreviaMs;
    if (!leerDuracion(tiempos, "fase_media_ms", &cfg->faseMediaMs, error)) return false;

    if (raiz.contains("inferencia")) {
        if (!raiz["inferencia"].isObject()) { *error = "'inferencia' debe ser un objeto"; return false; }
        const QJsonObject inf = raiz["inferencia"].toObject();
        for (auto it = inf.begin(); it != inf.end(); ++it)
            if (it.key() != "activada" && it.key() != "lote" && it.key() != "duracion_min_s") {
                *error = "Clave desconocida en inferencia: " + it.key();
                return false;
            }
        if (inf.contains("activada")) {
            if (!inf["activada"].isBool()) { *error = "inferencia.activada debe ser true o false"; return false; }
            cfg->inferenciaActivada = inf["activada"].toBool();
        }
        if (!leerEntero(inf, "inferencia", "lote", 1, ConfigExperimento::INFERENCIA_LOTE_MAXIMO, &cfg->inferenciaLote, error))
            return false;
        if (!leerEntero(inf, "inferencia", "duracion_min_s", 1, ConfigExperimento::INFERENCIA_DURACION_MAXIMA_S,
                        &cfg->inferenciaDuracionMinS, error))
            return false;
    }
    return true;
}

bool resolverHiperparametros(const ModeloDisponible& modelo, const QJsonObject& solicitados, EnergiaParams* params,
                             QJsonObject* usados, QString* error)
{
    CatalogoModelos::valoresPorDefecto(modelo, params);

    // Claves desconocidas: error (no se ignoran en silencio)
    for (auto it = solicitados.begin(); it != solicitados.end(); ++it) {
        bool existe = false;
        for (const EnergiaHiperparametro& h : modelo.hiper)
            if (QString::fromStdString(energia::leerTexto(h.clave)) == it.key()) existe = true;
        if (!existe) { *error = QString("Hiperparametro desconocido para este modelo: '%1'").arg(it.key()); return false; }
    }

    *usados = QJsonObject();
    for (size_t i = 0; i < modelo.hiper.size(); ++i) {
        const EnergiaHiperparametro& h = modelo.hiper[i];
        EnergiaValor& v = params->valores[i];
        const QString clave = QString::fromStdString(energia::leerTexto(h.clave));
        const QJsonValue pedido = solicitados.value(clave);
        const bool hay = !pedido.isUndefined();

        switch (h.tipo) {
        case ENERGIA_TIPO_ENTERO:
        case ENERGIA_TIPO_REAL:
            if (hay) {
                if (!pedido.isDouble()) { *error = QString("'%1' debe ser numerico").arg(clave); return false; }
                v.numero = pedido.toDouble();
            }
            if (h.tipo == ENERGIA_TIPO_ENTERO) (*usados)[clave] = static_cast<qint64>(v.numero);
            else (*usados)[clave] = v.numero;
            break;
        case ENERGIA_TIPO_OPCION: {
            const std::vector<std::string> ops = energia::separarOpciones(energia::leerTexto(h.opciones));
            if (hay) {
                if (pedido.isString()) {
                    int indice = -1;
                    for (size_t k = 0; k < ops.size(); ++k)
                        if (QString::fromStdString(ops[k]) == pedido.toString()) indice = static_cast<int>(k);
                    if (indice < 0) { *error = QString("'%1': opcion '%2' no existe").arg(clave, pedido.toString()); return false; }
                    v.numero = indice;
                } else if (pedido.isDouble()) {
                    v.numero = pedido.toDouble();
                } else {
                    *error = QString("'%1' debe ser el nombre de una opcion").arg(clave);
                    return false;
                }
            }
            const int indice = static_cast<int>(v.numero);
            (*usados)[clave] = (indice >= 0 && indice < static_cast<int>(ops.size())) ? QString::fromStdString(ops[indice])
                                                                                       : QString::number(indice);
            break;
        }
        case ENERGIA_TIPO_LISTA_ENTEROS:
            if (hay) {
                if (!pedido.isString()) { *error = QString("'%1' debe ser texto, p. ej. \"64,32\"").arg(clave); return false; }
                energia::copiarTexto(v.texto, sizeof(v.texto), pedido.toString().trimmed().toUtf8().constData());
            }
            (*usados)[clave] = QString::fromStdString(energia::leerTexto(v.texto));
            break;
        default:
            break;
        }
    }

    // Misma validacion que hara la DLL: un error aparece antes de empezar la corrida
    const std::string motivo = energia::validarParams(*params, modelo.hiper, nullptr);
    if (!motivo.empty()) { *error = "Hiperparametros: " + QString::fromStdString(motivo); return false; }
    return true;
}

} // namespace config
