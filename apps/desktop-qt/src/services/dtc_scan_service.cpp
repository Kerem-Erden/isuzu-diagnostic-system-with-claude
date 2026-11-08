#include "services/dtc_scan_service.hpp"

#include "gateway/gateway_client.hpp"

namespace {
constexpr int kScanTimeoutMs = 8000;  // > gateway P2 (1 s) + P2* chain for NRC 0x78 + link margin
}

DtcScanService::DtcScanService(GatewayClient* client, const knowledge::KnowledgeRepository* knowledge,
                               AuditStore* audit, QObject* parent)
    : QObject(parent), client_(client), knowledge_(knowledge), audit_(audit) {
    qRegisterMetaType<DtcScanResult>();
    connect(client_, &GatewayClient::replyReceived, this, &DtcScanService::onReply);
}

void DtcScanService::startScan() {
    if (busy()) {
        DtcScanResult result;
        result.acquisitionError = diag::fail(diag::Stage::Internal, diag::Code::Busy);
        QMetaObject::invokeMethod(this, [this, result] { emit scanFinished(result); }, Qt::QueuedConnection);
        return;
    }
    startedUtc_ = QDateTime::currentDateTimeUtc();
    pendingId_ = client_->send(QStringLiteral("READ_DTC"), {}, kScanTimeoutMs);
}

void DtcScanService::onReply(const GatewayReply& reply) {
    if (reply.id != pendingId_ || pendingId_ == 0) return;
    pendingId_ = 0;

    DtcScanResult result;
    result.requestId = reply.id;
    result.source = client_->identity().source;
    result.startedUtc = startedUtc_;
    result.finishedUtc = QDateTime::currentDateTimeUtc();
    result.acquired = reply.ok;
    result.acquisitionError = reply.error;
    result.ecu = reply.value(QStringLiteral("ecu"));
    if (reply.ok) {
        const QStringList codes = reply.value(QStringLiteral("dtcs")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& code : codes) result.entries.append(DtcEntry{code, EnrichmentStatus::KnowledgeUnavailable, {}, {}});
        enrich(result);
    }
    audit(result);
    emit scanFinished(result);
}

void DtcScanService::enrich(DtcScanResult& result) const {
    for (DtcEntry& entry : result.entries) {
        if (knowledge_ == nullptr) {
            entry.knowledgeError = diag::fail(diag::Stage::Database, diag::Code::NotFound);
            continue;
        }
        const knowledge::LookupResult lookup = knowledge_->lookup(entry.code, applicationKey_);
        switch (lookup.status) {
            case knowledge::LookupStatus::Known:
                entry.status = EnrichmentStatus::Known;
                entry.knowledge = lookup.knowledge;
                break;
            case knowledge::LookupStatus::Unknown:
                entry.status = EnrichmentStatus::Unknown;  // valid code, shown as "Unknown DTC"
                break;
            case knowledge::LookupStatus::Failed:
                entry.status = EnrichmentStatus::KnowledgeUnavailable;
                entry.knowledgeError = lookup.error;
                break;
        }
    }
}

void DtcScanService::audit(DtcScanResult& result) {
    if (audit_ == nullptr || !audit_->isOpen() || sessionId_ == 0) {
        result.auditError = diag::fail(diag::Stage::Audit, diag::Code::InvalidState);
        return;
    }
    ScanRecord record;
    record.sessionId = sessionId_;
    record.requestId = result.requestId;
    record.startedUtc = result.startedUtc;
    record.finishedUtc = result.finishedUtc;
    record.source = result.source;
    record.ecu = result.ecu;
    record.ok = result.acquired;
    record.error = result.acquisitionError;
    for (const DtcEntry& entry : result.entries) record.codes.append(entry.code);
    result.auditError = audit_->recordScan(record);
    result.audited = result.auditError.isOk();
}
