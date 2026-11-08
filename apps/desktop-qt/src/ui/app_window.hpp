#pragma once
/**
 * @file app_window.hpp
 * @brief Application shell: source banner, sidebar navigation, page stack, status bar.
 *
 * Creates and owns every service once (AppContext) and routes gateway activity into the bounded
 * developer event log. The banner and status bar always reflect what the gateway reported in HELLO
 * (brief §17: source, COM port, firmware/protocol version, IDs/bitrate, connection state, last error).
 */

#include <QMainWindow>
#include <memory>

#include "ui/app_context.hpp"

class QLabel;
class QListWidget;
class QStackedWidget;
struct DtcScanResult;
struct GatewayEvent;
struct GatewayReply;

class AppWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit AppWindow(QWidget* parent = nullptr);
    ~AppWindow() override;

    /**
     * @brief Developer aid for visual review: connects to the SIMULATION, reads DTCs, runs live data,
     *        saves a PNG of every page into @p directory and quits. Never touches a COM port.
     */
    void runScreenshotTour(const QString& directory);

private:
    void updateChrome();
    void logReply(const GatewayReply& reply);
    void logEvent(const GatewayEvent& event);
    void logScan(const DtcScanResult& result);

    std::unique_ptr<knowledge::KnowledgeRepository> knowledge_;
    std::unique_ptr<AuditStore> audit_;
    std::unique_ptr<QSettings> settings_;
    AppContext context_;
    QLabel* banner_;
    QListWidget* sidebar_;
    QStackedWidget* stack_;
    QLabel* statusLink_;
    QLabel* statusGateway_;
    QLabel* statusCan_;
    QLabel* statusError_;
};
