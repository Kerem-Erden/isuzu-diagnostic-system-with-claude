// Qt Test: view models and ConnectionController without widgets (SIMULATION only).

#include <QColor>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "data/audit_store.hpp"
#include "knowledge/knowledge_repository.hpp"
#include "models/view_models.hpp"
#include "services/connection_controller.hpp"
#include "services/live_data_service.hpp"
#include "transport/simulation_transport.hpp"

class ModelsTest : public QObject {
    Q_OBJECT
private slots:
    void eventLogIsBoundedAndCountsDrops() {
        EventLogModel log(3);
        for (int i = 0; i < 5; ++i) log.append(QStringLiteral("EVENT"), QStringLiteral("K"), QString::number(i));
        QCOMPARE(log.rowCount(), 3);
        QCOMPARE(log.dropped(), 2u);
        QCOMPARE(log.index(0, EventLogModel::Text).data().toString(), QStringLiteral("2"));  // oldest kept
        QCOMPARE(log.index(2, EventLogModel::Text).data().toString(), QStringLiteral("4"));
    }

    void eventFilterByCategoryAndText() {
        EventLogModel log;
        log.append(QStringLiteral("CAN"), QStringLiteral("CAN_RX"), QStringLiteral("id=7E8 data=0441"));
        log.append(QStringLiteral("REQUEST"), QStringLiteral("READ_DTC"), QStringLiteral("#5 NRC/NEGATIVE_RESPONSE/22"));
        log.append(QStringLiteral("CAN"), QStringLiteral("CAN_TX"), QStringLiteral("id=7E0 data=0201"));
        EventFilterModel filter;
        filter.setSourceModel(&log);
        filter.setCategory(QStringLiteral("CAN"));
        QCOMPARE(filter.rowCount(), 2);
        filter.setText(QStringLiteral("7e8"));  // case-insensitive
        QCOMPARE(filter.rowCount(), 1);
        filter.setCategory(QString());
        filter.setText(QStringLiteral("nrc"));
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(log.categories(), QStringList({"CAN", "REQUEST"}));
    }

    void dtcModelShowsRawCodeAndStatusForEveryEntry() {
        DtcScanResult result;
        result.acquired = true;
        DtcEntry known;
        known.code = QStringLiteral("P0087");
        known.status = EnrichmentStatus::Known;
        known.knowledge.title = QStringLiteral("Fuel Rail Pressure (FRP) Too Low");
        DtcEntry unknown;
        unknown.code = QStringLiteral("P3FFF");
        unknown.status = EnrichmentStatus::Unknown;
        DtcEntry unavailable;
        unavailable.code = QStringLiteral("P1093");
        unavailable.knowledgeError = diag::fail(diag::Stage::Database, diag::Code::NotFound);
        result.entries = {known, unknown, unavailable};
        DtcListModel model;
        QVERIFY(!model.hasResult());
        model.setResult(result);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.index(0, DtcListModel::Title).data().toString(), known.knowledge.title);
        QCOMPARE(model.index(1, DtcListModel::Status).data().toString(), QStringLiteral("Unknown DTC"));
        QCOMPARE(model.index(2, DtcListModel::Code).data().toString(), QStringLiteral("P1093"));
        QVERIFY(model.index(2, DtcListModel::Title).data().toString().contains(QStringLiteral("NOT_FOUND")));
        QCOMPARE(model.entry(5), nullptr);
    }

    void liveModelFollowsService() {
        SimulationTransport transport;
        GatewayClient client(&transport);
        LiveDataService live(&client);
        LiveDataModel model(&live);
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.index(0, LiveDataModel::Value).data().toString(), QStringLiteral("—"));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        client.connectToGateway();
        QTRY_COMPARE(client.state(), GatewayClient::State::Ready);
        live.start(500);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 5, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(model.index(0, LiveDataModel::State).data().toString(), QStringLiteral("fresh"), 3000);
        QCOMPARE(model.index(0, LiveDataModel::Source).data().toString(), QStringLiteral("SIMULATION"));
        QCOMPARE(model.index(0, LiveDataModel::State).data(Qt::ForegroundRole).value<QColor>(), QColor(QStringLiteral("#1E7B34")));
    }

    void connectionControllerOpensAuditSession() {
        QTemporaryDir dir;
        AuditStore audit(dir.filePath(QStringLiteral("audit.db")));
        QVERIFY(audit.open().isOk());
        ConnectionController controller(&audit);
        QSignalSpy session(&controller, &ConnectionController::sessionChanged);
        QVERIFY(controller.connectTo(ConnectionController::simulationKey()));
        QVERIFY(!controller.connectTo(ConnectionController::simulationKey()));  // already connecting
        QTRY_COMPARE(session.count(), 1);
        QVERIFY(controller.sessionId() > 0);
        controller.disconnectLink();
        QTRY_COMPARE(session.count(), 2);
        QCOMPARE(controller.sessionId(), 0);
        QVERIFY(controller.lastError().contains(QStringLiteral("closed by user")));
    }

    void connectionControllerReportsMissingPort() {
        ConnectionController controller(nullptr);
        QSignalSpy errors(&controller, &ConnectionController::lastErrorChanged);
        QVERIFY(controller.connectTo(QStringLiteral("COM_DOES_NOT_EXIST_ISUZU")));
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 3000);
        QVERIFY(controller.lastError().startsWith(QStringLiteral("Connection failed")));
        QCOMPARE(controller.client()->state(), GatewayClient::State::Disconnected);
    }
};

QTEST_GUILESS_MAIN(ModelsTest)
#include "test_models.moc"
