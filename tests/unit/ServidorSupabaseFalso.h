// ServidorSupabaseFalso.h - Servidor HTTP minimo que imita Auth y REST de Supabase (D-011).
//
// Solo para pruebas: escucha en 127.0.0.1 en un puerto libre y responde lo justo para
// ejercitar ClienteSupabase, ColaSubidas e HistorialRemoto sin red ni proyecto real.
// Cuenta valida: ana@ejemplo.com / secreta1 (DATOS INVENTADOS).
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class ServidorSupabaseFalso : public QObject {
public:
    struct Peticion {
        QByteArray metodo;
        QString ruta;       // sin consulta
        QUrlQuery consulta;
        QMap<QByteArray, QByteArray> cabeceras;  // nombres en minusculas
        QByteArray cuerpo;
    };
    struct Respuesta {
        int http = 200;
        QByteArray cuerpo;
        QMap<QByteArray, QByteArray> cabeceras;
    };

    // Estado que la prueba puede leer o cambiar
    QList<Peticion> peticiones;
    QString tokenAcceso = "acceso-1";
    QString tokenRenovacion = "renovacion-1";
    int emitidos = 1;
    int expiraEn = 3600;
    // Si se fija, responde en lugar de la logica por defecto (devolver http 0 = usar la normal)
    std::function<Respuesta(const Peticion&)> manejador;
    // Corridas "guardadas" por subir_corrida
    QJsonArray corridas;
    QHash<qint64, QJsonArray> series;
    // Cuenta de prueba: contrasena vigente, nombre del perfil y codigo de recuperacion
    QString contrasena = "secreta1";
    QString nombrePerfil = "Ana";
    QString codigoRecuperacion = "123456";
    // Imita una base sin la migracion 003 (unica por numero, sin columna t_creacion)
    bool baseSin003 = false;
    // Imita una base sin la migracion 008 (sin las columnas de la inferencia en corrida)
    bool baseSin008 = false;
    // Imita una base sin la migracion 009 (sin las columnas de las metricas del modelo en corrida)
    bool baseSin009 = false;
    // Inserts directos del detalle rechazados por la regla de la migracion 006
    int insertsDetalleRechazados = 0;

    static bool mismoInstante(const QJsonValue& a, const QJsonValue& b)
    {
        const QDateTime x = QDateTime::fromString(a.toString(), Qt::ISODateWithMs);
        const QDateTime y = QDateTime::fromString(b.toString(), Qt::ISODateWithMs);
        return x.isValid() && y.isValid() && x == y;
    }

    // puerto 0 = uno libre cualquiera
    explicit ServidorSupabaseFalso(QObject* parent = nullptr, quint16 puerto = 0) : QObject(parent)
    {
        servidor_.listen(QHostAddress::LocalHost, puerto);
        connect(&servidor_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = servidor_.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { leer(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QString url() const { return QString("http://127.0.0.1:%1").arg(servidor_.serverPort()); }

    int contar(const QString& ruta) const
    {
        int n = 0;
        for (const auto& p : peticiones) n += (p.ruta == ruta);
        return n;
    }

    // Invalida el token de acceso vigente (el cliente debe renovarlo al recibir 401)
    void revocarAcceso() { tokenAcceso = "revocado"; }

private:
    QTcpServer servidor_;
    QHash<QTcpSocket*, QByteArray> buffers_;

    static Respuesta json(int http, const QJsonValue& v)
    {
        Respuesta r;
        r.http = http;
        r.cuerpo = v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                 : v.isObject() ? QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)
                                : QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
        return r;
    }

    QJsonObject sesionNueva()
    {
        ++emitidos;
        tokenAcceso = QString("acceso-%1").arg(emitidos);
        tokenRenovacion = QString("renovacion-%1").arg(emitidos);
        return {{"access_token", tokenAcceso}, {"refresh_token", tokenRenovacion}, {"expires_in", expiraEn},
                {"token_type", "bearer"},
                {"user", QJsonObject{{"id", "u-ana"}, {"email", "ana@ejemplo.com"},
                                     {"user_metadata", QJsonObject{{"nombre", "Ana"}}}}}};
    }

    Respuesta responder(const Peticion& p)
    {
        if (manejador) {
            Respuesta r = manejador(p);
            if (r.http != 0) return r;
        }
        if (p.cabeceras.value("apikey").isEmpty()) return json(401, QJsonObject{{"message", "No API key found"}});
        const QJsonObject cuerpo = QJsonDocument::fromJson(p.cuerpo).object();

        if (p.ruta == "/auth/v1/token" && p.consulta.queryItemValue("grant_type") == "password") {
            if (cuerpo["email"].toString() == "ana@ejemplo.com" && cuerpo["password"].toString() == contrasena)
                return json(200, sesionNueva());
            return json(400, QJsonObject{{"code", 400}, {"error_code", "invalid_credentials"},
                                         {"msg", "Invalid login credentials"}});
        }
        if (p.ruta == "/auth/v1/token" && p.consulta.queryItemValue("grant_type") == "refresh_token") {
            if (cuerpo["refresh_token"].toString() == tokenRenovacion) return json(200, sesionNueva());
            return json(400, QJsonObject{{"code", 400}, {"error_code", "refresh_token_not_found"},
                                         {"msg", "Invalid Refresh Token: Refresh Token Not Found"}});
        }
        if (p.ruta == "/auth/v1/signup") {
            if (cuerpo["password"].toString().size() < 6)
                return json(422, QJsonObject{{"code", 422}, {"error_code", "weak_password"}, {"msg", "weak"}});
            // Con confirmacion de correo: devuelve el usuario sin sesion
            return json(200, QJsonObject{{"id", "u-nuevo"}, {"email", cuerpo["email"]},
                                         {"user_metadata", cuerpo["data"]}});
        }
        if (p.ruta == "/auth/v1/recover") return json(200, QJsonObject{});
        if (p.ruta == "/auth/v1/verify") {
            if (cuerpo["type"].toString() == "recovery" && cuerpo["email"].toString() == "ana@ejemplo.com"
                && cuerpo["token"].toString() == codigoRecuperacion)
                return json(200, sesionNueva());
            return json(403, QJsonObject{{"code", 403}, {"error_code", "otp_expired"}, {"msg", "Token has expired or is invalid"}});
        }
        if (p.ruta == "/auth/v1/user" && p.metodo == "PUT") {
            if (p.cabeceras.value("authorization") != "Bearer " + tokenAcceso.toUtf8())
                return json(401, QJsonObject{{"code", 401}, {"msg", "invalid JWT"}});
            if (cuerpo["password"].toString() == contrasena)
                return json(422, QJsonObject{{"code", 422}, {"error_code", "same_password"}, {"msg", "same"}});
            contrasena = cuerpo["password"].toString();
            return json(200, QJsonObject{{"id", "u-ana"}, {"email", "ana@ejemplo.com"}});
        }
        if (p.ruta == "/auth/v1/logout") {
            tokenRenovacion = "cerrada";
            Respuesta r;
            r.http = 204;
            return r;
        }

        // REST: exige el token vigente
        if (p.cabeceras.value("authorization") != "Bearer " + tokenAcceso.toUtf8())
            return json(401, QJsonObject{{"code", "PGRST301"}, {"message", "JWT expired"}});

        if (p.ruta == "/rest/v1/perfil" && p.metodo == "PATCH") {
            nombrePerfil = cuerpo["nombre"].toString();
            return json(200, QJsonArray{QJsonObject{{"nombre", nombrePerfil}}});
        }
        if (p.ruta == "/rest/v1/perfil") return json(200, QJsonArray{QJsonObject{{"nombre", nombrePerfil}, {"rol", "admin"}}});
        if (p.ruta == "/rest/v1/rpc/subir_corrida") {
            const QJsonObject paquete = cuerpo["p"].toObject();
            const QJsonObject c = paquete["corrida"].toObject();
            for (const QJsonValue& v : corridas) {
                const QJsonObject o = v.toObject();
                // Base vigente: unica por experimento + equipo + t_creacion. Con 'baseSin003', como la
                // version 2 del esquema: unica por numero
                const bool mismaClave = baseSin003 ? o["numero"] == c["numero"] : mismoInstante(o["t_creacion"], c["t_creacion"]);
                if (o["experimento"] == paquete["experimento"].toObject()["nombre"] && o["equipo"] == c["equipo"] && mismaClave)
                    return json(409, QJsonObject{{"code", "23505"},
                                                 {"message", "La corrida ya fue subida en este experimento"}});
            }
            const qint64 id = corridas.size() + 1;
            QJsonObject guardada = c;
            guardada["id"] = id;
            guardada["experimento"] = paquete["experimento"].toObject()["nombre"];
            guardada["usuario_id"] = "u-ana";
            guardada["perfil"] = QJsonObject{{"nombre", "Ana"}};
            if (paquete["hardware"].isObject()) guardada["hardware_equipo"] = paquete["hardware"];
            guardada["visibilidad"] = c["visibilidad"].toString("privada");
            corridas.append(guardada);
            series[id] = paquete["serie"].toArray();
            return json(200, id);
        }
        if (p.ruta == "/rest/v1/corrida" && p.metodo == "GET") {
            if (baseSin008 && p.consulta.queryItemValue("select").contains("inferencia_activada"))
                return json(400, QJsonObject{{"code", "42703"}, {"message", "column corrida.inferencia_activada does not exist"}});
            for (const char* col : {"metricas_modelo", "f1_macro_val"})
                if (baseSin009 && p.consulta.queryItemValue("select").contains(col))
                    return json(400, QJsonObject{{"code", "42703"},
                                                 {"message", QString("column corrida.%1 does not exist").arg(col)}});
            const QString filtroT = p.consulta.queryItemValue("t_creacion");
            if (baseSin003 && (!filtroT.isEmpty() || p.consulta.queryItemValue("select").contains("t_creacion")))
                return json(400, QJsonObject{{"code", "42703"}, {"message", "column corrida.t_creacion does not exist"}});
            const QString filtroId = p.consulta.queryItemValue("id", QUrl::FullyDecoded);
            if (!filtroId.isEmpty()) {
                // id=eq.N o id=in.(N,M,...)
                QList<qint64> ids;
                if (filtroId.startsWith("in.("))
                    for (const QString& x : filtroId.mid(4).chopped(1).split(',')) ids.append(x.toLongLong());
                else
                    ids.append(filtroId.mid(3).toLongLong());
                QJsonArray una;
                for (const QJsonValue& v : corridas)
                    if (ids.contains(v.toObject()["id"].toInteger())) una.append(v);
                return json(200, una);
            }
            if (filtroT.isEmpty()) return json(200, corridas);
            QJsonArray filtradas;
            for (const QJsonValue& v : corridas)
                if (v.toObject()["equipo"].toString() == p.consulta.queryItemValue("equipo").mid(3)
                    && mismoInstante(v.toObject()["t_creacion"], QJsonValue(filtroT.mid(3))))
                    filtradas.append(QJsonObject{{"id", v.toObject()["id"]}});
            return json(200, filtradas);
        }
        if (p.ruta == "/rest/v1/corrida" && p.metodo == "PATCH") {
            // Como RLS: solo se actualizan las corridas propias; las ajenas dan 0 filas
            const qint64 id = p.consulta.queryItemValue("id").mid(3).toLongLong();
            QJsonArray actualizadas;
            for (int i = 0; i < corridas.size(); ++i) {
                QJsonObject o = corridas[i].toObject();
                if (o["id"].toInteger() != id || o["usuario_id"].toString() != "u-ana") continue;
                QJsonObject fila{{"id", id}};
                for (auto it = cuerpo.begin(); it != cuerpo.end(); ++it) {  // visibilidad o nota
                    o[it.key()] = it.value();
                    fila[it.key()] = it.value();
                }
                corridas[i] = o;
                actualizadas.append(fila);
            }
            return json(200, actualizadas);
        }
        if (p.ruta == "/rest/v1/corrida" && p.metodo == "DELETE") {
            // Como RLS: solo se borran las propias
            const qint64 id = p.consulta.queryItemValue("id").mid(3).toLongLong();
            QJsonArray borradas;
            for (int i = static_cast<int>(corridas.size()) - 1; i >= 0; --i) {
                const QJsonObject o = corridas[i].toObject();
                if (o["id"].toInteger() != id || o["usuario_id"].toString() != "u-ana") continue;
                corridas.removeAt(i);
                series.remove(id);
                borradas.append(QJsonObject{{"id", id}});
            }
            return json(200, borradas);
        }
        // Como la migracion 006: el detalle de una corrida (paso, metrica_paso, serie_unida,
        // hiperparametro) solo se inserta dentro de la transaccion de subir_corrida. Cualquier
        // insert por REST es otra transaccion: la regla RLS lo rechaza (42501), sea o no el dueno.
        static const QStringList DETALLE = {"/rest/v1/paso", "/rest/v1/metrica_paso", "/rest/v1/serie_unida",
                                            "/rest/v1/hiperparametro"};
        if (p.metodo == "POST" && DETALLE.contains(p.ruta)) {
            ++insertsDetalleRechazados;
            return json(403, QJsonObject{{"code", "42501"}, {"details", QJsonValue::Null}, {"hint", QJsonValue::Null},
                                         {"message", QString("new row violates row-level security policy for table \"%1\"")
                                                         .arg(p.ruta.mid(9))}});
        }
        // Tampoco se fija subida_en a mano: insert en corrida solo por columnas, sin subida_en
        if (p.metodo == "POST" && p.ruta == "/rest/v1/corrida" && cuerpo.contains("subida_en"))
            return json(403, QJsonObject{{"code", "42501"}, {"message", "permission denied for table corrida"}});
        if (p.ruta == "/rest/v1/serie_unida") {
            const qint64 id = p.consulta.queryItemValue("corrida_id").mid(3).toLongLong();
            const int limite = p.consulta.queryItemValue("limit").toInt();
            const int desde = p.consulta.queryItemValue("offset").toInt();
            const QJsonArray todo = series.value(id);
            QJsonArray pagina;
            for (int i = desde; i < todo.size() && pagina.size() < limite; ++i) pagina.append(todo[i]);
            return json(200, pagina);
        }
        return json(404, QJsonObject{{"message", "ruta desconocida"}});
    }

    void leer(QTcpSocket* s)
    {
        QByteArray& b = buffers_[s];
        b += s->readAll();
        const int fin = b.indexOf("\r\n\r\n");
        if (fin < 0) return;
        const QList<QByteArray> lineas = b.left(fin).split('\n');
        Peticion p;
        const QList<QByteArray> primera = lineas.first().trimmed().split(' ');
        p.metodo = primera.value(0);
        const QUrl url(QString::fromUtf8(primera.value(1)));
        p.ruta = url.path();
        p.consulta = QUrlQuery(url);
        for (int i = 1; i < lineas.size(); ++i) {
            const int dp = lineas[i].indexOf(':');
            if (dp > 0) p.cabeceras[lineas[i].left(dp).trimmed().toLower()] = lineas[i].mid(dp + 1).trimmed();
        }
        const int largo = p.cabeceras.value("content-length").toInt();
        if (b.size() < fin + 4 + largo) return;  // falta el cuerpo
        p.cuerpo = b.mid(fin + 4, largo);
        b.remove(0, fin + 4 + largo);
        peticiones.append(p);

        const Respuesta r = responder(p);
        QByteArray salida = "HTTP/1.1 " + QByteArray::number(r.http) + " X\r\n";
        salida += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(r.cuerpo.size()) + "\r\n";
        for (auto it = r.cabeceras.begin(); it != r.cabeceras.end(); ++it) salida += it.key() + ": " + it.value() + "\r\n";
        salida += "\r\n" + r.cuerpo;
        s->write(salida);
        if (!b.isEmpty()) leer(s);
    }
};
