// Integration test of the whole desktop chain WITHOUT hardware (brief §16):
//
//   UI models ← services ← GatewayClient ← serial protocol v1 ← SimulationTransport
//            ← gw::Gateway (same code as firmware) ← ISO-TP / OBD ← SimulatedEcu
//   DtcScanService → KnowledgeRepository (real read-only DB) + AuditStore (SQLite)
//
// SIMULATION evidence only. It proves the software chain, not CAN wiring or ECU compatibility.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "data/audit_store.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "models/view_models.hpp"
#include "services/connection_controller.hpp"
#include "services/dtc_scan_service.hpp"
#include "services/live_data_service.hpp"
#include "transport/simulation_transport.hpp"

class EndToEndTest : public QObject {
    Q_OBJECT
private slots:
    void fullDiagnosticSessionInSimulation() {
        QTemporaryDir dir;
        knowledge::KnowledgeRepository knowledge(QStringLiteral(ISUZU_KNOWLEDGE_DB));
        QVERIFY(knowledge.open().isOk());
        AuditStore audit(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(audit.open().isOk());

        ConnectionController connection(&audit);
        GatewayClient* client = connection.client();
        DtcScanService scans(client, &knowledge, &audit);
        LiveDataService live(client);
        LiveDataModel liveModel(&live);
        DtcListModel dtcModel;
        EventLogModel log(200);
        QObject::connect(&scans, &DtcScanService::scanFinished, &dtcModel, &DtcListModel::setResult);
        QObject::connect(&connection, &ConnectionController::sessionChanged, &scans, &DtcScanService::setSessionId);
        QObject::connect(client, &GatewayClient::eventReceived, &log,
                         [&log](const GatewayEvent& e) { log.append(QStringLiteral("EVENT"), e.kind, e.fields.join(' ')); });

        // 1. connect: identity verified, audit session opened
        QSignalSpy session(&connection, &ConnectionController::sessionChanged);
        QVERIFY(connection.connectTo(ConnectionController::simulationKey()));
        QTRY_COMPARE(session.count(), 1);
        QVERIFY(client->identity().isSimulation());
        QVERIFY(connection.sessionId() > 0);

        // 2. Mode 03 through ISO-TP multi-frame → knowledge → audit → model
        QSignalSpy scanned(&scans, &DtcScanService::scanFinished);
        scans.startScan();
        QTRY_COMPARE(scanned.count(), 1);
        QCOMPARE(dtcModel.rowCount(), 3);
        QCOMPARE(dtcModel.index(0, DtcListModel::Title).data().toString(), QStringLiteral("Fuel Rail Pressure (FRP) Too Low"));
        QCOMPARE(dtcModel.index(2, DtcListModel::Status).data().toString(), QStringLiteral("Unknown DTC"));
        QVERIFY(dtcModel.result().audited);
        const QList<ScanRecord> stored = audit.recentScans(1);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored[0].sessionId, connection.sessionId());
        QCOMPARE(stored[0].source, QStringLiteral("SIMULATION"));

        // 3. Mode 01 live data → model
        live.start(500);
        QTRY_VERIFY_WITH_TIMEOUT(liveModel.index(2, LiveDataModel::State).data().toString() == QStringLiteral("fresh"), 3000);
        QVERIFY(log.rowCount() > 0);

        // 4. fault injection mid-session: ECU starts refusing → failed scan is reported and audited
        connection.simulation()->setScenario(gw::EcuScenario::NegativeResponse);
        live.stop();
        scans.startScan();
        QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 2, 3000);
        const DtcScanResult failed = scanned.at(1).at(0).value<DtcScanResult>();
        QVERIFY(!failed.acquired);
        QCOMPARE(failed.acquisitionError, diag::fail(diag::Stage::NegativeResponse, diag::Code::NegativeResponse, 0x22));
        QVERIFY(failed.audited);
        QCOMPARE(audit.recentScans(5).size(), 2);

        // 5. unplug: everything ends cleanly, live data no longer fresh
        connection.simulation()->simulateUnplug();
        QTRY_COMPARE(client->state(), GatewayClient::State::Disconnected);
        QCOMPARE(connection.sessionId(), 0);
        QVERIFY(liveModel.index(2, LiveDataModel::State).data().toString() != QStringLiteral("fresh"));
        QVERIFY(connection.lastError().contains(QStringLiteral("device removed")));
    }
};

QTEST_GUILESS_MAIN(EndToEndTest)
#include "test_end_to_end.moc"
