#include "knowledge/knowledge_repository.hpp"

#include <algorithm>
#include <utility>

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "diag/dtc.hpp"

namespace knowledge {
namespace {

using diag::Code;
using diag::Stage;

// Tables the lookup needs. Validation at open() turns a broken/foreign file into one clear error
// instead of SQL errors on every lookup.
const QStringList kRequiredTables = {
    QStringLiteral("kb_metadata"),          QStringLiteral("dtc_codes"),
    QStringLiteral("dtc_profiles"),         QStringLiteral("dtc_causes"),
    QStringLiteral("dtc_solution_recommendations"), QStringLiteral("dtc_procedure_steps"),
    QStringLiteral("dtc_related_live_data"), QStringLiteral("live_parameters"),
    QStringLiteral("diagnostic_applications"),
};

diag::Error dbError(Code code, std::uint32_t detail = 0) { return diag::fail(Stage::Database, code, detail); }

/// Collapses whitespace from PDF-extracted text ("Fuel   rail\n pressure" → "Fuel rail pressure").
QString clean(const QVariant& value) { return value.isNull() ? QString() : value.toString().simplified(); }

/// Qt binds a *null* QString as SQL NULL, and `NULL = ''` is not true in SQL. The queries use ''
/// for "no application selected", so always bind a non-null (possibly empty) string.
QString sqlText(const QString& text) { return text.isNull() ? QStringLiteral("") : text; }

// Preference order: profile of the selected application, then profiles with the most OEM procedure
// steps, then with a description, then with the most causes (same ranking as the reference app).
constexpr const char* kBestProfileSql = R"sql(
    SELECT p.id, p.profile_key, p.description, a.application_key
    FROM dtc_profiles p
    LEFT JOIN diagnostic_applications a ON a.id = p.diagnostic_application_id
    WHERE p.dtc_code_id = :dtc
    ORDER BY
        CASE WHEN :app <> '' AND a.application_key = :app THEN 0 ELSE 1 END,
        (SELECT COUNT(*) FROM dtc_procedure_steps s WHERE s.dtc_profile_id = p.id) DESC,
        CASE WHEN p.description IS NOT NULL AND LENGTH(TRIM(p.description)) > 0 THEN 0 ELSE 1 END,
        (SELECT COUNT(*) FROM dtc_causes c WHERE c.dtc_profile_id = p.id) DESC,
        p.id
    LIMIT 1)sql";

// Causes / solutions are collected across all applicable profiles of the code: one manual may hold
// the procedure, another the suspected-cause table. Matching application first, generic second.
constexpr const char* kDtcLevelSql = R"sql(
    SELECT x.description,
           MIN(CASE WHEN :app <> '' AND a.application_key = :app THEN 0
                    WHEN p.diagnostic_application_id IS NULL THEN 1 ELSE 2 END) AS rank_,
           MIN(x.sort_order) AS order_
    FROM %1 x
    JOIN dtc_profiles p ON p.id = x.dtc_profile_id
    LEFT JOIN diagnostic_applications a ON a.id = p.diagnostic_application_id
    WHERE p.dtc_code_id = :dtc
      AND (:app = '' OR p.diagnostic_application_id IS NULL OR a.application_key = :app)
    GROUP BY x.description
    ORDER BY rank_, order_, x.description)sql";

constexpr const char* kStepsSql = R"sql(
    SELECT instruction FROM dtc_procedure_steps
    WHERE dtc_profile_id = :profile AND procedure_type = :type
    ORDER BY step_order)sql";

constexpr const char* kRelatedSql = R"sql(
    SELECT lp.parameter_key, lp.display_name, lp.default_unit, r.relation_role, r.reason
    FROM dtc_related_live_data r
    JOIN live_parameters lp ON lp.id = r.live_parameter_id
    LEFT JOIN diagnostic_applications a ON a.id = r.diagnostic_application_id
    WHERE r.dtc_code_id = :dtc
      AND (r.diagnostic_application_id IS NULL OR :app = '' OR a.application_key = :app)
    ORDER BY r.display_priority, lp.display_name)sql";

bool readStrings(QSqlQuery& query, QStringList& out) {
    if (!query.exec()) return false;
    while (query.next()) {
        const QString text = clean(query.value(0));
        if (!text.isEmpty() && !out.contains(text)) out.append(text);
    }
    return true;
}

}  // namespace

KnowledgeRepository::KnowledgeRepository(QString databasePath)
    : path_(std::move(databasePath)),
      connectionName_(QStringLiteral("knowledge-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

KnowledgeRepository::~KnowledgeRepository() {
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName_, false);
        if (db.isOpen()) db.close();
    }  // the handle must be gone before removeDatabase()
    QSqlDatabase::removeDatabase(connectionName_);
}

diag::Error KnowledgeRepository::open() {
    open_ = false;
    const QFileInfo file(path_);
    if (!file.exists() || !file.isFile()) return openError_ = dbError(Code::NotFound);

    QSqlDatabase db = QSqlDatabase::contains(connectionName_) ? QSqlDatabase::database(connectionName_, false)
                                                              : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db.setDatabaseName(path_);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) return openError_ = dbError(Code::StorageFailure);

    // db.tables() fails/returns nothing for a non-SQLite file; treat that as a storage failure.
    const QStringList tables = db.tables();
    if (tables.isEmpty()) {
        db.close();
        return openError_ = dbError(Code::StorageFailure, 1);
    }
    for (int i = 0; i < kRequiredTables.size(); ++i) {
        if (!tables.contains(kRequiredTables[i])) {
            db.close();
            return openError_ = dbError(Code::InvalidState, static_cast<std::uint32_t>(i + 1));
        }
    }
    QSqlQuery meta(db);
    meta.prepare(QStringLiteral("SELECT key, value FROM kb_metadata WHERE key IN ('schema_version','dataset_version')"));
    int schemaVersion = 0;
    if (meta.exec()) {
        while (meta.next()) {
            if (meta.value(0).toString() == QLatin1String("schema_version")) schemaVersion = meta.value(1).toInt();
            else datasetVersion_ = meta.value(1).toString();
        }
    }
    if (schemaVersion != kSupportedSchemaVersion) {
        db.close();
        return openError_ = dbError(Code::InvalidState, static_cast<std::uint32_t>(schemaVersion));
    }
    open_ = true;
    return openError_ = diag::Error{};
}

