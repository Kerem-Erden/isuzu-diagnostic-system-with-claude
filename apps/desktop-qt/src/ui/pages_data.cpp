// LiveDataPage, DtcPage and HistoryPage.
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "data/audit_store.hpp"
#include "gateway/gateway_client.hpp"
#include "models/view_models.hpp"
#include "services/dtc_scan_service.hpp"
#include "services/live_data_service.hpp"
#include "ui/pages.hpp"

namespace {

QTableView* makeTable(QAbstractItemModel* model) {
    auto* table = new QTableView;
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setAlternatingRowColors(false);
    return table;
}

QString errorText(const diag::Error& e) {
    return QStringLiteral("%1/%2/%3")
        .arg(QLatin1String(diag::toString(e.stage)), QLatin1String(diag::toString(e.code)))
        .arg(e.detail, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString htmlList(const QStringList& items, bool ordered) {
    if (items.isEmpty()) return QStringLiteral("<p><i>%1</i></p>").arg(QObject::tr("none in the knowledge base"));
    QString html = ordered ? QStringLiteral("<ol>") : QStringLiteral("<ul>");
    for (const QString& item : items) html += QStringLiteral("<li>%1</li>").arg(item.toHtmlEscaped());
    return html + (ordered ? QStringLiteral("</ol>") : QStringLiteral("</ul>"));
}

}  // namespace

// ============================================================================ LiveDataPage

LiveDataPage::LiveDataPage(AppContext& context, QWidget* parent)
    : Page(tr("Live data (OBD Mode 01)"),
           tr("Values are polled by the gateway in a bounded round robin. A value that is not fresh is shown grey "
              "and is not current."),
           parent),
      context_(context),
      toggle_(new QPushButton(tr("Start"))),
      status_(new QLabel),
      table_(makeTable(context.liveModel)) {
    toggle_->setObjectName(QStringLiteral("primary"));
    auto* row = new QHBoxLayout;
    row->addWidget(toggle_);
    row->addWidget(status_, 1);
    content_->addLayout(row);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->horizontalHeader()->setSectionResizeMode(LiveDataModel::Name, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(LiveDataModel::State, QHeaderView::ResizeToContents);
    content_->addWidget(table_, 1);

    connect(toggle_, &QPushButton::clicked, this, [this] {
        if (context_.live->running()) context_.live->stop();
        else context_.live->start(context_.settings->value(settings_keys::kLivePeriodMs, 1000).toInt());
    });
    connect(context_.live, &LiveDataService::started, this, [this](int period) {
        status_->setText(tr("Polling 5 PIDs, full cycle %1 ms").arg(period));
        updateButtons();
    });
    connect(context_.live, &LiveDataService::stopped, this, [this](const QString& reason) {
        status_->setText(tr("Stopped: %1").arg(reason));
        updateButtons();
    });
    connect(context_.live, &LiveDataService::startFailed, this, [this](const diag::Error& error) {
        status_->setText(tr("Could not start: %1").arg(errorText(error)));
        updateButtons();
    });
    connect(context_.client, &GatewayClient::stateChanged, this, &LiveDataPage::updateButtons);
    updateButtons();
}

void LiveDataPage::updateButtons() {
    toggle_->setEnabled(context_.client->state() == GatewayClient::State::Ready);
    toggle_->setText(context_.live->running() ? tr("Stop") : tr("Start"));
}

// ============================================================================ DtcPage

DtcPage::DtcPage(AppContext& context, QWidget* parent)
    : Page(tr("Diagnostic trouble codes (OBD Mode 03)"),
           tr("Stored DTCs as reported by the ECU. Descriptions come from the offline Isuzu knowledge base and are "
              "possible causes, not a definitive diagnosis."),
           parent),
      context_(context),
      read_(new QPushButton(tr("Read DTCs"))),
      summary_(new QLabel(tr("No scan yet."))),
      table_(makeTable(context.dtcModel)),
      detail_(new QTextBrowser) {
    read_->setObjectName(QStringLiteral("primary"));
    summary_->setWordWrap(true);
    auto* row = new QHBoxLayout;
    row->addWidget(read_);
    row->addWidget(summary_, 1);
    content_->addLayout(row);
    // The description gets the free width; code, status and system fit their content.
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(DtcListModel::Code, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(DtcListModel::Status, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(DtcListModel::Title, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(DtcListModel::System, QHeaderView::ResizeToContents);
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(table_);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    content_->addWidget(splitter, 1);
    detail_->setOpenExternalLinks(false);

    connect(read_, &QPushButton::clicked, context_.scans, &DtcScanService::startScan);
    connect(read_, &QPushButton::clicked, this, [this] {
        read_->setEnabled(false);
        summary_->setText(tr("Reading stored DTCs …"));
    });
    connect(context_.scans, &DtcScanService::scanFinished, this, &DtcPage::showScan);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) { showDetail(current.row()); });
    connect(context_.client, &GatewayClient::stateChanged, this,
            [this](GatewayClient::State state) { read_->setEnabled(state == GatewayClient::State::Ready); });
    read_->setEnabled(false);
}

void DtcPage::showScan(const DtcScanResult& r) {
    read_->setEnabled(context_.client->state() == GatewayClient::State::Ready);
    // Acquisition, enrichment and audit outcomes are stated separately (old defect D3).
    const QString when = r.finishedUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString audit = r.audited ? tr("audit saved") : tr("audit NOT saved (%1)").arg(errorText(r.auditError));
    if (!r.acquired) {
        summary_->setText(tr("<b>Scan failed</b> at %1 — %2 · request #%3 · %4 · %5")
                              .arg(errorText(r.acquisitionError), r.source)
                              .arg(r.requestId)
                              .arg(when, audit));
    } else {
        summary_->setText(tr("<b>%1 stored code(s)</b> from ECU 0x%2 · %3 · %4 · request #%5 · %6")
                              .arg(r.entries.size())
                              .arg(r.ecu, r.source, when)
                              .arg(r.requestId)
                              .arg(audit));
    }
    if (r.acquired && r.entries.isEmpty()) {
        detail_->setHtml(tr("<p>The ECU reported <b>no stored DTCs</b>.</p>"));
    } else if (!r.entries.isEmpty()) {
        table_->selectRow(0);
        showDetail(0);
    } else {
        detail_->clear();
    }
}

void DtcPage::showDetail(int row) {
    const DtcEntry* e = context_.dtcModel->entry(row);
    if (e == nullptr) return;
    QString html = QStringLiteral("<h2 style='color:#A5001E'>%1</h2>").arg(e->code.toHtmlEscaped());
    switch (e->status) {
        case EnrichmentStatus::Unknown:
            html += tr("<p><b>Unknown DTC.</b> The code is valid and was reported by the ECU, but it is not in the "
                       "knowledge database. Consult the service manual for this vehicle.</p>");
            detail_->setHtml(html);
            return;
        case EnrichmentStatus::KnowledgeUnavailable:
            html += tr("<p><b>Knowledge unavailable</b> (%1). The raw code above is the ECU result.</p>")
                        .arg(errorText(e->knowledgeError));
            detail_->setHtml(html);
            return;
        case EnrichmentStatus::Known:
            break;
    }
    const knowledge::DtcKnowledge& k = e->knowledge;
    html += QStringLiteral("<h3>%1</h3><p style='color:#68707C'>%2 · profile %3</p>")
                .arg(k.title.toHtmlEscaped(), k.system.toHtmlEscaped(),
                     (k.profileKey.isEmpty() ? tr("—") : k.profileKey).toHtmlEscaped());
    if (!k.description.isEmpty()) html += QStringLiteral("<p>%1</p>").arg(k.description.toHtmlEscaped());
    html += tr("<h4>Possible causes</h4>") + htmlList(k.possibleCauses, false);
    html += tr("<h4>Diagnostic steps</h4>") + htmlList(k.diagnosisSteps, true);
    if (!k.confirmationSteps.isEmpty()) html += tr("<h4>Confirmation</h4>") + htmlList(k.confirmationSteps, true);
    if (!k.solutions.isEmpty()) html += tr("<h4>Recommended actions</h4>") + htmlList(k.solutions, false);
    QStringList related;
    for (const knowledge::RelatedLiveParameter& p : k.relatedLiveData) {
        related << (p.unit.isEmpty() ? p.displayName : QStringLiteral("%1 [%2]").arg(p.displayName, p.unit));
    }
    html += tr("<h4>Related live data</h4>") + htmlList(related, false);
    detail_->setHtml(html);
}

// ============================================================================ HistoryPage

HistoryPage::HistoryPage(AppContext& context, QWidget* parent)
    : Page(tr("Diagnostic history"), tr("DTC scans recorded in the local audit database (newest first)."), parent),
      context_(context),
      status_(new QLabel),
      table_(new QTableWidget) {
    auto* reloadButton = new QPushButton(tr("Reload"));
    auto* row = new QHBoxLayout;
    row->addWidget(reloadButton);
    row->addWidget(status_, 1);
    content_->addLayout(row);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({tr("Time"), tr("Source"), tr("ECU"), tr("Outcome"), tr("Codes"), tr("Request"), tr("Session")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    content_->addWidget(table_, 1);
    connect(reloadButton, &QPushButton::clicked, this, &HistoryPage::reload);
    connect(context_.scans, &DtcScanService::scanFinished, this, [this] {
        if (isVisible()) reload();
    });
}

void HistoryPage::showEvent(QShowEvent* event) {
    Page::showEvent(event);
    reload();
}

void HistoryPage::reload() {
    if (!context_.audit->isOpen()) {
        status_->setText(tr("Audit database unavailable – no history is recorded."));
        table_->setRowCount(0);
        return;
    }
    const QList<ScanRecord> scans = context_.audit->recentScans(200);
    status_->setText(tr("%1 · showing %2 most recent scans").arg(context_.audit->databasePath()).arg(scans.size()));
    table_->setRowCount(static_cast<int>(scans.size()));
    for (int i = 0; i < scans.size(); ++i) {
        const ScanRecord& s = scans[i];
        const QString cells[7] = {
            s.finishedUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            s.source,
            s.ecu.isEmpty() ? QStringLiteral("—") : s.ecu,
            s.ok ? tr("OK") : errorText(s.error),
            s.ok ? (s.codes.isEmpty() ? tr("none stored") : s.codes.join(QStringLiteral(", "))) : QString(),
            QString::number(s.requestId),
            QString::number(s.sessionId)};
        for (int c = 0; c < 7; ++c) table_->setItem(i, c, new QTableWidgetItem(cells[c]));
    }
    table_->resizeColumnsToContents();
}
