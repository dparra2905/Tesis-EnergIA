// ColaSubidas.cpp - ver ColaSubidas.h.
#include "ColaSubidas.h"

#include "Auditoria.h"
#include "ClienteSupabase.h"
#include "PaqueteCorrida.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace {

QString ahora() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

// Migraciones que necesita una corrida segun su paquete. Indice: 0 = la 008 (inferencia, D-014),
// 1 = la 009 (metricas del modelo, D-015). La columna es la que se consulta para saber si existe.
struct Migracion {
    int numero;
    const char* columna;
    const char* motivo;
};
const Migracion MIGRACIONES[] = {
    {8, "inferencia_activada", ColaSubidas::MOTIVO_SIN_008},
    {9, "metricas_modelo", ColaSubidas::MOTIVO_SIN_009},
};
constexpr int N_MIGRACIONES = static_cast<int>(sizeof(MIGRACIONES) / sizeof(MIGRACIONES[0]));

bool necesita(const QJsonObject& paquete, int i)
{
    const QJsonObject c = paquete["corrida"].toObject();
    return i == 0 ? c["inferencia_activada"].toBool(false) : c["metricas_modelo"].isObject();
}

int indiceDeMotivo(const QString& motivo)
{
    for (int i = 0; i < N_MIGRACIONES; ++i)
        if (motivo == QString::fromUtf8(MIGRACIONES[i].motivo)) return i;
    return -1;
}

} // namespace

ColaSubidas::ColaSubidas(const Opciones& opciones, ClienteSupabase* sesion, QObject* parent)
    : QObject(parent), op_(opciones), sesion_(sesion)
{
    cargar();
    cargarPreferencias();
    // Una corrida que quedo "subiendo" al cerrar la aplicacion vuelve a pendiente
    for (QJsonObject& e : cola_)
        if (e["estado"].toString() == "subiendo") e["estado"] = "pendiente";
    reintento_.setSingleShot(true);
    connect(&reintento_, &QTimer::timeout, this, &ColaSubidas::procesar);
    connect(sesion_, &ClienteSupabase::sesionActivada, this, [this] {
        fallosSeguidos_ = 0;
        // Otra sesion (quiza otra base): se vuelven a comprobar las migraciones 008 y 009. Si hay
        // corridas rechazadas por su falta, se comprueban ya para devolverlas a pendiente si se aplicaron
        QList<int> porComprobar;
        for (int i = 0; i < N_MIGRACIONES; ++i) {
            migracion_[i] = -1;
            const bool hayRechazadas = std::any_of(cola_.begin(), cola_.end(), [i](const QJsonObject& e) {
                return e["estado"].toString() == "rechazada" && indiceDeMotivo(e["ultimo_error"].toString()) == i;
            });
            if (hayRechazadas) porComprobar.append(i);
        }
        if (porComprobar.isEmpty()) procesar();
        for (int i : porComprobar) comprobarMigracion(i, [this] { procesar(); }, [](const QString&) {});
    });
    // El propietario de cada corrida ("propia", "otra"...) depende de quien tenga la sesion
    connect(sesion_, &ClienteSupabase::sesionCambiada, this, &ColaSubidas::cambiado);
    // Tras construir, para que quien la crea alcance a conectar sus senales
    QTimer::singleShot(0, this, &ColaSubidas::procesar);
}

ColaSubidas::~ColaSubidas()
{
    if (hilo_) {
        hilo_->wait();  // el hilo solo lee archivos: termina por si solo
        delete hilo_;
    }
}

QString ColaSubidas::clave(const QString& dirCorrida)
{
    return QDir::cleanPath(QFileInfo(dirCorrida).absoluteFilePath()).toLower();
}

QString ColaSubidas::propietario(const QJsonObject& e) const
{
    const QString dueno = e["usuario_id"].toString();
    if (dueno.isEmpty()) return "sin_cuenta";
    return dueno == sesion_->usuarioId() ? "propia" : "otra";
}

