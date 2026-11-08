#pragma once
/**
 * @file main_window.hpp
 * @brief Phase-3 skeleton window: connect (simulation), read DTCs, start/stop live data, event log.
 *
 * Deliberately minimal – it proves the client/transport wiring end to end. The real screens
 * (dashboard, live data, DTC list/detail, history, developer diagnostics, settings) follow in W8.
 * The source banner always shows what the *gateway* reported in HELLO, never an assumption.
 */

#include <QMainWindow>
#include <memory>

class AuditStore;
class DtcScanService;
class GatewayClient;
namespace knowledge {
class KnowledgeRepository;
}
struct DtcScanResult;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class SerialTransport;
class SimulationTransport;
struct GatewayIdentity;
struct GatewayReply;
struct GatewayEvent;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void onScan(const DtcScanResult& result);
    void onIdentity(const GatewayIdentity& identity);
    void onReply(const GatewayReply& reply);
    void onEvent(const GatewayEvent& event);
    void onDisconnected(const QString& reason);
    void log(const QString& text);
    void setConnectedUi(bool connected);
    void refreshPorts();
    void connectSelected();

    SimulationTransport* simulation_;
    SerialTransport* serial_ = nullptr;  ///< recreated for each COM connection
    GatewayClient* client_;
    QComboBox* portCombo_;
    QPushButton* refreshButton_;
    QLabel* banner_;
    QLabel* details_;
    QPushButton* connectButton_;
    QPushButton* readDtcButton_;
    QPushButton* liveButton_;
    QListWidget* log_;
    std::unique_ptr<knowledge::KnowledgeRepository> knowledge_;
    std::unique_ptr<AuditStore> audit_;
    DtcScanService* scanService_;
    bool liveRunning_ = false;
};