LookupResult KnowledgeRepository::lookup(const QString& code, const QString& applicationKey) const {
    LookupResult result;
    const QString normalized = code.trimmed().toUpper();
    result.knowledge.code = normalized;
    diag::Dtc parsed;
    const QByteArray bytes = normalized.toLatin1();
    if (!diag::Dtc::fromText(bytes.constData(), parsed)) {
        result.error = dbError(Code::InvalidArgument);
        return result;
    }
    if (!open_) {
        result.error = openError_.isOk() ? dbError(Code::InvalidState) : openError_;
        return result;
    }
    const QSqlDatabase db = QSqlDatabase::database(connectionName_, false);
    const QString app = sqlText(applicationKey.trimmed());

    QSqlQuery header(db);
    header.prepare(QStringLiteral("SELECT id, code, title, system FROM dtc_codes WHERE code = :code LIMIT 1"));
    header.bindValue(QStringLiteral(":code"), normalized);
    if (!header.exec()) {
        result.error = dbError(Code::StorageFailure);
        return result;
    }
    if (!header.next()) {
        result.status = LookupStatus::Unknown;  // valid code, not in the knowledge base
        return result;
    }
    const qint64 dtcId = header.value(0).toLongLong();
    DtcKnowledge& k = result.knowledge;
    k.code = header.value(1).toString();
    k.title = clean(header.value(2));
    k.system = clean(header.value(3));

    QSqlQuery profile(db);
    profile.prepare(QString::fromLatin1(kBestProfileSql));
    profile.bindValue(QStringLiteral(":dtc"), dtcId);
    profile.bindValue(QStringLiteral(":app"), app);
    if (!profile.exec()) {
        result.error = dbError(Code::StorageFailure, 2);
        return result;
    }
    qint64 profileId = -1;
    if (profile.next()) {
        profileId = profile.value(0).toLongLong();
        k.profileKey = profile.value(1).toString();
        k.description = clean(profile.value(2));
        k.applicationKey = clean(profile.value(3));
    }

    for (const auto& [table, target] : {std::pair{QStringLiteral("dtc_causes"), &k.possibleCauses},
                                        std::pair{QStringLiteral("dtc_solution_recommendations"), &k.solutions}}) {
        QSqlQuery query(db);
        query.prepare(QString::fromLatin1(kDtcLevelSql).arg(table));
        query.bindValue(QStringLiteral(":dtc"), dtcId);
        query.bindValue(QStringLiteral(":app"), app);
        if (!readStrings(query, *target)) {
            result.error = dbError(Code::StorageFailure, 3);
            return result;
        }
        if (target->isEmpty() && !app.isEmpty()) {
            // Never hide global knowledge because the selected application has none of its own.
            query.bindValue(QStringLiteral(":app"), sqlText(QString()));
            if (!readStrings(query, *target)) {
                result.error = dbError(Code::StorageFailure, 4);
                return result;
            }
        }
    }

    if (profileId >= 0) {
        for (const auto& [type, target] : {std::pair{QStringLiteral("DIAGNOSIS"), &k.diagnosisSteps},
                                           std::pair{QStringLiteral("CONFIRMATION"), &k.confirmationSteps}}) {
            QSqlQuery steps(db);
            steps.prepare(QString::fromLatin1(kStepsSql));
            steps.bindValue(QStringLiteral(":profile"), profileId);
            steps.bindValue(QStringLiteral(":type"), type);
            if (!readStrings(steps, *target)) {
                result.error = dbError(Code::StorageFailure, 5);
                return result;
            }
        }
    }

    QSqlQuery related(db);
    related.prepare(QString::fromLatin1(kRelatedSql));
    related.bindValue(QStringLiteral(":dtc"), dtcId);
    related.bindValue(QStringLiteral(":app"), app);
    if (!related.exec()) {
        result.error = dbError(Code::StorageFailure, 6);
        return result;
    }
    while (related.next()) {
        RelatedLiveParameter p{clean(related.value(0)), clean(related.value(1)), clean(related.value(2)),
                               clean(related.value(3)), clean(related.value(4))};
        const bool duplicate = std::any_of(k.relatedLiveData.cbegin(), k.relatedLiveData.cend(),
                                           [&p](const RelatedLiveParameter& e) { return e.key == p.key; });
        if (!duplicate) k.relatedLiveData.append(p);
    }
    result.status = LookupStatus::Known;
    return result;
}

QStringList KnowledgeRepository::applicationKeys() const {
    QStringList keys;
    if (!open_) return keys;
    QSqlQuery query(QSqlDatabase::database(connectionName_, false));
    if (query.exec(QStringLiteral("SELECT application_key FROM diagnostic_applications ORDER BY application_key"))) {
        while (query.next()) keys.append(query.value(0).toString());
    }
    return keys;
}

}  // namespace knowledge
