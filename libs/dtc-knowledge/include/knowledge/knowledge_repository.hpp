#pragma once
/**
 * @file knowledge_repository.hpp
 * @brief Read-only DTC knowledge lookup (data/knowledge/isuzu-knowledge.db, ADR-007).
 *
 * Rules carried over from the knowledge package ("core rule" in its README):
 *  - DTC recognition is global by code. Application / engine context only *enriches* the result
 *    (best matching profile first) and never decides whether a code is known.
 *  - A valid code that is not in the database is LookupStatus::Unknown – a normal result, not an
 *    error and never an exception.
 *  - Database problems are LookupStatus::Failed with Stage::Database, so callers can still show the
 *    raw ECU code ("knowledge unavailable") instead of losing it.
 *
 * The database is opened with QSQLITE_OPEN_READONLY: the application cannot modify it, and the
 * file's SHA-256 recorded in docs/dtc-database.md stays valid.
 *
 * Not thread-safe; use from the thread that called open() (Qt SQL connections are per thread).
 */

#include <QList>
#include <QString>
#include <QStringList>

#include "diag/error.hpp"

namespace knowledge {

struct RelatedLiveParameter {
    QString key;          ///< knowledge-base parameter key, e.g. "FUEL_RAIL_PRESSURE"
    QString displayName;
    QString unit;
    QString role;         ///< relation role from the source (may be empty)
    QString reason;
};

struct DtcKnowledge {
    QString code;
    QString title;              ///< canonical title (dtc_codes.title)
    QString system;             ///< e.g. "Fuel System"
    QString description;        ///< from the best profile; may be empty
    QString profileKey;         ///< which profile supplied description/steps
    QString applicationKey;     ///< application of that profile (empty = generic)
    QStringList possibleCauses;
    QStringList solutions;
    QStringList diagnosisSteps;
    QStringList confirmationSteps;
    QList<RelatedLiveParameter> relatedLiveData;
};

enum class LookupStatus { Known, Unknown, Failed };

struct LookupResult {
    LookupStatus status = LookupStatus::Failed;
    DtcKnowledge knowledge;     ///< valid when Known; for Unknown only `code` is set
    diag::Error error{};        ///< valid when Failed (Stage::Database)
};

class KnowledgeRepository {
public:
    /// Schema version this code understands (kb_metadata.schema_version).
    static constexpr int kSupportedSchemaVersion = 1;

    explicit KnowledgeRepository(QString databasePath);
    ~KnowledgeRepository();
    KnowledgeRepository(const KnowledgeRepository&) = delete;
    KnowledgeRepository& operator=(const KnowledgeRepository&) = delete;

    /**
     * @brief Opens read-only and validates the schema.
     * @return Ok, or Stage::Database with NotFound (no file), StorageFailure (cannot open / not a
     *         database), InvalidState (missing tables or unsupported schema version).
     */
    diag::Error open();
    bool isOpen() const { return open_; }
    const diag::Error& openError() const { return openError_; }
    QString databasePath() const { return path_; }
    /// kb_metadata.dataset_version, e.g. "v2".
    QString datasetVersion() const { return datasetVersion_; }

    /**
     * @brief Looks up one code (e.g. "P0087").
     * @param applicationKey optional diagnostic_applications.application_key used to prefer a profile.
     */
    LookupResult lookup(const QString& code, const QString& applicationKey = {}) const;

    /// All application keys (for the vehicle/ECU profile selection).
    QStringList applicationKeys() const;

private:
    QString path_;
    QString connectionName_;
    bool open_ = false;
    diag::Error openError_{};
    QString datasetVersion_;
};

}  // namespace knowledge
