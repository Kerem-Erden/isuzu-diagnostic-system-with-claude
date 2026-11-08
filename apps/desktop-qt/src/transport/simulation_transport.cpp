#include "transport/simulation_transport.hpp"

SimulationTransport::SimulationTransport(QObject* parent, gw::GatewayConfig config)
    : Transport(parent), config_(config) {
    config_.source = gw::DataSource::Simulation;  // this transport can never claim VEHICLE
    sink_.outbox = &outbox_;
    timer_.setInterval(2);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &SimulationTransport::tick);
}

SimulationTransport::~SimulationTransport() {
    // No signals from a half-destroyed object.
    timer_.stop();
}

bool SimulationTransport::open() {
    if (isOpen()) return true;
    clock_.start();
    ecu_ = std::make_unique<gw::SimulatedEcu>(config_.isotp);
    gateway_ = std::make_unique<gw::Gateway>(config_, *ecu_, sink_);
    ecu_->attach(*gateway_);
    ecu_->setScenario(scenario_);
    inbox_.clear();
    outbox_.clear();
    gateway_->start(nowUs());  // emits BOOT into the outbox like a freshly reset ESP32
    timer_.start();
    return true;
}

void SimulationTransport::close() { shutDown(QStringLiteral("closed by user")); }

void SimulationTransport::simulateUnplug() { shutDown(QStringLiteral("device removed")); }

void SimulationTransport::shutDown(const QString& reason) {
    if (!isOpen()) return;
    timer_.stop();
    gateway_.reset();
    ecu_.reset();
    inbox_.clear();
    outbox_.clear();
    // Queued so that closed() is never emitted from inside a caller's write()/close() call stack.
    QMetaObject::invokeMethod(this, [this, reason] { emit closed(reason); }, Qt::QueuedConnection);
}

void SimulationTransport::setScenario(gw::EcuScenario scenario) {
    scenario_ = scenario;
    if (ecu_) ecu_->setScenario(scenario);
}

void SimulationTransport::write(const QByteArray& bytes) {
    if (!isOpen() || muted_) return;
    inbox_.append(bytes);
}

std::uint64_t SimulationTransport::nowUs() const {
    // Offset by 1 s so timestamps are never 0 (0 is the "not yet armed" value in the core).
    return static_cast<std::uint64_t>(clock_.nsecsElapsed() / 1000) + 1'000'000u;
}

void SimulationTransport::tick() {
    if (!isOpen()) return;
    const std::uint64_t now = nowUs();
    if (!inbox_.isEmpty()) {
        const QByteArray pending = std::exchange(inbox_, {});
        gateway_->onSerialBytes(pending.constData(), static_cast<std::size_t>(pending.size()), now);
    }
    ecu_->poll(now);
    gateway_->poll(now);
    if (!outbox_.isEmpty()) emit bytesReceived(std::exchange(outbox_, {}));
}
