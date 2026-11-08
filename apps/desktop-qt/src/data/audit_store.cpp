#include "data/audit_store.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "gateway/gateway_types.hpp"

namespace {

using diag::Code;
using diag::Stage;

diag::Error auditError(Code code, std::uint32_t detail = 0) { return diag::fail(Stage::Audit, code, detail); }

// Ordered migrations. NEVER edit a migration that has shipped – append a new one instead, because
// existing databases have already applied the old text. Index + 1 = resulting user_version.
const char* const kMigrations[] = {
    // v1: sessions and DTC scan results. Raw codes are stored exactly as received; knowledge
    // enrichment is NOT stored (it can be recomputed and may change with a new knowledge DB).
    R"sql(
    CREATE TABLE sessions (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        started_at_utc   TEXT NOT NULL,
        link             TEXT NOT NULL,
        source           TEXT NOT NULL CHECK (source IN ('SIMULATION', 'VEHICLE')),
        firmware_version TEXT NOT NULL,
        build_id         TEXT NOT NULL,
        protocol_version INTEGER NOT NULL,
        request_can_id   TEXT NOT NULL,
        response_can_id  TEXT NOT NULL,
        bitrate          INTEGER NOT NULL
    );
    CREATE TABLE dtc_scans (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id      INTEGER NOT NULL REFERENCES sessions(id),
        request_id      INTEGER NOT NULL,
        started_at_utc  TEXT NOT NULL,
        finished_at_utc TEXT NOT NULL,
        source          TEXT NOT NULL,
        ecu             TEXT NOT NULL,
        outcome         TEXT NOT NULL CHECK (outcome IN ('OK', 'ERROR')),
        error_stage     TEXT,
        error_code      TEXT,
        error_detail    INTEGER,
        dtc_count       INTEGER NOT NULL,
        dtcs            TEXT NOT NULL
    );
    CREATE INDEX idx_dtc_scans_session ON dtc_scans(session_id);
    )sql",
};
constexpr int kMigrationCount = static_cast<int>(sizeof(kMigrations) / sizeof(kMigrations[0]));

/// Executes a multi-statement migration (QSqlQuery runs one statement at a time).
bool execScript(QSqlDatabase& db, const QString& script) {
    const QStringList statements = script.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& statement : statements) {
        if (statement.trimmed().isEmpty()) continue;
        QSqlQuery query(db);
        if (!query.exec(statement)) return false;
    }
    return true;
}

QString isoUtc(const QDateTime& time) { return time.toUTC().toString(Qt::ISODateWithMs); }

/// Qt binds a null QString as SQL NULL; columns like `ecu` are NOT NULL and an absent value
/// (e.g. no responder in a failed scan) must be stored as '' – it is still a valid record.
QString text(const QString& value) { return value.isNull() ? QStringLiteral("") : value; }

}  // namespace

int AuditStore::latestSchemaVersion() { return kMigrationCount; }

AuditStore::AuditStore(QString databasePath)
    : path_(std::move(databasePath)),
      connectionName_(QStringLiteral("audit-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

AuditStore::~AuditStore() {
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName_, false);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

int AuditStore::schemaVersion() const {
    QSqlQuery query(QSqlDatabase::database(connectionName_, false));
    return query.exec(QStringLiteral("PRAGMA user_version")) && query.next() ? query.value(0).toInt() : -1;
}

diag::Error AuditStore::open() {
    open_ = false;
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) return auditError(Code::StorageFailure, 1);
    QSqlDatabase db = QSqlDatabase::contains(connectionName_) ? QSqlDatabase::database(connectionName_, false)
                                                              : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db.setDatabaseName(path_);
    if (!db.open()) return auditError(Code::StorageFailure, 2);

    QSqlQuery pragma(db);
    // Foreign keys are off by default in SQLite; enable so a scan cannot reference a missing session.
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) return auditError(Code::StorageFailure, 3);

    const int current = schemaVersion();
    if (current < 0) return auditError(Code::StorageFailure, 4);  // not a database / unreadable
    if (current > kMigrationCount) {
        db.close();
        return auditError(Code::InvalidState, static_cast<std::uint32_t>(current));
    }
    for (int version = current; version < kMigrationCount; ++version) {
        if (!db.transaction()) return auditError(Code::StorageFailure, 5);
        const bool applied = execScript(db, QString::fromLatin1(kMigrations[version])) &&
                             QSqlQuery(db).exec(QStringLiteral("PRAGMA user_version = %1").arg(version + 1));
        if (!applied || !db.commit()) {
            db.rollback();
            return auditError(Code::StorageFailure, static_cast<std::uint32_t>(100 + version + 1));
        }
    }
    open_ = true;
    return diag::Error{};
}

