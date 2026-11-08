#include "services/connection_controller.hpp"

#include "data/audit_store.hpp"
#include "transport/serial_transport.hpp"
#include "transport/simulation_transport.hpp"

ConnectionController::ConnectionController(AuditStore* audit, QObject* parent)
    : QObject(parent),
      audit_(audit),
      simulation_(new SimulationTransport(this)),
      client_(new GatewayClient(simulation_, this)) {
    connect(client_, &GatewayClient::identityReady, this, &ConnectionController::onIdentity);
    connect(client_, &GatewayClient::connectionFailed, this,
            [this](const QString& reason) { setLastError(tr("Connection failed: %1").arg(reason)); });
    connect(client_, &GatewayClient::connectionLost, this, [this](const QString& reason) {
        setLastError(tr("Connection lost: %1").arg(reason));
        sessionId_ = 0;
        emit sessionChanged(0);
    });
    connect(client_, &GatewayClient::protocolWarning, this, &ConnectionController::setLastError);
}

QString ConnectionController::linkDescription() const { return client_->transport()->description(); }

void ConnectionController::setLastError(const QString& message) {
    lastError_ = message;
    emit lastErrorChanged(message);
}

bool ConnectionController::connectTo(const QString& linkKey) {
    if (client_->state() != GatewayClient::State::Disconnected) return false;
    // Point the client away from the old serial transport before deleting it.
    if (!client_->setTransport(simulation_)) return false;
    Transport* transport = simulation_;
    if (linkKey != simulationKey()) {
        // Deleting waits ≤ 2 s for a worker thread that might still be inside the driver.
        delete serial_;
        serial_ = new SerialTransport(linkKey, this);
        transport = serial_;
    }
    if (!client_->setTransport(transport)) return false;
    client_->connectToGateway();
    return true;
}

void ConnectionController::disconnectLink() { client_->disconnectFromGateway(); }

void ConnectionController::onIdentity(const GatewayIdentity& identity) {
    sessionId_ = 0;
    if (audit_ != nullptr && audit_->isOpen()) {
        qint64 id = 0;
        const diag::Error error = audit_->beginSession(identity, linkDescription(), id);
        if (error.isOk()) sessionId_ = id;
        else setLastError(tr("Audit session not created (%1)").arg(QLatin1String(diag::toString(error.code))));
    }
    emit sessionChanged(sessionId_);
}
