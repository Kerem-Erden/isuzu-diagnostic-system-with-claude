// DeveloperPage and SettingsPage.
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include "data/audit_store.hpp"
#include "diag/obd.hpp"
#include "gateway/gateway_client.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "models/view_models.hpp"
#include "services/live_data_service.hpp"
#include "ui/pages.hpp"

// ============================================================================ DeveloperPage

DeveloperPage::DeveloperPage(AppContext& context, QWidget* parent)
    : Page(tr("Developer diagnostics"),
           tr("Structured protocol, CAN and service events. The buffer is bounded; dropped entries are counted. "
              "CAN trace adds one event per diagnostic CAN frame."),
           parent),
      context_(context),
      filter_(new EventFilterModel(this)),
      category_(new QComboBox),
      text_(new QLineEdit),
      trace_(new QCheckBox(tr("CAN trace"))),
      counters_(new QLabel),
      table_(new QTableView),
      singlePid_(new QComboBox),
      singleRead_(new QPushButton(tr("Send once"))) {
    filter_->setSourceModel(context_.log);
    category_->addItem(tr("All categories"), QString());
    for (const char* name : {"CONNECTION", "PROTOCOL", "REQUEST", "EVENT", "CAN", "SCAN", "LIVE"}) {
        category_->addItem(QString::fromLatin1(name), QString::fromLatin1(name));
    }
    text_->setPlaceholderText(tr("Filter text (e.g. 7E8, NRC, TIMEOUT)"));
    auto* statusButton = new QPushButton(tr("Query STATUS"));
    auto* clearButton = new QPushButton(tr("Clear"));

    auto* exportButton = new QPushButton(tr("Export CSV…"));

    auto* row = new QHBoxLayout;
    row->addWidget(category_);
    row->addWidget(text_, 1);
    row->addWidget(trace_);
    row->addWidget(statusButton);
    row->addWidget(exportButton);
    row->addWidget(clearButton);
    content_->addLayout(row);

    // Exactly one Mode 01 request without starting live polling (vehicle-validation-plan.md §4).
    // The gateway de-duplicates request ids and never retries, so one click = one CAN request.
    std::size_t pidCount = 0;
    const diag::obd::PidDefinition* pids = diag::obd::livePids(pidCount);
    for (std::size_t i = 0; i < pidCount; ++i) {
        singlePid_->addItem(QStringLiteral("0x%1 %2").arg(pids[i].pid, 2, 16, QLatin1Char('0')).arg(QLatin1String(pids[i].name)),
                            QStringLiteral("%1").arg(pids[i].pid, 2, 16, QLatin1Char('0')).toUpper());
    }
    auto* single = new QHBoxLayout;
    single->addWidget(new QLabel(tr("Single Mode 01 request:")));
    single->addWidget(singlePid_);
    single->addWidget(singleRead_);
    single->addStretch();
    content_->addLayout(single);
    content_->addWidget(counters_);

    table_->setModel(filter_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(EventLogModel::Time, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(EventLogModel::Category, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(EventLogModel::Kind, QHeaderView::ResizeToContents);
    content_->addWidget(table_, 1);

    connect(category_, &QComboBox::currentIndexChanged, this,
            [this] { filter_->setCategory(category_->currentData().toString()); });
    connect(text_, &QLineEdit::textChanged, filter_, &EventFilterModel::setText);
    connect(trace_, &QCheckBox::toggled, this,
            [this](bool on) { context_.client->send(QStringLiteral("TRACE"), {on ? QStringLiteral("1") : QStringLiteral("0")}); });
    connect(statusButton, &QPushButton::clicked, this, [this] { context_.client->send(QStringLiteral("STATUS")); });
    connect(clearButton, &QPushButton::clicked, context_.log, &EventLogModel::clear);
    connect(singleRead_, &QPushButton::clicked, this,
            [this] { context_.client->send(QStringLiteral("READ_PID"), {singlePid_->currentData().toString()}); });
    connect(exportButton, &QPushButton::clicked, this, &DeveloperPage::exportCsv);
    connect(context_.log, &QAbstractItemModel::rowsInserted, this, [this] {
        updateCounters();
        if (table_->isVisible()) table_->scrollToBottom();
    });
    connect(context_.client, &GatewayClient::stateChanged, this, [this](GatewayClient::State state) {
        const bool ready = state == GatewayClient::State::Ready;
        trace_->setEnabled(ready);
        singleRead_->setEnabled(ready);
        if (!ready) {
            QSignalBlocker block(trace_);  // the gateway forgets TRACE on reboot/reconnect
            trace_->setChecked(false);
        }
        updateCounters();
    });
    trace_->setEnabled(false);
    singleRead_->setEnabled(false);
    updateCounters();
}

void DeveloperPage::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export developer log"),
        QStringLiteral("isuzu-trace-%1.csv").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;
    // The header makes the file self-describing evidence (who answered, which build, which link).
    const GatewayIdentity& id = context_.client->identity();
    const QStringList header = {
        QStringLiteral("Isuzu Diagnostic System developer log, exported %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)),
        QStringLiteral("link=%1 state=%2").arg(context_.client->transport()->description())
            .arg(context_.client->state() == GatewayClient::State::Ready ? QStringLiteral("ready") : QStringLiteral("not connected")),
        QStringLiteral("source=%1 fw=%2 build=%3 proto=%4 bitrate=%5 tx=%6 rx=%7")
            .arg(id.source, id.firmwareVersion, id.buildId).arg(id.protocolVersion).arg(id.bitrate).arg(id.requestId, id.responseId),
    };
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) ||
        file.write(context_.log->toCsv(header).toUtf8()) < 0) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1: %2").arg(path, file.errorString()));
        return;
    }
    context_.log->append(QStringLiteral("CONNECTION"), QStringLiteral("EXPORT"), tr("log exported to %1").arg(path));
}

