#pragma once
/**
 * @file gateway_types.hpp
 * @brief Qt value types for gateway replies, events and identity (owning copies of protocol data).
 *
 * proto::Message only references the receive buffer; these types copy what the desktop keeps so
 * they can travel through queued signals and outlive the buffer.
 */

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include "diag/error.hpp"
#include "proto/message.hpp"

/// Who answered HELLO. The source decides every SIMULATION/VEHICLE label in the UI (brief §13).
struct GatewayIdentity {
    int protocolVersion = 0;
    QString firmwareVersion;
    QString buildId;
    QString source;        ///< "SIMULATION" or "VEHICLE" exactly as reported by the gateway
    quint32 bitrate = 0;
    QString requestId;     ///< e.g. "7E0"
    QString responseId;    ///< e.g. "7E8"
    QString idType;        ///< "STD" (11-bit)
    bool mode04Available = false;

    bool isSimulation() const { return source == QLatin1String("SIMULATION"); }
    bool isVehicle() const { return source == QLatin1String("VEHICLE"); }
};

/// Final outcome of one request (gateway answer, or a desktop-side timeout/disconnect).
struct GatewayReply {
    quint32 id = 0;
    QString command;
    bool ok = false;
    /// Valid when !ok. Desktop-side failures use Stage::SerialTransport / Stage::Protocol.
    diag::Error error{};
    QStringList positional;           ///< fields without '=' (e.g. "pong")
    QMap<QString, QString> values;    ///< key=value fields; DTC parts are merged into "dtcs"

    QString value(const QString& key) const { return values.value(key); }
    /// "NRC/NEGATIVE_RESPONSE/22" – compact text for logs and the developer screen.
    QString errorText() const;
};

/// Asynchronous gateway event (LIVE, LIVE_ERR, DIAG, BOOT, CAN_TX, …).
struct GatewayEvent {
    quint32 sequence = 0;
    QString kind;
    QStringList fields;
};

/// Copies the fields of a parsed message into @p positional / @p values.
void copyFields(const proto::Message& message, QStringList& positional, QMap<QString, QString>& values);
QString toQString(std::string_view text);

Q_DECLARE_METATYPE(GatewayIdentity)
Q_DECLARE_METATYPE(GatewayReply)
Q_DECLARE_METATYPE(GatewayEvent)