qint64 ColaSubidas::tCreacionMs(const QString& dirCorrida) const
{
    const QString k = clave(dirCorrida);
    const auto it = tCreacion_.constFind(k);
    if (it != tCreacion_.constEnd()) return it.value();
    qint64 ms = 0;
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QDateTime t = QDateTime::fromString(
            QJsonDocument::fromJson(f.readAll()).object()["tiempos"].toObject()["t_creacion"].toString(), Qt::ISODateWithMs);
        if (t.isValid()) ms = t.toMSecsSinceEpoch();
    }
    tCreacion_[k] = ms;
    return ms;
}

QVariantList ColaSubidas::elementos() const
{
    QVariantList l;
    for (const QJsonObject& e : cola_) {
        QVariantMap m = e.toVariantMap();
        m["propietario"] = propietario(e);
        l.append(m);
    }
    return l;
}

int ColaSubidas::pendientes() const
{
    return static_cast<int>(std::count_if(cola_.begin(), cola_.end(), [](const QJsonObject& e) {
        const QString s = e["estado"].toString();
        return s == "pendiente" || s == "subiendo";
    }));
}

int ColaSubidas::sinCuenta() const
{
    return static_cast<int>(std::count_if(cola_.begin(), cola_.end(), [](const QJsonObject& e) {
        return e["usuario_id"].toString().isEmpty() && e["estado"].toString() != "subida";
    }));
}

int ColaSubidas::esperandoDecision() const
{
    return static_cast<int>(std::count_if(cola_.begin(), cola_.end(),
                                          [](const QJsonObject& e) { return e["estado"].toString() == "esperando_decision"; }));
}

void ColaSubidas::cargarPreferencias()
{
    QFile f(op_.rutaPreferencias);
    if (op_.rutaPreferencias.isEmpty() || !f.open(QIODevice::ReadOnly)) return;
    const QString m = QJsonDocument::fromJson(f.readAll()).object()["modo_subida"].toString();
    if (m == MODO_SUBIR_TODO || m == MODO_PREGUNTAR_SI_HAY_PROBLEMAS || m == MODO_PREGUNTAR_SIEMPRE) modo_ = m;
}

void ColaSubidas::setModoSubida(const QString& modo)
{
    if (modo != MODO_SUBIR_TODO && modo != MODO_PREGUNTAR_SI_HAY_PROBLEMAS && modo != MODO_PREGUNTAR_SIEMPRE) return;
    if (modo == modo_) return;
    modo_ = modo;
    emit modoSubidaCambiado();
    if (op_.rutaPreferencias.isEmpty()) return;
    // Se conservan las demas preferencias del archivo (p. ej. linea_base_s)
    QDir().mkpath(QFileInfo(op_.rutaPreferencias).absolutePath());
    QJsonObject pref;
    QFile previo(op_.rutaPreferencias);
    if (previo.open(QIODevice::ReadOnly)) pref = QJsonDocument::fromJson(previo.readAll()).object();
    previo.close();
    pref["modo_subida"] = modo_;
    QSaveFile f(op_.rutaPreferencias);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(pref).toJson(QJsonDocument::Indented));
        if (f.commit()) return;
    }
    emit notificacion("warning", "No se pudo guardar el modo de subida", QDir::toNativeSeparators(op_.rutaPreferencias));
}

QString ColaSubidas::veredictoDe(const QString& dirCorrida)
{
    QFile f(QDir(dirCorrida).filePath(auditoria::ARCHIVO));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object()["veredicto"].toString();
}

int ColaSubidas::rechazadas() const
{
    return static_cast<int>(std::count_if(cola_.begin(), cola_.end(),
                                          [](const QJsonObject& e) { return e["estado"].toString() == "rechazada"; }));
}

