#pragma once
/**
 * @file dtc_scan_service.hpp
 * @brief Mode 03 scan orchestration with three independent outcomes (brief §10, old defect D3).
 *
 * @code
 *   acquisition  – did the gateway/ECU deliver a DTC list?        (gateway reply)
 *   enrichment   – per code: Known / Unknown / KnowledgeUnavailable (knowledge DB)
 *   audit        – was the attempt persisted?                     (audit DB)
 * @endcode
 * A successful acquisition is always reported with all raw codes, even if the knowledge DB is
 * missing or the audit write fails; those failures are reported next to it, never instead of it.
 * A failed acquisition is audited too (the attempt and its error stage are evidence).
 *
 * The knowledge repository and audit store are optional (nullptr allowed) and not owned.
 */

#include <QDateTime>
#include <QList>
#include <QObject>

#include "data/audit_store.hpp"
#include "gateway/gateway_types.hpp"
#include "knowledge/knowledge_repository.hpp"

class GatewayClient;

enum class EnrichmentStatus { Known, Unknown, KnowledgeUnavailable };

struct DtcEntry {
    QString code;                       ///< raw code exactly as received from the ECU
    EnrichmentStatus status = EnrichmentStatus::KnowledgeUnavailable;
    knowledge::DtcKnowledge knowledge;  ///< filled when Known
    diag::Error knowledgeError{};       ///< set when KnowledgeUnavailable
};

struct DtcScanResult {
    // --- acquisition
    bool acquired = false;
    diag::Error acquisitionError{};
    quint32 requestId = 0;              ///< correlation id (protocol request id)
    QString source;                     ///< SIMULATION / VEHICLE from HELLO
    QString ecu;                        ///< responder id reported by the gateway
    QDateTime startedUtc;
    QDateTime finishedUtc;
    // --- enrichment (one entry per raw code, same order)
    QList<DtcEntry> entries;
    // --- audit
    bool audited = false;
    diag::Error auditError{};
};

class DtcScanService final : public QObject {
    Q_OBJECT
public:
    DtcScanService(GatewayClient* client, const knowledge::KnowledgeRepository* knowledge, AuditStore* audit,
                   QObject* parent = nullptr);

    /// Audit session for subsequent scans (from AuditStore::beginSession). 0 = no session → scans
    /// are reported as not audited (Stage::Audit / InvalidState).
    void setSessionId(qint64 sessionId) { sessionId_ = sessionId; }
    void setApplicationKey(const QString& key) { applicationKey_ = key; }

    /// Starts a scan. A second call while one is running yields an immediate Busy result (async).
    void startScan();
    bool busy() const { return pendingId_ != 0; }

signals:
    void scanFinished(const DtcScanResult& result);

private:
    void onReply(const GatewayReply& reply);
    void enrich(DtcScanResult& result) const;
    void audit(DtcScanResult& result);

    GatewayClient* client_;
    const knowledge::KnowledgeRepository* knowledge_;
    AuditStore* audit_;
    qint64 sessionId_ = 0;
    QString applicationKey_;
    quint32 pendingId_ = 0;
    QDateTime startedUtc_;
};

Q_DECLARE_METATYPE(DtcScanResult)
