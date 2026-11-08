#pragma once
/**
 * @file audit_store.hpp
 * @brief Diagnostic history / audit database (SQLite, read-write), separate from the knowledge DB.
 *
 * Schema versioning (ADR-007): `PRAGMA user_version` holds the applied migration count. open()
 * applies missing migrations in order, each in its own transaction. A database written by a newer
 * application (user_version > known) is refused rather than modified.
 *
 * Every write returns a diag::Error with Stage::Audit so that callers can report "audit failed"
 * next to a *successful* scan instead of turning the scan into a failure (old defect D3).
 *
 * Not thread-safe; one instance per thread.
 */

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "diag/error.hpp"

struct GatewayIdentity;

/// One persisted DTC acquisition attempt (successful or not).
struct ScanRecord {
    qint64 sessionId = 0;
    quint32 requestId = 0;      ///< protocol request id = correlation id of this operation
    QDateTime startedUtc;
    QDateTime finishedUtc;
    QString source;             ///< SIMULATION / VEHICLE as reported by the gateway
    QString ecu;                ///< responder id, e.g. "7E8"
    bool ok = false;
    diag::Error error{};        ///< acquisition error when !ok
    QStringList codes;          ///< raw codes exactly as received
};

class AuditStore {
public:
    /// Number of migrations this build knows; equals the expected user_version after open().
    static int latestSchemaVersion();

    explicit AuditStore(QString databasePath);
    ~AuditStore();
    AuditStore(const AuditStore&) = delete;
    AuditStore& operator=(const AuditStore&) = delete;

    /// Opens/creates the file (and its directory) and migrates. Stage::Audit errors:
    /// StorageFailure (cannot create/open/migrate), InvalidState (schema newer than this build).
    diag::Error open();
    bool isOpen() const { return open_; }
    int schemaVersion() const;
    QString databasePath() const { return path_; }

    /// Creates a session row for a verified gateway identity. @p sessionId receives the new id.
    diag::Error beginSession(const GatewayIdentity& identity, const QString& link, qint64& sessionId);
    diag::Error recordScan(const ScanRecord& record);
    /// Most recent scans first.
    QList<ScanRecord> recentScans(int limit) const;

private:
    QString path_;
    QString connectionName_;
    bool open_ = false;
};