void ColaSubidas::encolar(const QString& dirCorrida)
{
    const QString k = clave(dirCorrida);
    for (const QJsonObject& e : cola_)
        if (e["clave"].toString() == k) {
            procesar();
            return;
        }
    // Modo de subida (D-012): sin auditoria cuenta como "hay problemas"
    const QString veredicto = veredictoDe(dirCorrida);
    const bool preguntar = modo_ == MODO_PREGUNTAR_SIEMPRE
                           || (modo_ == MODO_PREGUNTAR_SI_HAY_PROBLEMAS && veredicto != auditoria::VALIDA);
    agregar(dirCorrida, preguntar ? "esperando_decision" : "pendiente");
    if (preguntar) {
        const QJsonObject& e = cola_.last();
        emit decisionPendiente(e["dir"].toString(), QString("%1 / corrida %2").arg(e["experimento"].toString()).arg(e["corrida"].toInt()),
                               veredicto);
    }
}

void ColaSubidas::setCorridaActiva(bool activa)
{
    if (activa == corridaActiva_) return;
    corridaActiva_ = activa;
    if (activa) reintento_.stop();
    else procesar();
}

void ColaSubidas::decidir(const QString& dirCorrida, bool subir)
{
    const QString k = clave(dirCorrida);
    for (int i = 0; i < cola_.size(); ++i) {
        if (cola_[i]["clave"].toString() != k || cola_[i]["estado"].toString() != "esperando_decision") continue;
        if (subir) {
            cola_[i]["estado"] = "pendiente";
            cola_[i]["actualizado"] = ahora();
        } else {
            cola_.removeAt(i);  // "Solo local": se puede subir despues
        }
        guardar();
        emit cambiado();
        if (subir) procesar();
        return;
    }
}

