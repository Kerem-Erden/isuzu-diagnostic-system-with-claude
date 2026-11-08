#pragma once
/**
 * @file app_context.hpp
 * @brief The objects every screen needs, created once by AppWindow (not owned by the pages).
 *
 * Passing one context instead of seven constructor arguments keeps the pages small; the pages
 * still only *use* services – all orchestration lives in the services/controller so it stays
 * testable without widgets.
 */

class AuditStore;
class ConnectionController;
class DtcListModel;
class DtcScanService;
class EventLogModel;
class GatewayClient;
class LiveDataModel;
class LiveDataService;
class QSettings;
namespace knowledge {
class KnowledgeRepository;
}

struct AppContext {
    ConnectionController* connection = nullptr;
    GatewayClient* client = nullptr;
    knowledge::KnowledgeRepository* knowledge = nullptr;
    AuditStore* audit = nullptr;
    DtcScanService* scans = nullptr;
    LiveDataService* live = nullptr;
    LiveDataModel* liveModel = nullptr;
    DtcListModel* dtcModel = nullptr;
    EventLogModel* log = nullptr;
    QSettings* settings = nullptr;
};
