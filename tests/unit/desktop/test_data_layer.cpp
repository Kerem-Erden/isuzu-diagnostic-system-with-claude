// Qt Test: knowledge repository (real read-only DB + broken fixtures), audit store migrations and
// failures, and DtcScanService outcome separation over the simulated gateway. No hardware.

#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "data/audit_store.hpp"
#include "gateway/gateway_client.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "services/dtc_scan_service.hpp"
#include "transport/simulation_transport.hpp"

using knowledge::KnowledgeRepository;
using knowledge::LookupStatus;

namespace {

const QString kKnowledgeDb = QStringLiteral(ISUZU_KNOWLEDGE_DB);

QByteArray sha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

/// Executes raw SQL on a database file through a private connection.
bool runSql(const QString& path, const QStringList& statements) {
    const QString name = QStringLiteral("fixture-%1").arg(qHash(path));
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        ok = db.open();
        for (const QString& statement : statements) {
            QSqlQuery query(db);
            ok = ok && query.exec(statement);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

struct ScanRig {
    explicit ScanRig(gw::EcuScenario scenario, const KnowledgeRepository* repo, AuditStore* audit)
        : client(&transport), service(&client, repo, audit), finished(&service, &DtcScanService::scanFinished) {
        transport.setScenario(scenario);
        client.connectToGateway();
        ready = QTest::qWaitFor([this] { return client.state() == GatewayClient::State::Ready; }, 3000);
    }
    DtcScanResult scan() {
        service.startScan();
        if (!QTest::qWaitFor([this] { return finished.count() > 0; }, 5000)) return {};
        return finished.takeFirst().at(0).value<DtcScanResult>();
    }
    SimulationTransport transport;
    GatewayClient client;
    DtcScanService service;
    QSignalSpy finished;
    bool ready = false;
};

}  // namespace

class DataLayerTest : public QObject {
    Q_OBJECT
private slots:
    // ------------------------------------------------------------ knowledge repository
    void knowledgeOpensRealDatabaseReadOnly() {
        const QByteArray before = sha256(kKnowledgeDb);
        {
            KnowledgeRepository repo(kKnowledgeDb);
            QVERIFY(repo.open().isOk());
            QCOMPARE(repo.datasetVersion(), QStringLiteral("v2"));
            QVERIFY(repo.applicationKeys().contains(QStringLiteral("ISUZU_4HK1_TM_REFERENCE")));
            for (int i = 0; i < 20; ++i) repo.lookup(QStringLiteral("P0087"));
        }
        QCOMPARE(sha256(kKnowledgeDb), before);  // file untouched
        QCOMPARE(before, QByteArray("e07c21259e8afb5bf5da413db46d2db85e802292db04bdefd3331f3135669811"));
    }

    void knownCodeIsEnriched() {
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        const auto result = repo.lookup(QStringLiteral("p0087 "));  // normalised
        QCOMPARE(result.status, LookupStatus::Known);
        QCOMPARE(result.knowledge.code, QStringLiteral("P0087"));
        QCOMPARE(result.knowledge.title, QStringLiteral("Fuel Rail Pressure (FRP) Too Low"));
        QCOMPARE(result.knowledge.system, QStringLiteral("Fuel System"));
        QVERIFY(!result.knowledge.possibleCauses.isEmpty());
        QVERIFY(!result.knowledge.diagnosisSteps.isEmpty());
        // The best profile is the one with the OEM procedure (65 steps in the reference manual).
        QCOMPARE(result.knowledge.profileKey, QStringLiteral("P0087:ISUZU_4HK1_TM_REFERENCE"));
    }

    void applicationKeyPrefersMatchingProfile() {
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        const auto result = repo.lookup(QStringLiteral("P0087"), QStringLiteral("ISUZU_NR_2005_4HK1_TC_EURO3"));
        QCOMPARE(result.status, LookupStatus::Known);
        QCOMPARE(result.knowledge.applicationKey, QStringLiteral("ISUZU_NR_2005_4HK1_TC_EURO3"));
        QVERIFY(!result.knowledge.possibleCauses.isEmpty());
    }

    void validCodeMissingFromDatabaseIsUnknownNotError() {
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        const auto result = repo.lookup(QStringLiteral("P3FFF"));
        QCOMPARE(result.status, LookupStatus::Unknown);
        QVERIFY(result.error.isOk());
        QCOMPARE(result.knowledge.code, QStringLiteral("P3FFF"));
    }

    void invalidCodeIsRejected() {
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        const auto result = repo.lookup(QStringLiteral("X0087"));
        QCOMPARE(result.status, LookupStatus::Failed);
        QCOMPARE(result.error, diag::fail(diag::Stage::Database, diag::Code::InvalidArgument));
    }

    void missingFileIsNotFound() {
        KnowledgeRepository repo(QStringLiteral("does/not/exist.db"));
        QCOMPARE(repo.open(), diag::fail(diag::Stage::Database, diag::Code::NotFound));
        const auto result = repo.lookup(QStringLiteral("P0087"));
        QCOMPARE(result.status, LookupStatus::Failed);
        QCOMPARE(result.error.code, diag::Code::NotFound);
    }

    void corruptFileIsStorageFailure() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("corrupt.db"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'x'));
        file.close();
        KnowledgeRepository repo(path);
        QCOMPARE(repo.open().code, diag::Code::StorageFailure);
    }

