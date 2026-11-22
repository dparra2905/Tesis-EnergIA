// CatalogoModelos.cpp - ver CatalogoModelos.h.
// Origen: adaptado de Back-EnergIA (orquestador.cpp, commit 1ab19f1, autor David Parra).
#include "CatalogoModelos.h"

#include "energia_comun.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLibrary>

#include <cstring>

const char* const CatalogoModelos::CLAVE_PRUEBA = "prueba";

namespace {

QString texto(const char* arreglo, size_t tam)
{
    return QString::fromStdString(energia::leerTexto(arreglo, tam));
}

template <size_t N>
QString texto(const char (&arreglo)[N]) { return texto(arreglo, N); }

QString nombreTipo(int32_t tipo)
{
    switch (tipo) {
    case ENERGIA_TIPO_ENTERO:        return "entero";
    case ENERGIA_TIPO_REAL:          return "real";
    case ENERGIA_TIPO_OPCION:        return "opcion";
    case ENERGIA_TIPO_LISTA_ENTEROS: return "lista_enteros";
    default:                         return "desconocido";
    }
}

QJsonObject hiperparametroAJson(const EnergiaHiperparametro& h)
{
    QJsonObject j;
    j["clave"] = texto(h.clave);
    j["etiqueta"] = texto(h.etiqueta);
    j["ayuda"] = texto(h.ayuda);
    j["tipo"] = nombreTipo(h.tipo);
    switch (h.tipo) {
    case ENERGIA_TIPO_ENTERO:
        j["minimo"] = static_cast<qint64>(h.minimo);
        j["maximo"] = static_cast<qint64>(h.maximo);
        j["por_defecto"] = static_cast<qint64>(h.por_defecto);
        break;
    case ENERGIA_TIPO_REAL:
        j["minimo"] = h.minimo;
        j["maximo"] = h.maximo;
        j["por_defecto"] = h.por_defecto;
        break;
    case ENERGIA_TIPO_OPCION: {
        QJsonArray opciones;
        for (const std::string& op : energia::separarOpciones(energia::leerTexto(h.opciones)))
            opciones.append(QString::fromStdString(op));
        j["opciones"] = opciones;
        j["por_defecto"] = static_cast<int>(h.por_defecto);  // indice de la opcion
        break;
    }
    case ENERGIA_TIPO_LISTA_ENTEROS:
        j["minimo"] = static_cast<qint64>(h.minimo);
        j["maximo"] = static_cast<qint64>(h.maximo);
        j["max_elementos"] = h.max_elementos;
        j["por_defecto"] = texto(h.defecto_texto);
        break;
    default:
        break;
    }
    return j;
}

} // namespace

QString CatalogoModelos::patron()
{
#ifdef Q_OS_WIN
    return QStringLiteral("EnergIA_*.dll");
#else
    return QStringLiteral("EnergIA_*.so");
#endif
}

bool CatalogoModelos::explorar(const QString& directorio)
{
    modelos_.clear();
    descartadas_.clear();
    const QDir dir(directorio);
    directorio_ = dir.absolutePath();
    if (!dir.exists()) return false;

    const QStringList archivos = dir.entryList({patron()}, QDir::Files, QDir::Name);
    for (const QString& archivo : archivos) {
        ModeloDisponible m;
        m.ruta = dir.absoluteFilePath(archivo);
        QString error;
        if (!leerDescripcion(&m, &error)) {
            descartadas_.append({m.ruta, error});
            continue;
        }
        const QString clave = texto(m.info.clave);
        bool repetida = false;
        for (const ModeloDisponible& otro : modelos_)
            if (texto(otro.info.clave) == clave) repetida = true;
        if (repetida) {
            descartadas_.append({m.ruta, QString("clave '%1' repetida").arg(clave)});
            continue;
        }
        m.interno = (clave == QLatin1String(CLAVE_PRUEBA));
        modelos_.append(m);
    }
    return true;
}

