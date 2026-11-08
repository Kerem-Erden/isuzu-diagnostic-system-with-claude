#include "ui/main_window.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/audit_store.hpp"
#include "gateway/gateway_client.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "services/dtc_scan_service.hpp"
#include "services/live_data_service.hpp"
#include "transport/serial_transport.hpp"
#include "transport/simulation_transport.hpp"

namespace {
constexpr int kMaxLogLines = 500;  // bounded: the developer screen must never grow without limit
const QString kSimulationKey = QStringLiteral("::simulation");  // cannot collide with a COM name
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      simulation_(new SimulationTransport(this)),
      client_(new GatewayClient(simulation_, this)),
      portCombo_(new QComboBox),
      refreshButton_(new QPushButton(tr("Refresh ports"))),
      banner_(new QLabel(tr("NOT CONNECTED"))),
      details_(new QLabel),
      connectButton_(new QPushButton(tr("Connect"))),
      readDtcButton_(new QPushButton(tr("Read DTCs"))),
      liveButton_(new QPushButton(tr("Start live data"))),
      log_(new QListWidget),
      knowledge_(std::make_unique<knowledge::KnowledgeRepository>(
          QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/isuzu-knowledge.db")))),
      audit_(std::make_unique<AuditStore>(
          QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("audit.db")))),
      scanService_(new DtcScanService(client_, knowledge_.get(), audit_.get(), this)),
      liveService_(new LiveDataService(client_, this)),
      liveTable_(new QTableWidget) {
    setWindowTitle(tr("Isuzu Diagnostic System"));
    resize(1000, 700);

    liveTable_->setColumnCount(6);
    liveTable_->setHorizontalHeaderLabels({tr("Parameter"), tr("Value"), tr("Unit"), tr("State"), tr("Updated"), tr("Source")});
    liveTable_->setRowCount(static_cast<int>(liveService_->parameters().size()));
    liveTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    liveTable_->verticalHeader()->setVisible(false);
    liveTable_->horizontalHeader()->setStretchLastSection(true);
    for (int row = 0; row < liveTable_->rowCount(); ++row) {
        for (int column = 0; column < liveTable_->columnCount(); ++column) liveTable_->setItem(row, column, new QTableWidgetItem);
        updateLiveRow(row);
    }
    liveTable_->setMaximumHeight(liveTable_->verticalHeader()->length() + liveTable_->horizontalHeader()->height() + 4);

    banner_->setAlignment(Qt::AlignCenter);
    banner_->setStyleSheet(QStringLiteral("font-weight:600; padding:8px; background:#E8EAED; color:#30343B;"));
    details_->setStyleSheet(QStringLiteral("color:#68707C;"));

    portCombo_->setMinimumWidth(320);
    refreshPorts();

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(portCombo_);
    buttons->addWidget(refreshButton_);
    buttons->addWidget(connectButton_);
    buttons->addWidget(readDtcButton_);
    buttons->addWidget(liveButton_);
    buttons->addStretch();

    auto* layout = new QVBoxLayout;
    layout->addWidget(banner_);
    layout->addWidget(details_);
    layout->addLayout(buttons);
    layout->addWidget(liveTable_);
    layout->addWidget(log_, 1);
    auto* central = new QWidget;
    central->setLayout(layout);
    setCentralWidget(central);
    setConnectedUi(false);

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(connectButton_, &QPushButton::clicked, this, [this] {
        if (client_->state() == GatewayClient::State::Disconnected) connectSelected();
        else client_->disconnectFromGateway();
    });
    connect(readDtcButton_, &QPushButton::clicked, scanService_, &DtcScanService::startScan);
    connect(scanService_, &DtcScanService::scanFinished, this, &MainWindow::onScan);

    // Both databases are optional at runtime: without them scans still show raw ECU codes.
    const diag::Error knowledgeOpen = knowledge_->open();
    log(knowledgeOpen.isOk()
            ? tr("Knowledge DB %1 (dataset %2)").arg(knowledge_->databasePath(), knowledge_->datasetVersion())
            : tr("Knowledge DB unavailable (%1/%2): DTCs will be shown without descriptions")
                  .arg(QLatin1String(diag::toString(knowledgeOpen.stage)), QLatin1String(diag::toString(knowledgeOpen.code))));
    const diag::Error auditOpen = audit_->open();
    log(auditOpen.isOk() ? tr("Audit DB %1 (schema v%2)").arg(audit_->databasePath()).arg(audit_->schemaVersion())
                         : tr("Audit DB unavailable (%1): history will not be recorded")
                               .arg(QLatin1String(diag::toString(auditOpen.code))));
    connect(liveButton_, &QPushButton::clicked, this, [this] {
        if (liveService_->running()) liveService_->stop();
        else liveService_->start(1000);
    });
    connect(liveService_, &LiveDataService::parameterChanged, this, &MainWindow::updateLiveRow);
    connect(liveService_, &LiveDataService::started, this, [this](int period) {
        liveButton_->setText(tr("Stop live data"));
        log(tr("Live data started (%1 ms cycle)").arg(period));
    });
    connect(liveService_, &LiveDataService::stopped, this, [this](const QString& reason) {
        liveButton_->setText(tr("Start live data"));
        log(tr("Live data stopped: %1").arg(reason));
    });
    connect(liveService_, &LiveDataService::startFailed, this, [this](const diag::Error& error) {
        log(tr("Live data could not start: %1/%2").arg(QLatin1String(diag::toString(error.stage)), QLatin1String(diag::toString(error.code))));
    });
    connect(client_, &GatewayClient::identityReady, this, &MainWindow::onIdentity);
    connect(client_, &GatewayClient::replyReceived, this, &MainWindow::onReply);
    connect(client_, &GatewayClient::eventReceived, this, &MainWindow::onEvent);
    connect(client_, &GatewayClient::connectionLost, this, &MainWindow::onDisconnected);
    connect(client_, &GatewayClient::connectionFailed, this, [this](const QString& reason) {
        setConnectedUi(false);
        log(tr("Connection failed: %1").arg(reason));
    });
    connect(client_, &GatewayClient::protocolWarning, this, [this](const QString& text) { log(tr("Protocol: %1").arg(text)); });
}

// Out of line so unique_ptr can see the complete repository/store types.
MainWindow::~MainWindow() = default;

void MainWindow::updateLiveRow(int row) {
    const LiveParameter& p = liveService_->parameters().at(row);
    using State = LiveParameter::State;
    QString state;
    QColor color(QStringLiteral("#68707C"));
    switch (p.state) {
        case State::NoData: state = tr("no data"); break;
        case State::Fresh: state = tr("fresh"); color = QColor(QStringLiteral("#1E7B34")); break;
        case State::Stale: state = tr("stale"); color = QColor(QStringLiteral("#B26A00")); break;
        case State::Unsupported: state = tr("unsupported by ECU"); break;
        case State::Error:
            state = tr("error %1/%2").arg(QLatin1String(diag::toString(p.lastError.stage)), QLatin1String(diag::toString(p.lastError.code)));
            color = QColor(QStringLiteral("#A5001E"));
            break;
    }
    liveTable_->item(row, 0)->setText(p.displayName);
    // A stale or failed value stays visible (greyed) but is never presented as current.
    liveTable_->item(row, 1)->setText(p.hasValue ? QString::number(p.value, 'f', 2) : QStringLiteral("—"));
    liveTable_->item(row, 1)->setForeground(p.state == State::Fresh ? QColor(QStringLiteral("#30343B")) : QColor(QStringLiteral("#9AA0A8")));
    liveTable_->item(row, 2)->setText(p.unit);
    liveTable_->item(row, 3)->setText(state);
    liveTable_->item(row, 3)->setForeground(color);
    liveTable_->item(row, 4)->setText(p.hasValue ? tr("gateway t=%1 ms").arg(p.gatewayTimeMs) : QString());
    liveTable_->item(row, 5)->setText(p.source);
}

void MainWindow::refreshPorts() {
    const QString previous = portCombo_->currentData().toString();
    portCombo_->clear();
    portCombo_->addItem(tr("Simulation (no vehicle)"), kSimulationKey);
    for (const SerialPortEntry& port : availableSerialPorts()) {
        QString label = QStringLiteral("%1 — %2").arg(port.portName, port.description);
        if (port.likelyGateway) label += tr("  [CP210x – ESP32 gateway?]");
        portCombo_->addItem(label, port.portName);
    }
    const int index = portCombo_->findData(previous);
    portCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void MainWindow::connectSelected() {
    const QString key = portCombo_->currentData().toString();
    // Point the client away from the old serial transport before deleting it.
    if (!client_->setTransport(simulation_)) {
        log(tr("Cannot switch link while connected"));
        return;
    }
    Transport* transport = simulation_;
    if (key != kSimulationKey) {
        // A fresh transport per COM connection: no state survives from a previous device.
        // Deleting waits ≤ 2 s for a worker thread that might still be inside the driver.
        delete serial_;
        serial_ = new SerialTransport(key, this);
        transport = serial_;
    }
    if (!client_->setTransport(transport)) {
        log(tr("Cannot switch link while connected"));
        return;
    }
    banner_->setText(tr("CONNECTING to %1 …").arg(transport->description()));
    connectButton_->setText(tr("Cancel"));
    portCombo_->setEnabled(false);
    refreshButton_->setEnabled(false);
    client_->connectToGateway();
}

void MainWindow::setConnectedUi(bool connected) {
    connectButton_->setText(connected ? tr("Disconnect") : tr("Connect"));
    portCombo_->setEnabled(!connected);
    refreshButton_->setEnabled(!connected);
    readDtcButton_->setEnabled(connected);
    liveButton_->setEnabled(connected);
    if (!connected) liveButton_->setText(tr("Start live data"));
}

void MainWindow::onIdentity(const GatewayIdentity& identity) {
    if (identity.isSimulation()) {
        banner_->setText(tr("SIMULATION — NO VEHICLE DATA"));
        banner_->setStyleSheet(QStringLiteral("font-weight:700; padding:8px; background:#FFF1F3; color:#A5001E; border:2px solid #A5001E;"));
    } else {
        banner_->setText(tr("VEHICLE — implemented but not vehicle-validated"));
        banner_->setStyleSheet(QStringLiteral("font-weight:700; padding:8px; background:#A5001E; color:#FFFFFF;"));
    }
    details_->setText(tr("%1 · fw %2 (%3) · protocol %4 · CAN %5 bit/s · TX %6 / RX %7 · %8-bit IDs")
                          .arg(client_->transport()->description(), identity.firmwareVersion, identity.buildId)
                          .arg(identity.protocolVersion)
                          .arg(identity.bitrate)
                          .arg(identity.requestId, identity.responseId,
                               identity.idType == QLatin1String("STD") ? QStringLiteral("11") : QStringLiteral("29")));
    setConnectedUi(true);
    log(tr("Connected: source=%1").arg(identity.source));

    qint64 sessionId = 0;
    const diag::Error session = audit_->isOpen() ? audit_->beginSession(identity, client_->transport()->description(), sessionId)
                                                 : diag::fail(diag::Stage::Audit, diag::Code::InvalidState);
    scanService_->setSessionId(session.isOk() ? sessionId : 0);
    if (!session.isOk()) log(tr("Audit session not created (%1)").arg(QLatin1String(diag::toString(session.code))));
}

void MainWindow::onScan(const DtcScanResult& result) {
    const QString source = result.source.isEmpty() ? tr("?") : result.source;
    if (!result.acquired) {
        log(tr("DTC scan #%1 [%2] FAILED at %3/%4/%5")
                .arg(result.requestId)
                .arg(source, QLatin1String(diag::toString(result.acquisitionError.stage)),
                     QLatin1String(diag::toString(result.acquisitionError.code)))
                .arg(result.acquisitionError.detail, 2, 16, QLatin1Char('0')));
    } else {
        log(tr("DTC scan #%1 [%2] ECU %3: %n stored code(s)", nullptr, static_cast<int>(result.entries.size()))
                .arg(result.requestId)
                .arg(source, result.ecu));
        for (const DtcEntry& entry : result.entries) {
            switch (entry.status) {
                case EnrichmentStatus::Known:
                    log(tr("   %1  %2  (%3)").arg(entry.code, entry.knowledge.title, entry.knowledge.system));
                    break;
                case EnrichmentStatus::Unknown:
                    log(tr("   %1  Unknown DTC – not in the knowledge database").arg(entry.code));
                    break;
                case EnrichmentStatus::KnowledgeUnavailable:
                    log(tr("   %1  (knowledge unavailable: %2)").arg(entry.code, QLatin1String(diag::toString(entry.knowledgeError.code))));
                    break;
            }
        }
    }
    // Audit outcome is reported separately – it never changes the scan result above.
    log(result.audited ? tr("   audit: saved") : tr("   audit: NOT saved (%1)").arg(QLatin1String(diag::toString(result.auditError.code))));
}

void MainWindow::onReply(const GatewayReply& reply) {
    if (reply.command == QLatin1String("PING") && reply.ok) return;  // keep-alive noise
    // READ_DTC and LIVE_* outcomes are reported by their services.
    if (reply.command == QLatin1String("READ_DTC") || reply.command.startsWith(QLatin1String("LIVE_"))) return;
    log(tr("%1 #%2 → %3").arg(reply.command).arg(reply.id).arg(reply.ok ? tr("OK") : reply.errorText()));
}

void MainWindow::onEvent(const GatewayEvent& event) {
    // Live samples go to the table; only other events are logged.
    if (event.kind != QLatin1String("LIVE") && event.kind != QLatin1String("LIVE_ERR") && event.kind != QLatin1String("DTC_PART")) {
        log(tr("EVT %1 %2").arg(event.kind, event.fields.join(QLatin1Char(' '))));
    }
}

void MainWindow::onDisconnected(const QString& reason) {
    banner_->setText(tr("NOT CONNECTED"));
    banner_->setStyleSheet(QStringLiteral("font-weight:600; padding:8px; background:#E8EAED; color:#30343B;"));
    details_->clear();
    setConnectedUi(false);
    log(tr("Disconnected: %1").arg(reason));
}

void MainWindow::log(const QString& text) {
    log_->addItem(QStringLiteral("%1  %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), text));
    while (log_->count() > kMaxLogLines) delete log_->takeItem(0);
    log_->scrollToBottom();
}
