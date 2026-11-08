#pragma once
/**
 * @file pages.hpp
 * @brief The screens of the desktop application (brief §17). Each page is a thin view over the
 *        services in AppContext; it holds no diagnostic logic of its own.
 *
 * | Page            | Shows                                                                 |
 * |-----------------|-----------------------------------------------------------------------|
 * | ConnectionPage  | link selection, vehicle/ECU profile, verified gateway identity        |
 * | DashboardPage   | connection, last DTC scan and live snapshot at a glance              |
 * | LiveDataPage    | per-PID value / unit / state / timestamp / source                    |
 * | DtcPage         | DTC list (raw code + Known/Unknown) and detail (causes, steps, …)     |
 * | HistoryPage     | persisted scans from the audit database                              |
 * | DeveloperPage   | bounded, filterable structured events; CAN trace toggle; counters   |
 * | SettingsPage    | live period, stale threshold, database locations, safety status      |
 */

#include <QWidget>

#include "ui/app_context.hpp"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTableWidget;
class QTextBrowser;
class EventFilterModel;
struct DtcScanResult;
struct GatewayIdentity;

/// Sidebar order = QStackedWidget index.
enum PageIndex { kConnectionPage, kDashboardPage, kLivePage, kDtcPage, kHistoryPage, kDeveloperPage, kSettingsPage };

/// QSettings keys (per-user, HKCU on Windows).
namespace settings_keys {
inline constexpr char kLivePeriodMs[] = "live/periodMs";
inline constexpr char kStaleMs[] = "live/staleMs";
inline constexpr char kApplicationKey[] = "vehicle/applicationKey";
inline constexpr char kLastLink[] = "link/last";
}  // namespace settings_keys

/// Common page frame: title, optional hint line, then the page's own content.
class Page : public QWidget {
    Q_OBJECT
public:
    Page(const QString& title, const QString& hint, QWidget* parent = nullptr);

protected:
    class QVBoxLayout* content_;
};

class ConnectionPage final : public Page {
    Q_OBJECT
public:
    explicit ConnectionPage(AppContext& context, QWidget* parent = nullptr);

private:
    void refreshPorts();
    void updateState();
    void showIdentity(const GatewayIdentity& identity);

    AppContext& context_;
    QComboBox* ports_;
    QPushButton* refresh_;
    QPushButton* connect_;
    QLabel* state_;
    QComboBox* profile_;
    QFormLayout* identityForm_;
    QLabel* identity_[8];
};

class DashboardPage final : public Page {
    Q_OBJECT
public:
    explicit DashboardPage(AppContext& context, QWidget* parent = nullptr);

signals:
    void navigate(int pageIndex);

private:
    void refresh();

    AppContext& context_;
    QLabel* connection_;
    QLabel* connectionDetail_;
    QLabel* scan_;
    QLabel* scanDetail_;
    QLabel* live_;
    QLabel* liveDetail_;
    QPushButton* readDtc_;
    QPushButton* liveToggle_;
};

class LiveDataPage final : public Page {
    Q_OBJECT
public:
    explicit LiveDataPage(AppContext& context, QWidget* parent = nullptr);

private:
    void updateButtons();

    AppContext& context_;
    QPushButton* toggle_;
    QLabel* status_;
    QTableView* table_;
};

class DtcPage final : public Page {
    Q_OBJECT
public:
    explicit DtcPage(AppContext& context, QWidget* parent = nullptr);

private:
    void showScan(const DtcScanResult& result);
    void showDetail(int row);

    AppContext& context_;
    QPushButton* read_;
    QLabel* summary_;
    QTableView* table_;
    QTextBrowser* detail_;
};

class HistoryPage final : public Page {
    Q_OBJECT
public:
    explicit HistoryPage(AppContext& context, QWidget* parent = nullptr);
    void reload();

protected:
    void showEvent(class QShowEvent* event) override;

private:
    AppContext& context_;
    QLabel* status_;
    QTableWidget* table_;
};

class DeveloperPage final : public Page {
    Q_OBJECT
public:
    explicit DeveloperPage(AppContext& context, QWidget* parent = nullptr);

private:
    void updateCounters();
    void exportCsv();

    AppContext& context_;
    EventFilterModel* filter_;
    QComboBox* category_;
    QLineEdit* text_;
    QCheckBox* trace_;
    QLabel* counters_;
    QTableView* table_;
    QComboBox* singlePid_;
    QPushButton* singleRead_;
};

class SettingsPage final : public Page {
    Q_OBJECT
public:
    explicit SettingsPage(AppContext& context, QWidget* parent = nullptr);

private:
    AppContext& context_;
    QSpinBox* period_;
    QSpinBox* stale_;
};