void DeveloperPage::updateCounters() {
    const auto& f = context_.client->framingCounters();
    counters_->setText(tr("Serial frames: %1 valid · %2 bad CRC · %3 malformed · %4 oversized · %5 noise bytes   |   "
                          "log: %6 / %7 entries, %8 dropped")
                           .arg(f.frames)
                           .arg(f.badChecksum)
                           .arg(f.malformed)
                           .arg(f.oversized)
                           .arg(f.noiseBytes)
                           .arg(context_.log->rowCount())
                           .arg(context_.log->capacity())
                           .arg(context_.log->dropped()));
}

// ============================================================================ SettingsPage

SettingsPage::SettingsPage(AppContext& context, QWidget* parent)
    : Page(tr("Settings"), QString(), parent), context_(context), period_(new QSpinBox), stale_(new QSpinBox) {
    auto* liveFrame = new QFrame;
    liveFrame->setObjectName(QStringLiteral("card"));
    auto* liveForm = new QFormLayout(liveFrame);
    // 500 ms is the gateway minimum (one PID every 100 ms); slower is gentler on the ECU.
    period_->setRange(500, 10000);
    period_->setSingleStep(250);
    period_->setSuffix(tr(" ms"));
    period_->setValue(context_.settings->value(settings_keys::kLivePeriodMs, 1000).toInt());
    stale_->setRange(0, 60000);
    stale_->setSingleStep(500);
    stale_->setSuffix(tr(" ms"));
    stale_->setSpecialValueText(tr("automatic (3 cycles, ≥ 1.5 s)"));
    stale_->setValue(context_.settings->value(settings_keys::kStaleMs, 0).toInt());
    liveForm->addRow(tr("Live data cycle (all 5 PIDs)"), period_);
    liveForm->addRow(tr("Mark value stale after"), stale_);
    content_->addWidget(liveFrame);

    auto* dataFrame = new QFrame;
    dataFrame->setObjectName(QStringLiteral("card"));
    auto* dataForm = new QFormLayout(dataFrame);
    const knowledge::KnowledgeRepository* kb = context_.knowledge;
    dataForm->addRow(tr("Knowledge database"),
                     new QLabel(kb->isOpen() ? tr("%1 (dataset %2, read-only)").arg(kb->databasePath(), kb->datasetVersion())
                                             : tr("unavailable (%1) – %2")
                                                   .arg(QLatin1String(diag::toString(kb->openError().code)), kb->databasePath())));
    dataForm->addRow(tr("Audit database"),
                     new QLabel(context_.audit->isOpen() ? tr("%1 (schema v%2)").arg(context_.audit->databasePath()).arg(context_.audit->schemaVersion())
                                                         : tr("unavailable – %1").arg(context_.audit->databasePath())));
    content_->addWidget(dataFrame);

    auto* safetyFrame = new QFrame;
    safetyFrame->setObjectName(QStringLiteral("card"));
    auto* safety = new QVBoxLayout(safetyFrame);
    auto* safetyText = new QLabel(
        tr("<b>Safety status</b><br>OBD Mode 04 (clear DTCs) is not available in this version. Live data and DTC "
           "reading are <b>implemented but not vehicle-validated</b>. The gateway transmits on CAN only while this "
           "application keeps a session open and returns to listen-only when idle."));
    safetyText->setWordWrap(true);
    safety->addWidget(safetyText);
    content_->addWidget(safetyFrame);
    content_->addStretch();

    connect(period_, &QSpinBox::valueChanged, this,
            [this](int value) { context_.settings->setValue(settings_keys::kLivePeriodMs, value); });
    connect(stale_, &QSpinBox::valueChanged, this, [this](int value) {
        context_.settings->setValue(settings_keys::kStaleMs, value);
        context_.live->setStaleAfterMs(value);
    });
    context_.live->setStaleAfterMs(stale_->value());
}
