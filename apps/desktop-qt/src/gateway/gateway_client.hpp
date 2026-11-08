#pragma once
/**
 * @file gateway_client.hpp
 * @brief Desktop side of protocol v1: handshake, request correlation, timeouts, keep-alive.
 *
 * Responsibilities:
 *  - frame the byte stream (proto::FrameReader – the same code as the firmware),
 *  - assign request ids (random start per connection, so a late reply from a previous connection
 *    cannot match a new request),
 *  - match responses by id only; unknown/late responses become protocolWarning(),
 *  - fail every pending request on timeout or disconnect – never leave the UI waiting,
 *  - merge DTC_PART events into the final READ_DTC reply and verify the count,
 *  - send PING every keep-alive interval while ready (the gateway ends idle sessions after 5 s).
 *
 * Errors created on the desktop side:
 *  - Stage::SerialTransport / Code::Timeout      no reply within the request timeout
 *  - Stage::SerialTransport / Code::Disconnected link closed while waiting
 *  - Stage::Protocol / Code::MalformedPayload     DTC parts inconsistent with count
 *
 * Lives on the GUI thread; the Transport may live elsewhere (signals are queued). The transport is
 * not owned.
 */

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

#include "gateway/gateway_types.hpp"
#include "proto/frame.hpp"
#include "transport/transport.hpp"

class GatewayClient final : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Handshaking, Ready };
    Q_ENUM(State)

    explicit GatewayClient(Transport* transport, QObject* parent = nullptr);

    /// Opens the transport and sends HELLO. Result: identityReady() or connectionFailed().
    void connectToGateway();
    void disconnectFromGateway();
    /// Switches the link (e.g. simulation ↔ COM port). Only while Disconnected and the current
    /// transport is closed. The transport is not owned. @return false if not allowed now.
    bool setTransport(Transport* transport);

    /**
     * @brief Sends one request. @return its id; the outcome arrives as replyReceived(id, …).
     * Requests issued while not Ready fail asynchronously with Disconnected.
     */
    quint32 send(const QString& command, const QStringList& args = {}, int timeoutMs = 3000);

    State state() const { return state_; }
    const GatewayIdentity& identity() const { return identity_; }
    Transport* transport() const { return transport_; }

    void setKeepAliveInterval(int ms) { keepAlive_.setInterval(ms); }
    const proto::FrameReader::Counters& framingCounters() const { return reader_.counters(); }

signals:
    void stateChanged(GatewayClient::State state);
    void identityReady(const GatewayIdentity& identity);
    void connectionFailed(const QString& reason);
    void connectionLost(const QString& reason);
    void replyReceived(const GatewayReply& reply);
    void eventReceived(const GatewayEvent& event);
    /// Something unexpected but survivable (unknown response id, unparsable frame, gateway reboot).
    void protocolWarning(const QString& message);

private:
    struct Pending {
        QString command;
        qint64 deadlineMs = 0;
        QStringList dtcParts;  ///< collected DTC_PART fragments (READ_DTC only)
    };

    void attachTransport();
    quint32 sendInternal(const QString& command, const QStringList& args, int timeoutMs);
    void onBytes(const QByteArray& bytes);
    void onBody(std::string_view body);
    void handleResponse(const proto::Message& message);
    void handleEvent(const proto::Message& message);
    void onTransportClosed(const QString& reason);
    void checkTimeouts();
    void failAll(const diag::Error& error);
    void finish(quint32 id, GatewayReply reply);
    void setState(State state);
    quint32 nextId();

    Transport* transport_;
    proto::FrameReader reader_;
    QHash<quint32, Pending> pending_;
    quint32 lastId_ = 0;
    quint32 helloId_ = 0;
    int missedKeepAlives_ = 0;
    bool failureReported_ = false;
    State state_ = State::Disconnected;
    GatewayIdentity identity_;
    QElapsedTimer clock_;
    QTimer timeoutTimer_;
    QTimer keepAlive_;
};