bool CatalogoModelos::leerDescripcion(ModeloDisponible* m, QString* error)
{
    QLibrary lib(m->ruta);
    if (!lib.load()) { *error = "no se pudo cargar: " + lib.errorString(); return false; }
    auto fnVersion = reinterpret_cast<FnVersion>(lib.resolve("energia_api_version"));
    auto fnInfo = reinterpret_cast<FnInfo>(lib.resolve("energia_info_modelo"));
    auto fnN = reinterpret_cast<FnNHiper>(lib.resolve("energia_n_hiperparametros"));
    auto fnDesc = reinterpret_cast<FnDescribir>(lib.resolve("energia_describir_hiperparametro"));
    const bool completa = fnVersion && fnInfo && fnN && fnDesc && lib.resolve("entrenar") && lib.resolve("evaluar")
                          && lib.resolve("guardar_modelo") && lib.resolve("liberar_modelo") && lib.resolve("inferir");
    // predecir amplio la version 4 sin cambiar su numero (D-015): se pide aparte para explicar el motivo
    const bool conPredecir = lib.resolve("predecir") != nullptr;
    bool ok = false;
    if (!completa) {
        // Una DLL v3 no exporta inferir: se explica por la version, no por la funcion que falta
        *error = (fnVersion && fnVersion() != ENERGIA_API_VERSION)
                     ? QString("contrato versión %1, el orquestador usa la %2").arg(fnVersion()).arg(ENERGIA_API_VERSION)
                     : QString("no exporta todas las funciones del contrato v%1").arg(ENERGIA_API_VERSION);
    } else if (fnVersion() == ENERGIA_API_VERSION && !conPredecir) {
        *error = QString("no exporta predecir: recompila la DLL con el contrato v%1 actual").arg(ENERGIA_API_VERSION);
    } else if (fnVersion() != ENERGIA_API_VERSION) {
        *error = QString("contrato versión %1, el orquestador usa la %2").arg(fnVersion()).arg(ENERGIA_API_VERSION);
    } else if (fnInfo(&m->info) != ENERGIA_OK || energia::leerTexto(m->info.clave).empty()) {
        *error = "energia_info_modelo fallo o no dio una clave";
    } else {
        const int32_t n = fnN();
        if (n < 0 || n > ENERGIA_MAX_HIPER) {
            *error = QString("declara %1 hiperparametros (maximo %2)").arg(n).arg(ENERGIA_MAX_HIPER);
        } else {
            ok = true;
            for (int32_t i = 0; i < n && ok; ++i) {
                EnergiaHiperparametro h;
                std::memset(&h, 0, sizeof(h));
                if (fnDesc(i, &h) != ENERGIA_OK) { *error = QString("no describe el hiperparametro %1").arg(i); ok = false; }
                else m->hiper.push_back(h);
            }
            // Los valores por defecto deben pasar la misma validacion que usara la DLL
            if (ok) {
                EnergiaParams p;
                valoresPorDefecto(*m, &p);
                const std::string motivo = energia::validarParams(p, m->hiper, nullptr);
                if (!motivo.empty()) { *error = "descripcion invalida: " + QString::fromStdString(motivo); ok = false; }
            }
        }
    }
    lib.unload();
    return ok;
}

void CatalogoModelos::valoresPorDefecto(const ModeloDisponible& m, EnergiaParams* p)
{
    std::memset(p, 0, sizeof(*p));
    p->n_valores = static_cast<int32_t>(m.hiper.size());
    for (size_t i = 0; i < m.hiper.size(); ++i) {
        const EnergiaHiperparametro& h = m.hiper[i];
        EnergiaValor& v = p->valores[i];
        std::memcpy(v.clave, h.clave, sizeof(v.clave));
        v.numero = h.por_defecto;
        std::memcpy(v.texto, h.defecto_texto, sizeof(v.texto));
    }
}

QJsonObject CatalogoModelos::modeloAJson(const ModeloDisponible& m)
{
    QJsonObject j;
    j["archivo"] = QFileInfo(m.ruta).fileName();
    j["ruta"] = QDir::toNativeSeparators(m.ruta);
    j["clave"] = texto(m.info.clave);
    j["nombre"] = texto(m.info.nombre);
    j["libreria"] = texto(m.info.libreria);
    j["unidad_paso"] = texto(m.info.unidad_paso);
    j["hilos"] = m.info.hilos;
    j["interno"] = m.interno;
    QJsonArray hiper;
    for (const EnergiaHiperparametro& h : m.hiper) hiper.append(hiperparametroAJson(h));
    j["hiperparametros"] = hiper;
    return j;
}

QJsonObject CatalogoModelos::aJson() const
{
    QJsonArray modelos;
    for (const ModeloDisponible& m : modelos_) modelos.append(modeloAJson(m));
    QJsonArray descartadas;
    for (const DllDescartada& d : descartadas_) {
        QJsonObject j;
        j["archivo"] = QFileInfo(d.ruta).fileName();
        j["ruta"] = QDir::toNativeSeparators(d.ruta);
        j["motivo"] = d.motivo;
        descartadas.append(j);
    }
    QJsonObject raiz;
    raiz["version_contrato"] = ENERGIA_API_VERSION;
    raiz["directorio"] = QDir::toNativeSeparators(directorio_);
    raiz["modelos"] = modelos;
    raiz["descartadas"] = descartadas;
    return raiz;
}