diag::Error AuditStore::beginSession(const GatewayIdentity& identity, const QString& link, qint64& sessionId) {
    if (!open_) return auditError(Code::InvalidState);
    QSqlQuery query(QSqlDatabase::database(connectionName_, false));
    query.prepare(QStringLiteral(
        "INSERT INTO sessions (started_at_utc, link, source, firmware_version, build_id, protocol_version, "
        "request_can_id, response_can_id, bitrate) VALUES (:t, :link, :source, :fw, :build, :proto, :tx, :rx, :bitrate)"));
    query.bindValue(QStringLiteral(":t"), isoUtc(QDateTime::currentDateTimeUtc()));
    query.bindValue(QStringLiteral(":link"), text(link));
    query.bindValue(QStringLiteral(":source"), identity.source);
    query.bindValue(QStringLiteral(":fw"), text(identity.firmwareVersion));
    query.bindValue(QStringLiteral(":build"), text(identity.buildId));
    query.bindValue(QStringLiteral(":proto"), identity.protocolVersion);
    query.bindValue(QStringLiteral(":tx"), text(identity.requestId));
    query.bindValue(QStringLiteral(":rx"), text(identity.responseId));
    query.bindValue(QStringLiteral(":bitrate"), identity.bitrate);
    if (!query.exec()) return auditError(Code::StorageFailure);
    sessionId = query.lastInsertId().toLongLong();
    return diag::Error{};
}

diag::Error AuditStore::recordScan(const ScanRecord& record) {
    if (!open_) return auditError(Code::InvalidState);
    QSqlQuery query(QSqlDatabase::database(connectionName_, false));
    query.prepare(QStringLiteral(
        "INSERT INTO dtc_scans (session_id, request_id, started_at_utc, finished_at_utc, source, ecu, outcome, "
        "error_stage, error_code, error_detail, dtc_count, dtcs) VALUES "
        "(:session, :req, :start, :end, :source, :ecu, :outcome, :stage, :code, :detail, :count, :dtcs)"));
    query.bindValue(QStringLiteral(":session"), record.sessionId);
    query.bindValue(QStringLiteral(":req"), record.requestId);
    query.bindValue(QStringLiteral(":start"), isoUtc(record.startedUtc));
    query.bindValue(QStringLiteral(":end"), isoUtc(record.finishedUtc));
    query.bindValue(QStringLiteral(":source"), text(record.source));
    query.bindValue(QStringLiteral(":ecu"), text(record.ecu));
    query.bindValue(QStringLiteral(":outcome"), record.ok ? QStringLiteral("OK") : QStringLiteral("ERROR"));
    const QVariant null(QMetaType::fromType<QString>());
    query.bindValue(QStringLiteral(":stage"), record.ok ? null : QVariant(QString::fromLatin1(diag::toString(record.error.stage))));
    query.bindValue(QStringLiteral(":code"), record.ok ? null : QVariant(QString::fromLatin1(diag::toString(record.error.code))));
    query.bindValue(QStringLiteral(":detail"), record.ok ? QVariant(QMetaType::fromType<qint64>()) : QVariant(record.error.detail));
    query.bindValue(QStringLiteral(":count"), static_cast<int>(record.codes.size()));
    query.bindValue(QStringLiteral(":dtcs"), text(record.codes.join(QLatin1Char(','))));  // empty list → ''
    if (!query.exec()) return auditError(Code::StorageFailure);
    return diag::Error{};
}

QList<ScanRecord> AuditStore::recentScans(int limit) const {
    QList<ScanRecord> records;
    if (!open_) return records;
    QSqlQuery query(QSqlDatabase::database(connectionName_, false));
    query.prepare(QStringLiteral(
        "SELECT session_id, request_id, started_at_utc, finished_at_utc, source, ecu, outcome, error_stage, "
        "error_code, error_detail, dtcs FROM dtc_scans ORDER BY id DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec()) return records;
    while (query.next()) {
        ScanRecord r;
        r.sessionId = query.value(0).toLongLong();
        r.requestId = query.value(1).toUInt();
        r.startedUtc = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        r.finishedUtc = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        r.source = query.value(4).toString();
        r.ecu = query.value(5).toString();
        r.ok = query.value(6).toString() == QLatin1String("OK");
        if (!r.ok) {
            const QByteArray stage = query.value(7).toString().toLatin1();
            const QByteArray code = query.value(8).toString().toLatin1();
            diag::stageFromString(stage.constData(), r.error.stage);
            diag::codeFromString(code.constData(), r.error.code);
            r.error.detail = query.value(9).toUInt();
        }
        r.codes = query.value(10).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        records.append(r);
    }
    return records;
}
