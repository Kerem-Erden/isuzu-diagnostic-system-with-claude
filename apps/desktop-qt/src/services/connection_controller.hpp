#pragma once
/**
 * @file connection_controller.hpp
 * @brief Owns the links (simulation / COM port) and the GatewayClient, and opens an audit session
 *        for every verified gateway identity. UI-independent so it can be tested headless.
 *
 * The link key "::simulation" selects the in-process simulation; any other key is a COM port
 * name. A new SerialTransport is created per COM connection so nothing survives from a previous
 * device.
 */

#include <QObject>
#include <QString>

#include "gateway/gateway_client.hpp"

class AuditStore;
class SerialTransport;
class SimulationTransport;

class ConnectionController final : public QObject {
    Q_OBJECT
public:
    /// Link key of the in-process simulation (cannot collide with a COM port name).
    static QString simulationKey() { return QStringLiteral("::simulation"); }

    explicit ConnectionController(AuditStore* audit, QObject* parent = nullptr);

    /// Starts connecting. @return false if a connection is active.
    bool connectTo(const QString& linkKey);
    void disconnectLink();

    GatewayClient* client() const { return client_; }
    SimulationTransport* simulation() const { return simulation_; }
    QString linkDescription() const;
    /// Audit session id of the current connection (0 = none / audit unavailable).
    qint64 sessionId() const { return sessionId_; }
    /// Latest connection-level problem (failed/lost connection, protocol warning).
    QString lastError() const { return lastError_; }

signals:
    void sessionChanged(qint64 sessionId);
    void lastErrorChanged(const QString& message);

private:
    void setLastError(const QString& message);
    void onIdentity(const GatewayIdentity& identity);

    AuditStore* audit_;
    SimulationTransport* simulation_;
    SerialTransport* serial_ = nullptr;
    GatewayClient* client_;
    qint64 sessionId_ = 0;
    QString lastError_;
};
