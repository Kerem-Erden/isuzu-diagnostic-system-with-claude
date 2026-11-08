#include "gateway/gateway_client.hpp"

#include <QRandomGenerator>

#include "proto/message.hpp"

namespace {

using diag::Code;
using diag::Stage;

constexpr int kHelloTimeoutMs = 3000;
constexpr int kKeepAliveMs = 1500;      // < gateway session timeout (5 s) with margin for 2 losses
constexpr int kTimeoutCheckMs = 50;
constexpr int kMaxMissedKeepAlives = 2;

}  // namespace

GatewayClient::GatewayClient(Transport* transport, QObject* parent) : QObject(parent), transport_(transport) {
    qRegisterMetaType<GatewayIdentity>();
    qRegisterMetaType<GatewayReply>();
    qRegisterMetaType<GatewayEvent>();
    clock_.start();
    timeoutTimer_.setInterval(kTimeoutCheckMs);
    connect(&timeoutTimer_, &QTimer::timeout, this, &GatewayClient::checkTimeouts);
    keepAlive_.setInterval(kKeepAliveMs);
    connect(&keepAlive_, &QTimer::timeout, this, [this] { sendInternal(QStringLiteral("PING"), {}, keepAlive_.interval()); });
    connect(transport_, &Transport::bytesReceived, this, &GatewayClient::onBytes);
    connect(transport_, &Transport::closed, this, &GatewayClient::onTransportClosed);
    connect(transport_, &Transport::errorOccurred, this, &GatewayClient::protocolWarning);
}

void GatewayClient::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    if (state == State::Ready) keepAlive_.start(); else keepAlive_.stop();
    emit stateChanged(state);
}

