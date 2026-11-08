#pragma once
/**
 * @file simulation_transport.hpp
 * @brief In-process gateway: gw::Gateway + gw::SimulatedEcu behind the Transport interface.
 *
 * SIMULATION — NO VEHICLE DATA. This runs the *same* gateway-core code as the ESP32 firmware
 * (ADR-003), so the desktop exercises the real protocol, ISO-TP and OBD paths without hardware.
 * The gateway announces source=SIMULATION in BOOT/HELLO; the desktop never infers the source
 * from which transport class is used.
 *
 * Bytes travel through inbox/outbox buffers processed by a 2 ms timer, so behaviour is
 * asynchronous like a serial port. Fault injection helpers (mute, noise, disconnect) exist for
 * tests and the developer screen.
 */

#include <QElapsedTimer>
#include <QTimer>
#include <memory>

#include "gw/gateway.hpp"
#include "gw/simulated_ecu.hpp"
#include "transport/transport.hpp"

class SimulationTransport final : public Transport {
    Q_OBJECT
public:
    explicit SimulationTransport(QObject* parent = nullptr, gw::GatewayConfig config = {});
    ~SimulationTransport() override;

    QString description() const override { return QStringLiteral("In-process simulation"); }
    bool open() override;
    void close() override;
    bool isOpen() const override { return gateway_ != nullptr; }
    void write(const QByteArray& bytes) override;

    /// Changes the simulated ECU behaviour; applies to the next CAN request.
    void setScenario(gw::EcuScenario scenario);
    /// Test hook: drop everything the desktop sends (simulates a dead link without closing it).
    void setLinkMuted(bool muted) { muted_ = muted; }
    /// Test hook: deliver arbitrary bytes to the desktop as if the gateway had sent them.
    void injectToDesktop(const QByteArray& bytes) { outbox_.append(bytes); }
    /// Test hook: behaves like a USB unplug.
    void simulateUnplug();

    /// Read-only access for tests.
    const gw::Gateway* gateway() const { return gateway_.get(); }

private:
    /// gateway → desktop: frames are buffered and emitted on the next tick (asynchronous).
    struct OutboxSink final : gw::SerialSink {
        QByteArray* outbox = nullptr;
        void write(std::string_view frame) override {
            outbox->append(frame.data(), static_cast<qsizetype>(frame.size()));
        }
    };

    void tick();
    std::uint64_t nowUs() const;
    void shutDown(const QString& reason);

    gw::GatewayConfig config_;
    gw::EcuScenario scenario_ = gw::EcuScenario::Normal;
    std::unique_ptr<gw::SimulatedEcu> ecu_;
    std::unique_ptr<gw::Gateway> gateway_;
    QTimer timer_;
    QElapsedTimer clock_;
    QByteArray inbox_;   ///< desktop → gateway, consumed on the next tick
    QByteArray outbox_;  ///< gateway → desktop, emitted on the next tick
    OutboxSink sink_;
    bool muted_ = false;
};
