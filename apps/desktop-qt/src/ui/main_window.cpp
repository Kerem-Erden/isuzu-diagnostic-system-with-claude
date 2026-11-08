#include "ui/main_window.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "gateway/gateway_client.hpp"
#include "transport/simulation_transport.hpp"

namespace {
constexpr int kMaxLogLines = 500;  // bounded: the developer screen must never grow without limit
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      transport_(new SimulationTransport(this)),
      client_(new GatewayClient(transport_, this)),
      banner_(new QLabel(tr("NOT CONNECTED"))),
      details_(new QLabel),
      connectButton_(new QPushButton(tr("Connect (simulation)"))),
      readDtcButton_(new QPushButton(tr("Read DTCs"))),
      liveButton_(new QPushButton(tr("Start live data"))),
      log_(new QListWidget) {
    setWindowTitle(tr("Isuzu Diagnostic System"));
    resize(900, 600);

    banner_->setAlignment(Qt::AlignCenter);
    banner_->setStyleSheet(QStringLiteral("font-weight:600; padding:8px; background:#E8EAED; color:#30343B;"));
    details_->setStyleSheet(QStringLiteral("color:#68707C;"));

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(connectButton_);
    buttons->addWidget(readDtcButton_);
    buttons->addWidget(liveButton_);
    buttons->addStretch();

    auto* layout = new QVBoxLayout;
    layout->addWidget(banner_);
    layout->addWidget(details_);
    layout->addLayout(buttons);
    layout->addWidget(log_, 1);
    auto* central = new QWidget;
    central->setLayout(layout);
    setCentralWidget(central);
    setConnectedUi(false);

    connect(connectButton_, &QPushButton::clicked, this, [this] {
        if (client_->state() == GatewayClient::State::Disconnected) client_->connectToGateway();
        else client_->disconnectFromGateway();
    });
    connect(readDtcButton_, &QPushButton::clicked, this, [this] { client_->send(QStringLiteral("READ_DTC")); });
    connect(liveButton_, &QPushButton::clicked, this, [this] {
        client_->send(liveRunning_ ? QStringLiteral("LIVE_STOP") : QStringLiteral("LIVE_START"),
                      liveRunning_ ? QStringList{} : QStringList{QStringLiteral("1000")});
    });
    connect(client_, &GatewayClient::identityReady, this, &MainWindow::onIdentity);
    connect(client_, &GatewayClient::replyReceived, this, &MainWindow::onReply);
    connect(client_, &GatewayClient::eventReceived, this, &MainWindow::onEvent);
    connect(client_, &GatewayClient::connectionLost, this, &MainWindow::onDisconnected);
    connect(client_, &GatewayClient::connectionFailed, this, [this](const QString& reason) { log(tr("Connection failed: %1").arg(reason)); });
    connect(client_, &GatewayClient::protocolWarning, this, [this](const QString& text) { log(tr("Protocol: %1").arg(text)); });
}

void MainWindow::setConnectedUi(bool connected) {
    connectButton_->setText(connected ? tr("Disconnect") : tr("Connect (simulation)"));
    readDtcButton_->setEnabled(connected);
    liveButton_->setEnabled(connected);
    if (!connected) {
        liveRunning_ = false;
        liveButton_->setText(tr("Start live data"));
    }
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
}

void MainWindow::onReply(const GatewayReply& reply) {
    if (reply.command == QLatin1String("PING") && reply.ok) return;  // keep-alive noise
    if (reply.command == QLatin1String("LIVE_START") && reply.ok) {
        liveRunning_ = true;
        liveButton_->setText(tr("Stop live data"));
    } else if (reply.command == QLatin1String("LIVE_STOP") && reply.ok) {
        liveRunning_ = false;
        liveButton_->setText(tr("Start live data"));
    }
    if (reply.command == QLatin1String("READ_DTC") && reply.ok) {
        const QString codes = reply.value(QStringLiteral("dtcs"));
        log(tr("DTCs (%1) from ECU %2: %3")
                .arg(reply.value(QStringLiteral("count")), reply.value(QStringLiteral("ecu")),
                     codes.isEmpty() ? tr("none stored") : codes));
        return;
    }
    log(tr("%1 #%2 → %3").arg(reply.command).arg(reply.id).arg(reply.ok ? tr("OK") : reply.errorText()));
}

void MainWindow::onEvent(const GatewayEvent& event) {
    if (event.kind == QLatin1String("LIVE")) {
        log(tr("LIVE %1 = %2 %3").arg(event.fields.value(0), event.fields.value(1), event.fields.value(2)));
    } else if (event.kind != QLatin1String("DTC_PART")) {
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
