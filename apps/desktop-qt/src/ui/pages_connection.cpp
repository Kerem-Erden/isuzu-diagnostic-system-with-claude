// Page base class, ConnectionPage and DashboardPage.
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "knowledge/knowledge_repository.hpp"
#include "models/view_models.hpp"
#include "services/connection_controller.hpp"
#include "services/dtc_scan_service.hpp"
#include "services/live_data_service.hpp"
#include "transport/serial_transport.hpp"
#include "ui/pages.hpp"

Page::Page(const QString& title, const QString& hint, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("page"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 18);
    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    if (!hint.isEmpty()) {
        auto* hintLabel = new QLabel(hint);
        hintLabel->setObjectName(QStringLiteral("hint"));
        hintLabel->setWordWrap(true);
        layout->addWidget(hintLabel);
    }
    content_ = new QVBoxLayout;
    content_->setSpacing(10);
    layout->addLayout(content_, 1);
}

namespace {

/// White card with a title and a large value line (dashboard / identity panels).
QFrame* card(const QString& title, QLabel*& value, QLabel*& detail) {
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(frame);
    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("cardTitle"));
    value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("cardValue"));
    detail = new QLabel;
    detail->setObjectName(QStringLiteral("hint"));
    detail->setWordWrap(true);
    detail->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(heading);
    layout->addWidget(value);
    layout->addWidget(detail);
    layout->addStretch();  // keep the text at the top of the card
    return frame;
}

}  // namespace

// ============================================================================ ConnectionPage