    void foreignSchemaIsInvalidState() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("foreign.db"));
        QVERIFY(runSql(path, {QStringLiteral("CREATE TABLE kb_metadata(key TEXT, value TEXT)"),
                              QStringLiteral("INSERT INTO kb_metadata VALUES('schema_version','1')")}));
        KnowledgeRepository repo(path);
        QCOMPARE(repo.open().code, diag::Code::InvalidState);
        QVERIFY(!repo.isOpen());
    }

    // ------------------------------------------------------------ audit store
    void auditMigratesFromEmptyAndReopens() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("nested/audit.db"));
        {
            AuditStore store(path);
            QVERIFY(store.open().isOk());
            QCOMPARE(store.schemaVersion(), AuditStore::latestSchemaVersion());
        }
        AuditStore again(path);
        QVERIFY(again.open().isOk());  // idempotent
        QCOMPARE(again.schemaVersion(), AuditStore::latestSchemaVersion());
    }

    void auditRefusesNewerSchema() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("audit.db"));
        QVERIFY(runSql(path, {QStringLiteral("PRAGMA user_version = 99")}));
        AuditStore store(path);
        QCOMPARE(store.open(), diag::fail(diag::Stage::Audit, diag::Code::InvalidState, 99));
    }

    void auditRecordsAndReadsBackScans() {
        QTemporaryDir dir;
        AuditStore store(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(store.open().isOk());
        GatewayIdentity identity;
        identity.source = QStringLiteral("SIMULATION");
        identity.protocolVersion = 1;
        qint64 session = 0;
        QVERIFY(store.beginSession(identity, QStringLiteral("test"), session).isOk());
        ScanRecord failed;
        failed.sessionId = session;
        failed.requestId = 7;
        failed.source = identity.source;
        failed.startedUtc = failed.finishedUtc = QDateTime::currentDateTimeUtc();
        failed.error = diag::fail(diag::Stage::NegativeResponse, diag::Code::NegativeResponse, 0x22);
        QVERIFY(store.recordScan(failed).isOk());
        const QList<ScanRecord> scans = store.recentScans(10);
        QCOMPARE(scans.size(), 1);
        QVERIFY(!scans[0].ok);
        QCOMPARE(scans[0].error, failed.error);
        QCOMPARE(scans[0].requestId, 7u);
    }

    void auditSourceIsConstrained() {
        QTemporaryDir dir;
        AuditStore store(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(store.open().isOk());
        GatewayIdentity identity;
        identity.source = QStringLiteral("UNKNOWN");
        qint64 session = 0;
        QCOMPARE(store.beginSession(identity, QStringLiteral("x"), session).code, diag::Code::StorageFailure);
    }

    // ------------------------------------------------------------ scan service
    void scanSeparatesKnownUnknownAndAudits() {
        QTemporaryDir dir;
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        AuditStore audit(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(audit.open().isOk());
        ScanRig rig(gw::EcuScenario::Normal, &repo, &audit);
        QVERIFY(rig.ready);
        qint64 session = 0;
        QVERIFY(audit.beginSession(rig.client.identity(), rig.transport.description(), session).isOk());
        rig.service.setSessionId(session);

        const DtcScanResult result = rig.scan();
        QVERIFY(result.acquired);
        QCOMPARE(result.source, QStringLiteral("SIMULATION"));
        QCOMPARE(result.entries.size(), 3);
        QCOMPARE(result.entries[0].code, QStringLiteral("P1093"));
        QCOMPARE(result.entries[0].status, EnrichmentStatus::Known);
        QCOMPARE(result.entries[1].status, EnrichmentStatus::Known);
        QCOMPARE(result.entries[2].code, QStringLiteral("P3FFF"));
        QCOMPARE(result.entries[2].status, EnrichmentStatus::Unknown);
        QVERIFY(result.audited);
        QCOMPARE(audit.recentScans(1).value(0).codes, QStringList({"P1093", "P0087", "P3FFF"}));
    }

    void missingKnowledgeKeepsRawCodes() {
        KnowledgeRepository repo(QStringLiteral("missing.db"));
        repo.open();
        ScanRig rig(gw::EcuScenario::Normal, &repo, nullptr);
        QVERIFY(rig.ready);
        const DtcScanResult result = rig.scan();
        QVERIFY(result.acquired);  // the ECU result is NOT a failure
        QCOMPARE(result.entries.size(), 3);
        for (const DtcEntry& entry : result.entries) {
            QCOMPARE(entry.status, EnrichmentStatus::KnowledgeUnavailable);
            QCOMPARE(entry.knowledgeError.code, diag::Code::NotFound);
        }
        QVERIFY(!result.audited);
    }

    void auditFailureDoesNotHideSuccessfulScan() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("audit.db"));
        AuditStore audit(path);
        QVERIFY(audit.open().isOk());
        KnowledgeRepository repo(kKnowledgeDb);
        QVERIFY(repo.open().isOk());
        ScanRig rig(gw::EcuScenario::SingleDtc, &repo, &audit);
        QVERIFY(rig.ready);
        qint64 session = 0;
        QVERIFY(audit.beginSession(rig.client.identity(), QStringLiteral("sim"), session).isOk());
        rig.service.setSessionId(session);
        // Real storage failure: another connection removes the table behind the store's back.
        QVERIFY(runSql(path, {QStringLiteral("DROP TABLE dtc_scans")}));

        const DtcScanResult result = rig.scan();
        QVERIFY(result.acquired);
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries[0].status, EnrichmentStatus::Known);
        QVERIFY(!result.audited);
        QCOMPARE(result.auditError, diag::fail(diag::Stage::Audit, diag::Code::StorageFailure));
    }

    void failedAcquisitionIsAuditedWithStage() {
        QTemporaryDir dir;
        AuditStore audit(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(audit.open().isOk());
        ScanRig rig(gw::EcuScenario::NegativeResponse, nullptr, &audit);
        QVERIFY(rig.ready);
        qint64 session = 0;
        QVERIFY(audit.beginSession(rig.client.identity(), QStringLiteral("sim"), session).isOk());
        rig.service.setSessionId(session);
        const DtcScanResult result = rig.scan();
        QVERIFY(!result.acquired);
        QCOMPARE(result.acquisitionError.stage, diag::Stage::NegativeResponse);
        QVERIFY(result.entries.isEmpty());
        QVERIFY(result.audited);
        QCOMPARE(audit.recentScans(1).value(0).error.detail, 0x22u);
    }

    void zeroDtcsIsSuccessWithEmptyList() {
        // Regression: an empty code list joined to a *null* QString violated dtcs NOT NULL.
        QTemporaryDir dir;
        AuditStore audit(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(audit.open().isOk());
        ScanRig rig(gw::EcuScenario::NoDtcs, nullptr, &audit);
        QVERIFY(rig.ready);
        qint64 session = 0;
        QVERIFY(audit.beginSession(rig.client.identity(), QStringLiteral("sim"), session).isOk());
        rig.service.setSessionId(session);
        const DtcScanResult result = rig.scan();
        QVERIFY(result.acquired);
        QVERIFY(result.entries.isEmpty());
        QVERIFY2(result.audited, diag::toString(result.auditError.code));
        QVERIFY(audit.recentScans(1).value(0).ok);
    }

    void secondScanWhileBusyIsRejected() {
        ScanRig rig(gw::EcuScenario::ResponsePending, nullptr, nullptr);
        QVERIFY(rig.ready);
        rig.service.startScan();
        rig.service.startScan();
        QTRY_COMPARE(rig.finished.count(), 1);
        QCOMPARE(rig.finished.at(0).at(0).value<DtcScanResult>().acquisitionError.code, diag::Code::Busy);
        QTRY_COMPARE_WITH_TIMEOUT(rig.finished.count(), 2, 3000);
        QVERIFY(rig.finished.at(1).at(0).value<DtcScanResult>().acquired);
    }
};

QTEST_GUILESS_MAIN(DataLayerTest)
#include "test_data_layer.moc"