quint32 GatewayClient::nextId() {
    if (lastId_ == 0 || lastId_ >= proto::kMaxId) {
        // Random start per connection: a stale reply from an earlier connection cannot match.
        lastId_ = QRandomGenerator::global()->bounded(1u, 1'000'000'000u);
    } else {
        ++lastId_;
    }
    return lastId_;
}

void GatewayClient::connectToGateway() {
    if (state_ != State::Disconnected) return;
    reader_.reset();
    lastId_ = 0;
    missedKeepAlives_ = 0;
    identity_ = GatewayIdentity{};
    if (!transport_->open()) {
        emit connectionFailed(QStringLiteral("could not open %1").arg(transport_->description()));
        return;
    }
    timeoutTimer_.start();
    setState(State::Handshaking);
    helloId_ = sendInternal(QStringLiteral("HELLO"), {}, kHelloTimeoutMs);
}

void GatewayClient::disconnectFromGateway() {
    if (transport_->isOpen()) transport_->close();  // → closed() → onTransportClosed()
}

quint32 GatewayClient::send(const QString& command, const QStringList& args, int timeoutMs) {
    if (state_ != State::Ready) {
        const quint32 id = nextId();
        GatewayReply reply;
        reply.id = id;
        reply.command = command;
        reply.error = diag::fail(Stage::SerialTransport, Code::Disconnected);
        // Asynchronous like every other outcome, so callers can connect after send() returns.
        QMetaObject::invokeMethod(this, [this, reply] { emit replyReceived(reply); }, Qt::QueuedConnection);
        return id;
    }
    return sendInternal(command, args, timeoutMs);
}

quint32 GatewayClient::sendInternal(const QString& command, const QStringList& args, int timeoutMs) {
    const quint32 id = nextId();
    proto::FrameWriter writer;
    const QByteArray commandBytes = command.toLatin1();
    proto::beginRequest(writer, id, std::string_view(commandBytes.constData(), static_cast<std::size_t>(commandBytes.size())));
    for (const QString& arg : args) {
        const QByteArray bytes = arg.toLatin1();
        writer.field(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    }
    const std::string_view frame = writer.finish();
    // Register before writing: the reply must find its pending entry even if it arrives quickly.
    pending_.insert(id, Pending{command, clock_.elapsed() + timeoutMs, {}});
    if (frame.empty()) {
        GatewayReply reply;
        reply.id = id;
        reply.command = command;
        reply.error = diag::fail(Stage::Protocol, Code::InvalidArgument);
        QMetaObject::invokeMethod(this, [this, id, reply] { finish(id, reply); }, Qt::QueuedConnection);
        return id;
    }
    transport_->write(QByteArray(frame.data(), static_cast<qsizetype>(frame.size())));
    return id;
}

void GatewayClient::onBytes(const QByteArray& bytes) {
    reader_.feed(bytes.constData(), static_cast<std::size_t>(bytes.size()), [this](std::string_view body) { onBody(body); });
}

void GatewayClient::onBody(std::string_view body) {
    const diag::Result<proto::Message> parsed = proto::parseBody(body);
    if (!parsed.ok()) {
        emit protocolWarning(QStringLiteral("unparsable frame (%1): %2")
                                 .arg(QLatin1String(diag::toString(parsed.error().code)), toQString(body)));
        return;
    }
    switch (parsed.value().type) {
        case proto::MessageType::Response: handleResponse(parsed.value()); break;
        case proto::MessageType::Event: handleEvent(parsed.value()); break;
        case proto::MessageType::Request:
            emit protocolWarning(QStringLiteral("unexpected request from gateway: %1").arg(toQString(body)));
            break;
    }
}

void GatewayClient::handleResponse(const proto::Message& message) {
    auto it = pending_.find(message.id);
    if (it == pending_.end()) {
        // Late reply after a desktop timeout, or a reply meant for an earlier connection.
        emit protocolWarning(QStringLiteral("response for unknown request id %1").arg(message.id));
        return;
    }
    GatewayReply reply;
    reply.id = message.id;
    reply.command = it->command;
    reply.ok = message.isOk();
    reply.error = message.error;
    copyFields(message, reply.positional, reply.values);

    if (reply.ok && reply.values.contains(QStringLiteral("parts"))) {
        // Long DTC list: the codes arrived in DTC_PART events before this response.
        const QStringList codes = it->dtcParts.join(QLatin1Char(',')).split(QLatin1Char(','), Qt::SkipEmptyParts);
        bool countOk = false;
        bool partsOk = false;
        const int count = reply.value(QStringLiteral("count")).toInt(&countOk);
        const int parts = reply.value(QStringLiteral("parts")).toInt(&partsOk);
        if (!countOk || !partsOk || parts != it->dtcParts.size() || count != codes.size()) {
            reply.ok = false;  // never show a silently truncated DTC list
            reply.error = diag::fail(Stage::Protocol, Code::MalformedPayload, static_cast<std::uint32_t>(codes.size()));
        } else {
            reply.values.insert(QStringLiteral("dtcs"), codes.join(QLatin1Char(',')));
        }
    }
    finish(message.id, reply);
}

void GatewayClient::handleEvent(const proto::Message& message) {
    GatewayEvent event;
    event.sequence = message.id;
    event.kind = toQString(message.name);
    for (std::size_t i = 0; i < message.argCount; ++i) event.fields.append(toQString(message.args[i]));

    if (event.kind == QLatin1String("DTC_PART")) {
        QMap<QString, QString> values;
        QStringList positional;
        copyFields(message, positional, values);
        auto it = pending_.find(values.value(QStringLiteral("req")).toUInt());
        if (it != pending_.end()) it->dtcParts.append(values.value(QStringLiteral("dtcs")));
    } else if (event.kind == QLatin1String("BOOT") && state_ == State::Ready) {
        // The ESP32 reset (brown-out, watchdog, USB re-enumeration). Its session state is gone, so
        // every pending request is lost and the identity must be verified again.
        emit protocolWarning(QStringLiteral("gateway restarted; repeating handshake"));
        failAll(diag::fail(Stage::SerialTransport, Code::Disconnected, 1));
        setState(State::Handshaking);
        helloId_ = sendInternal(QStringLiteral("HELLO"), {}, kHelloTimeoutMs);
    }
    emit eventReceived(event);
}

void GatewayClient::finish(quint32 id, GatewayReply reply) {
    pending_.remove(id);
    if (reply.ok && reply.command == QLatin1String("PING")) missedKeepAlives_ = 0;
    if (id == helloId_ && state_ == State::Handshaking) {
        helloId_ = 0;
        const QString previousSource = identity_.source;
        GatewayIdentity identity;
        identity.protocolVersion = reply.value(QStringLiteral("proto")).toInt();
        identity.firmwareVersion = reply.value(QStringLiteral("fw"));
        identity.buildId = reply.value(QStringLiteral("build"));
        identity.source = reply.value(QStringLiteral("source"));
        identity.bitrate = reply.value(QStringLiteral("bitrate")).toUInt();
        identity.requestId = reply.value(QStringLiteral("tx"));
        identity.responseId = reply.value(QStringLiteral("rx"));
        identity.idType = reply.value(QStringLiteral("ids"));
        identity.mode04Available = reply.value(QStringLiteral("mode04")) == QLatin1String("1");

        QString problem;
        if (!reply.ok) problem = QStringLiteral("HELLO failed: %1").arg(reply.errorText());
        else if (identity.protocolVersion != static_cast<int>(proto::kProtocolVersion))
            problem = QStringLiteral("unsupported protocol version %1").arg(identity.protocolVersion);
        else if (!identity.isSimulation() && !identity.isVehicle())
            problem = QStringLiteral("gateway did not identify its data source");
        else if (!previousSource.isEmpty() && previousSource != identity.source)
            problem = QStringLiteral("data source changed from %1 to %2").arg(previousSource, identity.source);
        // Fail closed: without a verified identity no diagnostic data may be shown.
        if (!problem.isEmpty()) {
            emit connectionFailed(problem);
            disconnectFromGateway();
        } else {
            identity_ = identity;
            setState(State::Ready);
            emit identityReady(identity_);
        }
    }
    emit replyReceived(reply);
}

void GatewayClient::checkTimeouts() {
    const qint64 now = clock_.elapsed();
    QList<quint32> expired;
    for (auto it = pending_.cbegin(); it != pending_.cend(); ++it) {
        if (now >= it->deadlineMs) expired.append(it.key());
    }
    int missedPings = 0;
    for (quint32 id : expired) {
        GatewayReply reply;
        reply.id = id;
        reply.command = pending_.value(id).command;
        reply.error = diag::fail(Stage::SerialTransport, Code::Timeout);
        if (reply.command == QLatin1String("PING")) ++missedPings;
        finish(id, reply);
    }
    missedKeepAlives_ += missedPings;
    if (missedPings > 0 && missedKeepAlives_ >= kMaxMissedKeepAlives) {
        // The link is open but dead (e.g. gateway hung, VM grabbed the USB device): give up
        // instead of letting requests time out one by one forever.
        emit protocolWarning(QStringLiteral("gateway stopped answering PING"));
        disconnectFromGateway();
    }
}

void GatewayClient::failAll(const diag::Error& error) {
    const QList<quint32> ids = pending_.keys();
    for (quint32 id : ids) {
        GatewayReply reply;
        reply.id = id;
        reply.command = pending_.value(id).command;
        reply.error = error;
        finish(id, reply);
    }
}

void GatewayClient::onTransportClosed(const QString& reason) {
    timeoutTimer_.stop();
    helloId_ = 0;
    failAll(diag::fail(Stage::SerialTransport, Code::Disconnected));
    setState(State::Disconnected);
    emit connectionLost(reason);
}