void ColaSubidas::agregar(const QString& dirCorrida, const QString& estado)
{
    const QString k = clave(dirCorrida);
    QJsonObject e;
    e["clave"] = k;
    e["dir"] = QDir::toNativeSeparators(QFileInfo(dirCorrida).absoluteFilePath());
    QFile f(QDir(dirCorrida).filePath("metadata.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
        e["experimento"] = m["experimento"].toString();
        e["corrida"] = m["corrida"].toInt();
    }
    e["estado"] = estado;
    e["veredicto"] = veredictoDe(dirCorrida);
    e["intentos"] = 0;
    e["ultimo_error"] = "";
    e["id_remoto"] = QJsonValue::Null;
    // Dueno: el usuario con sesion al terminar la corrida ("" = sin cuenta: queda esperando
    // a que alguien la reclame; no se asigna sola a la proxima cuenta)
    e["usuario_id"] = sesion_->usuarioId();
    e["actualizado"] = ahora();
    cola_.append(e);
    guardar();
    emit cambiado();
    procesar();
}

void ColaSubidas::reintentar(const QString& dirCorrida)
{
    const QString k = clave(dirCorrida);
    bool hay = false;
    for (QJsonObject& e : cola_) {
        if (e["clave"].toString() != k) continue;
        hay = true;
        // Subir a mano tambien decide por una corrida que esperaba decision
        if (e["estado"].toString() == "rechazada" || e["estado"].toString() == "esperando_decision") e["estado"] = "pendiente";
        // Pedirlo sobre una corrida sin cuenta es reclamarla para la cuenta con sesion
        if (e["usuario_id"].toString().isEmpty() && sesion_->conectado()) e["usuario_id"] = sesion_->usuarioId();
    }
    if (!hay) {
        agregar(dirCorrida, "pendiente");  // a mano: no pasa por el modo de subida
        return;
    }
    fallosSeguidos_ = 0;
    guardar();
    emit cambiado();
    procesar();
}

void ColaSubidas::reintentarTodo()
{
    for (QJsonObject& e : cola_)
        if (e["estado"].toString() == "rechazada") e["estado"] = "pendiente";
    fallosSeguidos_ = 0;
    guardar();
    emit cambiado();
    procesar();
}

void ColaSubidas::reclamarSinCuenta()
{
    if (!sesion_->conectado()) return;
    for (QJsonObject& e : cola_) {
        if (!e["usuario_id"].toString().isEmpty() || e["estado"].toString() == "subida") continue;
        e["usuario_id"] = sesion_->usuarioId();  // las que esperan decision siguen esperando
        if (e["estado"].toString() == "rechazada") e["estado"] = "pendiente";
    }
    fallosSeguidos_ = 0;
    guardar();
    emit cambiado();
    procesar();
}

void ColaSubidas::sincronizarRemotas(const QVariantList& corridasRemotas)
{
    remotas_.clear();
    for (const QVariant& v : corridasRemotas) {
        const QVariantMap m = v.toMap();
        const QDateTime t = QDateTime::fromString(m.value("t_creacion").toString(), Qt::ISODateWithMs);
        if (!t.isValid()) continue;
        remotas_[m.value("equipo").toString() + '|' + QString::number(t.toMSecsSinceEpoch())] = m.value("id").toLongLong();
    }
    emit cambiado();
}

void ColaSubidas::olvidarRemota(qint64 idRemoto)
{
    for (int i = static_cast<int>(cola_.size()) - 1; i >= 0; --i)
        if (cola_[i]["estado"].toString() == "subida" && cola_[i]["id_remoto"].toInteger() == idRemoto) cola_.removeAt(i);
    for (auto it = remotas_.begin(); it != remotas_.end();)
        it = it.value() == idRemoto ? remotas_.erase(it) : std::next(it);
    guardar();
    emit cambiado();
}

void ColaSubidas::depurarSubidas(const QVariantList& corridasRemotas, bool listaCompleta)
{
    const QString usuario = sesion_->usuarioId();
    if (!sesion_->conectado() || usuario.isEmpty()) return;
    QSet<qint64> enLaBase;
    for (const QVariant& v : corridasRemotas) enLaBase.insert(v.toMap().value("id").toLongLong());
    // Solo las de esta cuenta: las corridas propias siempre son visibles en la lista (RLS)
    QList<qint64> ausentes;
    for (const QJsonObject& e : cola_) {
        const qint64 id = e["id_remoto"].toInteger();
        if (e["estado"].toString() == "subida" && e["usuario_id"].toString() == usuario && id > 0 && !enLaBase.contains(id))
            ausentes.append(id);
    }
    if (ausentes.isEmpty()) return;
    if (listaCompleta) {
        olvidarAusentes(ausentes);
        return;
    }
    // La lista trajo el maximo de corridas: las que faltan pueden ser mas viejas. Se piden por id.
    QStringList ids;
    for (qint64 id : ausentes) ids << QString::number(id);
    QUrlQuery q;
    q.addQueryItem("select", "id");
    q.addQueryItem("id", "in.(" + ids.join(',') + ")");
    sesion_->rest("GET", "corrida", q, {}, [this, ausentes](const ClienteSupabase::Resultado& r) {
        if (!r.ok()) return;  // sin respuesta completa no se concluye nada
        QSet<qint64> existen;
        for (const QJsonValue& v : r.cuerpo.array()) existen.insert(v.toObject()["id"].toInteger());
        QList<qint64> borradas;
        for (qint64 id : ausentes)
            if (!existen.contains(id)) borradas.append(id);
        olvidarAusentes(borradas);
    });
}

void ColaSubidas::olvidarAusentes(const QList<qint64>& ausentes)
{
    if (ausentes.isEmpty()) return;
    const QString usuario = sesion_->usuarioId();
    for (int i = static_cast<int>(cola_.size()) - 1; i >= 0; --i)
        if (cola_[i]["estado"].toString() == "subida" && cola_[i]["usuario_id"].toString() == usuario
            && ausentes.contains(cola_[i]["id_remoto"].toInteger()))
            cola_.removeAt(i);
    guardar();
    emit cambiado();
}

QVariantMap ColaSubidas::estadoDe(const QString& dirCorrida) const
{
    const QString k = clave(dirCorrida);
    QVariantMap r{{"estado", "no_encolada"}, {"origen", "cola"}};
    for (const QJsonObject& e : cola_)
        if (e["clave"].toString() == k) {
            r = e.toVariantMap();
            r["origen"] = "cola";
            r["propietario"] = propietario(e);
        }
    // La lista de la base manda: si la corrida esta alli, esta subida
    if (r.value("estado").toString() != "subida" && !remotas_.isEmpty()) {
        const qint64 ms = tCreacionMs(dirCorrida);
        const auto it = remotas_.constFind(op_.equipo + '|' + QString::number(ms));
        if (ms > 0 && it != remotas_.constEnd()) {
            r["estado"] = "subida";
            r["id_remoto"] = it.value();
            r["ultimo_error"] = "";
            r["origen"] = "base";
        }
    }
    return r;
}

void ColaSubidas::procesar()
{
    if (corridaActiva_ || !actual_.isEmpty() || !sesion_->conectado()) return;
    const QString usuario = sesion_->usuarioId();
    // Solo las de la cuenta con sesion: las sin cuenta esperan a que alguien las reclame
    auto it = std::find_if(cola_.begin(), cola_.end(), [&usuario](const QJsonObject& e) {
        return e["estado"].toString() == "pendiente" && !usuario.isEmpty() && e["usuario_id"].toString() == usuario;
    });
    if (it == cola_.end()) return;
    reintento_.stop();
    (*it)["estado"] = "subiendo";
    (*it)["intentos"] = (*it)["intentos"].toInt() + 1;
    (*it)["actualizado"] = ahora();
    actual_ = (*it)["clave"].toString();
    guardar();
    emit cambiado();

    // Paquete en otro hilo: lee los CSV de la corrida y puede calcular el sha256 del dataset
    const QString dir = (*it)["dir"].toString();
    const QString equipo = op_.equipo;
    const QString rutaDs = PaqueteCorrida::rutaDataset(dir);
    const QJsonObject huellaGuardada = huellas_.value(rutaDs);
    const QString k = actual_;
    if (hilo_) {
        hilo_->wait();
        delete hilo_;
    }
    hilo_ = QThread::create([this, k, dir, equipo, rutaDs, huellaGuardada] {
        Preparado p;
        // Auditoria que falta (corrida anterior a la auditoria): se calcula antes de subir, como
        // --postprocesar, para que llegue a la base con veredicto. Si falla, se sube sin el.
        if (!QFile::exists(QDir(dir).filePath(auditoria::ARCHIVO))) {
            QJsonObject a;
            if (!auditoria::auditarYGuardar(dir, &a, &p.errorAuditoria) && p.errorAuditoria.isEmpty())
                p.errorAuditoria = "no se pudo calcular";
        }
        QString sha;
        // Corridas nuevas: la huella se calculo al preparar los datos (metadata.datos.sha256) y
        // no hace falta el CSV. Se guarda tambien en la cache de huellas.
        const QJsonObject datosMeta = PaqueteCorrida::datosMetadata(dir);
        if (!rutaDs.isEmpty() && datosMeta["sha256"].isString()) {
            sha = datosMeta["sha256"].toString();
            if (datosMeta["fecha_modificacion"].isString())
                p.huella = QJsonObject{{"tam", datosMeta["tam_bytes"]}, {"fecha", datosMeta["fecha_modificacion"]}, {"sha256", sha}};
        } else if (!rutaDs.isEmpty()) {
            // Corridas anteriores a la huella en metadata: se calcula al subir (cache o archivo)
            const QFileInfo fi(rutaDs);
            if (fi.exists()) {
                const QString fecha = fi.lastModified().toUTC().toString(Qt::ISODateWithMs);
                if (huellaGuardada["tam"].toInteger() == fi.size() && huellaGuardada["fecha"].toString() == fecha) {
                    sha = huellaGuardada["sha256"].toString();
                } else {
                    QString e;
                    sha = PaqueteCorrida::huellaArchivo(rutaDs, &e);
                    if (!sha.isEmpty())
                        p.huella = QJsonObject{{"tam", fi.size()}, {"fecha", fecha}, {"sha256", sha}};
                }
            }
            // Si el CSV ya no esta, la corrida se sube sin dataset (sin huella no se puede
            // asegurar que sea el mismo archivo)
        }
        p.rutaDataset = rutaDs;
        p.paquete = PaqueteCorrida::construir(dir, equipo, sha, &p.error);
        QMetaObject::invokeMethod(this, [this, k, p] { alPreparar(k, p); }, Qt::QueuedConnection);
    });
    hilo_->start();
}

void ColaSubidas::alPreparar(const QString& k, const Preparado& p)
{
    if (!p.huella.isEmpty()) {
        huellas_[p.rutaDataset] = p.huella;
        guardarHuellas();
    }
    if (!p.errorAuditoria.isEmpty()) {
        for (const QJsonObject& e : cola_)
            if (e["clave"].toString() == k)
                emit notificacion("warning", "Se sube sin auditoría",
                                  QString("%1 / corrida %2: no se pudo calcular la auditoría (%3).")
                                      .arg(e["experimento"].toString()).arg(e["corrida"].toInt()).arg(p.errorAuditoria));
    }
    if (!p.error.isEmpty()) {
        terminar(k, "rechazada", p.error);
        return;
    }
    if (corridaActiva_) {
        // Empezo una corrida mientras se armaba el paquete: no se envia; se sube al terminar
        for (QJsonObject& e : cola_)
            if (e["clave"].toString() == k) e["intentos"] = std::max(0, e["intentos"].toInt() - 1);
        terminar(k, "pendiente", {});
        return;
    }
    for (QJsonObject& e : cola_)
        if (e["clave"].toString() == k) e["veredicto"] = p.paquete["corrida"].toObject()["veredicto"];
    // Para comprobar un 409 en la base
    for (QJsonObject& e : cola_)
        if (e["clave"].toString() == k) {
            e["equipo"] = p.paquete["corrida"].toObject()["equipo"];
            e["t_creacion"] = p.paquete["corrida"].toObject()["t_creacion"];
        }
    enviar(k, p.paquete);
}

void ColaSubidas::enviar(const QString& k, const QJsonObject& paquete)
{
    // Una corrida con inferencia necesita la migracion 008 (D-014) y una con metricas del modelo, la
    // 009 (D-015); las demas se suben como antes
    for (int i = 0; i < N_MIGRACIONES; ++i) {
        if (!necesita(paquete, i)) continue;
        const Migracion& mg = MIGRACIONES[i];
        if (migracion_[i] == 0) {
            terminar(k, "rechazada", mg.motivo);
            emit notificacion("warning", QString("Falta aplicar la migración 00%1").arg(mg.numero),
                              QString("La corrida tiene %1 y la base de datos no tiene la migración 00%2: se subirá sola "
                                      "cuando se aplique.")
                                  .arg(i == 0 ? "inferencia" : "métricas del modelo")
                                  .arg(mg.numero));
            return;
        }
        if (migracion_[i] < 0) {
            comprobarMigracion(
                i,
                [this, k, paquete] {
                    if (corridaActiva_) {  // empezo una corrida mientras se preguntaba: se sube al terminar
                        terminar(k, "pendiente", {});
                        return;
                    }
                    enviar(k, paquete);
                },
                [this, k](const QString& error) {
                    // Sin respuesta: no se concluye nada; la corrida sigue pendiente, como sin red
                    terminar(k, "pendiente", error);
                    ++fallosSeguidos_;
                    programarReintento();
                });
            return;
        }
    }
    sesion_->subirCorrida(paquete, [this, k, paquete](const ClienteSupabase::Resultado& r) {
        // Si falla la subida, la proxima vez se vuelven a comprobar las migraciones que necesita
        if (!(r.http >= 200 && r.http < 300))
            for (int i = 0; i < N_MIGRACIONES; ++i)
                if (necesita(paquete, i)) migracion_[i] = -1;
        alResponder(k, r.http, r.red, r.crudo, r.error);
    });
}

void ColaSubidas::comprobarMigracion(int i, std::function<void()> luego, std::function<void(const QString&)> sinRespuesta)
{
    const QString columna = QLatin1String(MIGRACIONES[i].columna);
    QUrlQuery q;
    q.addQueryItem("select", columna);
    q.addQueryItem("limit", "1");
    sesion_->rest("GET", "corrida", q, {}, [this, i, columna, luego, sinRespuesta](const ClienteSupabase::Resultado& r) {
        if (r.ok()) {
            setMigracion(i, true);
            luego();
        } else if (!r.red && r.http == 400 && r.error.contains(columna)) {
            setMigracion(i, false);
            luego();
        } else {
            sinRespuesta(r.error);
        }
    });
}

void ColaSubidas::setMigracion008(bool aplicada) { setMigracion(0, aplicada); }
void ColaSubidas::setMigracion009(bool aplicada) { setMigracion(1, aplicada); }

void ColaSubidas::setMigracion(int i, bool aplicada)
{
    const int antes = migracion_[i];
    migracion_[i] = aplicada ? 1 : 0;
    if (!aplicada || antes == 1) return;
    // Las corridas rechazadas por la falta de esta migracion vuelven solas a pendiente
    bool cambio = false;
    for (QJsonObject& e : cola_)
        if (e["estado"].toString() == "rechazada" && indiceDeMotivo(e["ultimo_error"].toString()) == i) {
            e["estado"] = "pendiente";
            e["ultimo_error"] = "";
            e["actualizado"] = ahora();
            cambio = true;
        }
    if (!cambio) return;
    guardar();
    emit cambiado();
    emit notificacion("info", QString("Migración 00%1 aplicada").arg(MIGRACIONES[i].numero),
                      QString("Las corridas con %1 que esperaban la migración se subirán ahora.")
                          .arg(i == 0 ? "inferencia" : "métricas del modelo"));
    QTimer::singleShot(0, this, &ColaSubidas::procesar);
}

void ColaSubidas::alResponder(const QString& k, int http, bool red, const QByteArray& crudo, const QString& error)
{
    auto it = std::find_if(cola_.begin(), cola_.end(), [&k](const QJsonObject& e) { return e["clave"].toString() == k; });
    if (it == cola_.end()) {
        actual_.clear();
        return;
    }
    const QString nombre = QString("%1 / corrida %2").arg((*it)["experimento"].toString()).arg((*it)["corrida"].toInt());
    if (http >= 200 && http < 300) {
        const qint64 id = crudo.trimmed().toLongLong();
        const QString dir = (*it)["dir"].toString();
        (*it)["id_remoto"] = id;
        (*it)["usuario_id"] = sesion_->usuarioId();
        fallosSeguidos_ = 0;
        terminar(k, "subida", {});
        emit corridaSubida(dir, id);
        emit notificacion("success", "Corrida guardada en la base de datos", nombre);
        return;
    }
    if (http == 409 || error.contains("ya fue subida")) {
        // La base dice que ya existe: se comprueba que sea ESTA corrida (equipo y t_creacion)
        verificarDuplicado(k);
        return;
    }
    if (red || http == 0 || http == 401 || http == 408 || http == 429 || http >= 500) {
        // Transitorio: sin red, sesion vencida, limite de peticiones o servidor caido
        terminar(k, "pendiente", error);
        ++fallosSeguidos_;
        programarReintento();
        return;
    }
    terminar(k, "rechazada", error);
    emit notificacion("error", "La base de datos rechazó la corrida", nombre + ": " + error);
}

void ColaSubidas::verificarDuplicado(const QString& k)
{
    QJsonObject e;
    for (const QJsonObject& x : cola_)
        if (x["clave"].toString() == k) e = x;
    QUrlQuery q;
    q.addQueryItem("select", "id");
    q.addQueryItem("equipo", "eq." + e["equipo"].toString());
    q.addQueryItem("t_creacion", "eq." + e["t_creacion"].toString());
    sesion_->rest("GET", "corrida", q, {}, [this, k](const ClienteSupabase::Resultado& r) {
        auto it = std::find_if(cola_.begin(), cola_.end(), [&k](const QJsonObject& x) { return x["clave"].toString() == k; });
        if (it == cola_.end()) {
            actual_.clear();
            return;
        }
        if (r.red || r.http >= 500) {
            terminar(k, "pendiente", r.error);
            ++fallosSeguidos_;
            programarReintento();
            return;
        }
        if (!r.ok()) {
            // Sin la columna t_creacion (base sin la migracion 003) no se puede comprobar
            terminar(k, "rechazada", "La base dice que ya existe una corrida con este número, pero no se pudo "
                                     "comprobar que sea esta. ¿Falta aplicar la migración 003? (" + r.error + ")");
            return;
        }
        const QJsonArray filas = r.cuerpo.array();
        if (filas.isEmpty()) {
            terminar(k, "rechazada", "La base tiene otra corrida que choca con esta (mismo experimento, equipo y "
                                     "número o fecha de creación) y no es la misma corrida.");
            return;
        }
        (*it)["id_remoto"] = filas.first().toObject()["id"].toInteger();
        terminar(k, "subida", "Ya estaba subida en la base de datos.");
    });
}

void ColaSubidas::terminar(const QString& k, const QString& estado, const QString& error)
{
    for (QJsonObject& e : cola_) {
        if (e["clave"].toString() != k) continue;
        e["estado"] = estado;
        e["ultimo_error"] = error;
        e["actualizado"] = ahora();
    }
    actual_.clear();
    guardar();
    emit cambiado();
    if (estado != "pendiente") procesar();  // la siguiente, si hay
}

void ColaSubidas::programarReintento()
{
    const qint64 espera = std::min<qint64>(static_cast<qint64>(op_.esperaInicialMs) << std::min(fallosSeguidos_ - 1, 10),
                                           op_.esperaMaximaMs);
    reintento_.start(static_cast<int>(espera));
}

void ColaSubidas::cargar()
{
    QFile f(op_.rutaCola);
    if (f.open(QIODevice::ReadOnly)) {
        for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object()["corridas"].toArray())
            cola_.append(v.toObject());
    }
    QFile h(op_.rutaHuellas);
    if (h.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(h.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) huellas_[it.key()] = it.value().toObject();
    }
}

void ColaSubidas::guardar() const
{
    if (op_.rutaCola.isEmpty()) return;
    QDir().mkpath(QFileInfo(op_.rutaCola).absolutePath());
    QSaveFile f(op_.rutaCola);
    if (!f.open(QIODevice::WriteOnly)) return;
    QJsonArray a;
    for (const QJsonObject& e : cola_) a.append(e);
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"corridas", a}}).toJson());
    f.commit();
}

void ColaSubidas::guardarHuellas() const
{
    if (op_.rutaHuellas.isEmpty()) return;
    QDir().mkpath(QFileInfo(op_.rutaHuellas).absolutePath());
    QSaveFile f(op_.rutaHuellas);
    if (!f.open(QIODevice::WriteOnly)) return;
    QJsonObject o;
    for (auto it = huellas_.begin(); it != huellas_.end(); ++it) o[it.key()] = it.value();
    f.write(QJsonDocument(o).toJson());
    f.commit();
}