ConnectionPage::ConnectionPage(AppContext& context, QWidget* parent)
    : Page(tr("Connection & vehicle"),
           tr("Choose the gateway link. The data source (SIMULATION or VEHICLE) is taken from the gateway's own "
              "HELLO answer, never from this selection."),
           parent),
      context_(context),
      ports_(new QComboBox),
      refresh_(new QPushButton(tr("Refresh"))),
      connect_(new QPushButton(tr("Connect"))),
      state_(new QLabel),
      profile_(new QComboBox) {
    connect_->setObjectName(QStringLiteral("primary"));
    ports_->setMinimumWidth(380);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Link:")));
    row->addWidget(ports_, 1);
    row->addWidget(refresh_);
    row->addWidget(connect_);
    content_->addLayout(row);
    content_->addWidget(state_);

    // Vehicle / ECU profile: in the MVP it selects the knowledge-base application used to enrich
    // DTCs. It does NOT change CAN addressing (brief §11) – that is fixed by the firmware build.
    auto* profileRow = new QHBoxLayout;
    profileRow->addWidget(new QLabel(tr("Vehicle / engine profile:")));
    profileRow->addWidget(profile_, 1);
    content_->addLayout(profileRow);
    auto* profileHint = new QLabel(
        tr("Used to prefer matching service-manual content for DTC descriptions. CAN bitrate and ECU request/"
           "response IDs come from the firmware configuration and are shown below; changing them from the "
           "desktop is not supported in this version."));
    profileHint->setObjectName(QStringLiteral("hint"));
    profileHint->setWordWrap(true);
    content_->addWidget(profileHint);

    profile_->addItem(tr("Generic Isuzu (no specific application)"), QString());
    if (context_.knowledge->isOpen()) {
        for (const QString& key : context_.knowledge->applicationKeys()) profile_->addItem(key, key);
    }
    const QString savedProfile = context_.settings->value(settings_keys::kApplicationKey).toString();
    profile_->setCurrentIndex(std::max(0, profile_->findData(savedProfile)));
    context_.scans->setApplicationKey(profile_->currentData().toString());
    connect(profile_, &QComboBox::currentIndexChanged, this, [this] {
        const QString key = profile_->currentData().toString();
        context_.settings->setValue(settings_keys::kApplicationKey, key);
        context_.scans->setApplicationKey(key);
    });

    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    identityForm_ = new QFormLayout(frame);
    const QString labels[8] = {tr("Data source"), tr("Firmware"), tr("Build"), tr("Protocol"),
                               tr("CAN bitrate"), tr("Request / response ID"), tr("Identifier type"), tr("Mode 04 (clear DTCs)")};
    for (int i = 0; i < 8; ++i) {
        identity_[i] = new QLabel(QStringLiteral("—"));
        identity_[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        identityForm_->addRow(labels[i], identity_[i]);
    }
    content_->addWidget(frame);
    content_->addStretch();

    connect(refresh_, &QPushButton::clicked, this, &ConnectionPage::refreshPorts);
    connect(connect_, &QPushButton::clicked, this, [this] {
        if (context_.client->state() == GatewayClient::State::Disconnected) {
            const QString key = ports_->currentData().toString();
            context_.settings->setValue(settings_keys::kLastLink, key);
            context_.connection->connectTo(key);
        } else {
            context_.connection->disconnectLink();
        }
    });
    connect(context_.client, &GatewayClient::stateChanged, this, &ConnectionPage::updateState);
    connect(context_.client, &GatewayClient::identityReady, this, &ConnectionPage::showIdentity);
    connect(context_.connection, &ConnectionController::lastErrorChanged, this, &ConnectionPage::updateState);
    refreshPorts();
    updateState();
}

void ConnectionPage::refreshPorts() {
    const QString previous = ports_->count() > 0 ? ports_->currentData().toString()
                                                 : context_.settings->value(settings_keys::kLastLink).toString();
    ports_->clear();
    ports_->addItem(tr("Simulation (no vehicle) – in-process gateway"), ConnectionController::simulationKey());
    for (const SerialPortEntry& port : availableSerialPorts()) {
        QString label = QStringLiteral("%1 — %2").arg(port.portName, port.description);
        if (port.likelyGateway) label += tr("   [CP210x · ESP32 gateway?]");
        ports_->addItem(label, port.portName);
    }
    ports_->setCurrentIndex(std::max(0, ports_->findData(previous)));
}

void ConnectionPage::updateState() {
    const GatewayClient::State state = context_.client->state();
    const bool idle = state == GatewayClient::State::Disconnected;
    ports_->setEnabled(idle);
    refresh_->setEnabled(idle);
    connect_->setText(idle ? tr("Connect") : state == GatewayClient::State::Ready ? tr("Disconnect") : tr("Cancel"));
    QString text = state == GatewayClient::State::Ready ? tr("Connected via %1").arg(context_.connection->linkDescription())
                   : state == GatewayClient::State::Handshaking ? tr("Connecting to %1 …").arg(context_.connection->linkDescription())
                                                                : tr("Not connected");
    if (idle && !context_.connection->lastError().isEmpty()) text += QStringLiteral(" — ") + context_.connection->lastError();
    state_->setText(text);
    if (idle) {
        for (QLabel* label : identity_) label->setText(QStringLiteral("—"));
    }
}

void ConnectionPage::showIdentity(const GatewayIdentity& identity) {
    identity_[0]->setText(identity.isSimulation() ? tr("SIMULATION — NO VEHICLE DATA") : tr("VEHICLE"));
    identity_[1]->setText(identity.firmwareVersion);
    identity_[2]->setText(identity.buildId);
    identity_[3]->setText(QString::number(identity.protocolVersion));
    identity_[4]->setText(tr("%1 bit/s").arg(identity.bitrate));
    identity_[5]->setText(QStringLiteral("0x%1 / 0x%2").arg(identity.requestId, identity.responseId));
    identity_[6]->setText(identity.idType == QLatin1String("STD") ? tr("11-bit (standard)") : identity.idType);
    identity_[7]->setText(identity.mode04Available ? tr("available") : tr("disabled in this firmware"));
}

// ============================================================================ DashboardPage

DashboardPage::DashboardPage(AppContext& context, QWidget* parent)
    : Page(tr("Diagnostic dashboard"), QString(), parent),
      context_(context),
      readDtc_(new QPushButton(tr("Read DTCs"))),
      liveToggle_(new QPushButton(tr("Start live data"))) {
    readDtc_->setObjectName(QStringLiteral("primary"));
    auto* grid = new QGridLayout;
    grid->addWidget(card(tr("Gateway"), connection_, connectionDetail_), 0, 0);
    grid->addWidget(card(tr("Last DTC scan"), scan_, scanDetail_), 0, 1);
    grid->addWidget(card(tr("Live data"), live_, liveDetail_), 0, 2);
    content_->addLayout(grid);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(readDtc_);
    buttons->addWidget(liveToggle_);
    buttons->addStretch();
    content_->addLayout(buttons);
    content_->addStretch();

    connect(readDtc_, &QPushButton::clicked, this, [this] {
        context_.scans->startScan();
        emit navigate(kDtcPage);
    });
    connect(liveToggle_, &QPushButton::clicked, this, [this] {
        if (context_.live->running()) {
            context_.live->stop();
        } else {
            context_.live->start(context_.settings->value(settings_keys::kLivePeriodMs, 1000).toInt());
        }
    });
    connect(context_.client, &GatewayClient::stateChanged, this, &DashboardPage::refresh);
    connect(context_.client, &GatewayClient::identityReady, this, &DashboardPage::refresh);
    connect(context_.scans, &DtcScanService::scanFinished, this, &DashboardPage::refresh);
    connect(context_.live, &LiveDataService::parameterChanged, this, &DashboardPage::refresh);
    connect(context_.live, &LiveDataService::started, this, &DashboardPage::refresh);
    connect(context_.live, &LiveDataService::stopped, this, &DashboardPage::refresh);
    refresh();
}

void DashboardPage::refresh() {
    const bool ready = context_.client->state() == GatewayClient::State::Ready;
    const GatewayIdentity& id = context_.client->identity();
    connection_->setText(ready ? (id.isSimulation() ? tr("SIMULATION") : tr("VEHICLE")) : tr("Not connected"));
    connectionDetail_->setText(ready ? tr("%1 · fw %2 · ECU 0x%3/0x%4 · %5 bit/s")
                                           .arg(context_.connection->linkDescription(), id.firmwareVersion, id.requestId,
                                                id.responseId)
                                           .arg(id.bitrate)
                                     : context_.connection->lastError());
    readDtc_->setEnabled(ready && !context_.scans->busy());
    liveToggle_->setEnabled(ready);
    liveToggle_->setText(context_.live->running() ? tr("Stop live data") : tr("Start live data"));

    if (!context_.dtcModel->hasResult()) {
        scan_->setText(QStringLiteral("—"));
        scanDetail_->setText(tr("No scan in this session"));
    } else {
        const DtcScanResult& r = context_.dtcModel->result();
        int known = 0;
        int unknown = 0;
        for (const DtcEntry& e : r.entries) {
            if (e.status == EnrichmentStatus::Known) ++known;
            if (e.status == EnrichmentStatus::Unknown) ++unknown;
        }
        scan_->setText(r.acquired ? tr("%n stored code(s)", nullptr, static_cast<int>(r.entries.size())) : tr("Scan failed"));
        scanDetail_->setText(r.acquired ? tr("%1 known · %2 unknown · %3 · audit %4")
                                              .arg(known)
                                              .arg(unknown)
                                              .arg(r.finishedUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss")),
                                                   r.audited ? tr("saved") : tr("NOT saved"))
                                        : tr("%1/%2 · audit %3")
                                              .arg(QLatin1String(diag::toString(r.acquisitionError.stage)),
                                                   QLatin1String(diag::toString(r.acquisitionError.code)),
                                                   r.audited ? tr("saved") : tr("NOT saved")));
    }

    QStringList lines;
    int fresh = 0;
    for (const LiveParameter& p : context_.live->parameters()) {
        if (p.state == LiveParameter::State::Fresh) ++fresh;
        lines << QStringLiteral("%1: %2 %3 (%4)")
                     .arg(p.displayName, p.hasValue ? QString::number(p.value, 'f', 1) : QStringLiteral("—"), p.unit,
                          LiveDataModel::stateText(static_cast<int>(p.state)));
    }
    live_->setText(context_.live->running() ? tr("%1 of %2 fresh").arg(fresh).arg(lines.size()) : tr("Stopped"));
    liveDetail_->setText(lines.join(QLatin1Char('\n')));
}
