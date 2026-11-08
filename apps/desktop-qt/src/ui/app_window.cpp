#include "ui/app_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "data/audit_store.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "models/view_models.hpp"
#include "services/connection_controller.hpp"
#include "services/dtc_scan_service.hpp"
#include "services/live_data_service.hpp"
#include "ui/pages.hpp"
#include "ui/style.hpp"

namespace {

QString errorText(const diag::Error& e) {
    return QStringLiteral("%1/%2/%3")
        .arg(QLatin1String(diag::toString(e.stage)), QLatin1String(diag::toString(e.code)))
        .arg(e.detail, 2, 16, QLatin1Char('0'))
        .toUpper();
}

}  // namespace

AppWindow::AppWindow(QWidget* parent)
    : QMainWindow(parent),
      knowledge_(std::make_unique<knowledge::KnowledgeRepository>(
          QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/isuzu-knowledge.db")))),
      audit_(std::make_unique<AuditStore>(
          QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("audit.db")))),
      settings_(std::make_unique<QSettings>()),
      banner_(new QLabel),
      sidebar_(new QListWidget),
      stack_(new QStackedWidget),
      statusLink_(new QLabel),
      statusGateway_(new QLabel),
      statusCan_(new QLabel),
      statusError_(new QLabel) {
    setWindowTitle(tr("Isuzu Diagnostic System"));
    resize(1180, 760);
    qApp->setStyleSheet(style::applicationStyleSheet());

    // ---- services (order matters: models are updated before pages react to the same signal)
    knowledge_->open();  // optional: scans still show raw codes without it
    audit_->open();      // optional: scans report "audit NOT saved" without it
    context_.knowledge = knowledge_.get();
    context_.audit = audit_.get();
    context_.settings = settings_.get();
    context_.connection = new ConnectionController(audit_.get(), this);
    context_.client = context_.connection->client();
    context_.scans = new DtcScanService(context_.client, knowledge_.get(), audit_.get(), this);
    context_.live = new LiveDataService(context_.client, this);
    context_.liveModel = new LiveDataModel(context_.live, this);
    context_.dtcModel = new DtcListModel(this);
    context_.log = new EventLogModel(2000, this);
    connect(context_.scans, &DtcScanService::scanFinished, context_.dtcModel, &DtcListModel::setResult);
    connect(context_.connection, &ConnectionController::sessionChanged, context_.scans, &DtcScanService::setSessionId);

    // ---- developer log routing (bounded model)
    connect(context_.client, &GatewayClient::replyReceived, this, &AppWindow::logReply);
    connect(context_.client, &GatewayClient::eventReceived, this, &AppWindow::logEvent);
    connect(context_.scans, &DtcScanService::scanFinished, this, &AppWindow::logScan);
    connect(context_.client, &GatewayClient::protocolWarning, this,
            [this](const QString& text) { context_.log->append(QStringLiteral("PROTOCOL"), QStringLiteral("WARNING"), text); });
    connect(context_.client, &GatewayClient::identityReady, this, [this](const GatewayIdentity& id) {
        context_.log->append(QStringLiteral("CONNECTION"), QStringLiteral("HELLO"),
                             tr("source=%1 fw=%2 build=%3 proto=%4 tx=%5 rx=%6 bitrate=%7")
                                 .arg(id.source, id.firmwareVersion, id.buildId)
                                 .arg(id.protocolVersion)
                                 .arg(id.requestId, id.responseId)
                                 .arg(id.bitrate));
    });
    connect(context_.client, &GatewayClient::connectionLost, this,
            [this](const QString& reason) { context_.log->append(QStringLiteral("CONNECTION"), QStringLiteral("LOST"), reason); });
    connect(context_.client, &GatewayClient::connectionFailed, this,
            [this](const QString& reason) { context_.log->append(QStringLiteral("CONNECTION"), QStringLiteral("FAILED"), reason); });
    context_.log->append(QStringLiteral("CONNECTION"), QStringLiteral("STARTUP"),
                         tr("knowledge DB: %1 · audit DB: %2")
                             .arg(knowledge_->isOpen() ? tr("ok") : errorText(knowledge_->openError()),
                                  audit_->isOpen() ? tr("ok") : tr("unavailable")));

    // ---- layout: banner on top, sidebar + pages, status bar
    banner_->setAlignment(Qt::AlignCenter);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(220);
    auto* brand = new QLabel(QStringLiteral("ISUZU"));
    brand->setObjectName(QStringLiteral("brand"));
    auto* brandSub = new QLabel(tr("Diagnostic System"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    auto* side = new QVBoxLayout;
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(0);
    side->addWidget(brand);
    side->addWidget(brandSub);
    side->addWidget(sidebar_, 1);

    auto* dashboard = new DashboardPage(context_);
    const QList<QPair<QString, QWidget*>> pages = {
        {tr("Connection"), new ConnectionPage(context_)},  {tr("Dashboard"), dashboard},
        {tr("Live data"), new LiveDataPage(context_)},     {tr("DTCs"), new DtcPage(context_)},
        {tr("History"), new HistoryPage(context_)},        {tr("Developer"), new DeveloperPage(context_)},
        {tr("Settings"), new SettingsPage(context_)},
    };
    for (const auto& [name, page] : pages) {
        sidebar_->addItem(name);
        stack_->addWidget(page);
    }
    connect(sidebar_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(dashboard, &DashboardPage::navigate, this, [this](int page) { sidebar_->setCurrentRow(page); });
    sidebar_->setCurrentRow(kConnectionPage);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addLayout(side);
    body->addWidget(stack_, 1);
    auto* root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(banner_);
    root->addLayout(body, 1);
    auto* central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);

    statusBar()->addWidget(statusLink_);
    statusBar()->addWidget(statusGateway_);
    statusBar()->addWidget(statusCan_);
    statusBar()->addWidget(statusError_, 1);

    connect(context_.client, &GatewayClient::stateChanged, this, &AppWindow::updateChrome);
    connect(context_.client, &GatewayClient::identityReady, this, &AppWindow::updateChrome);
    connect(context_.connection, &ConnectionController::lastErrorChanged, this, &AppWindow::updateChrome);
    updateChrome();
}

AppWindow::~AppWindow() = default;

void AppWindow::updateChrome() {
    const GatewayClient::State state = context_.client->state();
    const GatewayIdentity& id = context_.client->identity();
    if (state != GatewayClient::State::Ready) {
        banner_->setText(state == GatewayClient::State::Handshaking ? tr("CONNECTING …") : tr("NOT CONNECTED"));
        banner_->setStyleSheet(style::disconnectedBanner());
    } else if (id.isSimulation()) {
        banner_->setText(tr("SIMULATION — NO VEHICLE DATA"));
        banner_->setStyleSheet(style::simulationBanner());
    } else {
        banner_->setText(tr("VEHICLE — implemented but not vehicle-validated"));
        banner_->setStyleSheet(style::vehicleBanner());
    }
    const bool ready = state == GatewayClient::State::Ready;
    statusLink_->setText(tr("Link: %1 (%2)")
                             .arg(context_.connection->linkDescription(),
                                  ready ? tr("ready") : state == GatewayClient::State::Handshaking ? tr("handshake") : tr("closed")));
    statusGateway_->setText(ready ? tr("fw %1 · protocol %2").arg(id.firmwareVersion).arg(id.protocolVersion) : QString());
    statusCan_->setText(ready ? tr("CAN %1 bit/s · 0x%2 → 0x%3").arg(id.bitrate).arg(id.requestId, id.responseId) : QString());
    statusError_->setText(context_.connection->lastError().isEmpty() ? QString()
                                                                     : tr("Last error: %1").arg(context_.connection->lastError()));
}

void AppWindow::logReply(const GatewayReply& reply) {
    if (reply.ok && reply.command == QLatin1String("PING")) return;  // keep-alive would flood the log
    QStringList fields = reply.positional;
    for (auto it = reply.values.cbegin(); it != reply.values.cend(); ++it) fields << it.key() + QLatin1Char('=') + it.value();
    context_.log->append(QStringLiteral("REQUEST"), reply.command,
                         QStringLiteral("#%1 %2 %3").arg(reply.id).arg(reply.ok ? tr("OK") : errorText(reply.error), fields.join(QLatin1Char(' '))));
}

void AppWindow::logEvent(const GatewayEvent& event) {
    const QString category = event.kind.startsWith(QLatin1String("CAN_"))  ? QStringLiteral("CAN")
                             : event.kind.startsWith(QLatin1String("LIVE")) ? QStringLiteral("LIVE")
                                                                            : QStringLiteral("EVENT");
    context_.log->append(category, event.kind, QStringLiteral("seq %1 · %2").arg(event.sequence).arg(event.fields.join(QLatin1Char(' '))));
}

void AppWindow::logScan(const DtcScanResult& r) {
    QStringList codes;
    for (const DtcEntry& e : r.entries) codes << QStringLiteral("%1(%2)").arg(e.code, DtcListModel::statusText(e.status));
    context_.log->append(QStringLiteral("SCAN"), QStringLiteral("READ_DTC"),
                         tr("#%1 acquisition=%2 audit=%3 %4")
                             .arg(r.requestId)
                             .arg(r.acquired ? tr("OK") : errorText(r.acquisitionError),
                                  r.audited ? tr("saved") : errorText(r.auditError), codes.join(QLatin1Char(' '))));
}

void AppWindow::runScreenshotTour(const QString& directory) {
    QDir().mkpath(directory);
    // Strictly the in-process simulation: this tour must never open a serial port.
    context_.connection->connectTo(ConnectionController::simulationKey());
    auto* steps = new QTimer(this);
    auto step = std::make_shared<int>(0);
    connect(steps, &QTimer::timeout, this, [this, steps, step, directory] {
        switch ((*step)++) {
            case 0: context_.scans->startScan(); break;
            case 1: context_.live->start(500); break;
            default: {
                const int page = *step - 4;  // steps 2–3 let live data fill the table
                if (page < 0) break;
                if (page >= stack_->count()) {
                    steps->stop();
                    QCoreApplication::quit();
                    break;
                }
                sidebar_->setCurrentRow(page);
                QTimer::singleShot(300, this, [this, directory, page] {
                    grab().save(QDir(directory).filePath(QStringLiteral("page-%1.png").arg(page)));
                });
            }
        }
    });
    steps->start(900);
}
