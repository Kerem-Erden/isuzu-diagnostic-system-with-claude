#pragma once
/**
 * @file view_models.hpp
 * @brief Qt item models for the screens. They hold presentation state only and are tested
 *        without widgets (tests/unit/desktop/test_models.cpp).
 *
 * - LiveDataModel   – one row per LiveDataService parameter.
 * - DtcListModel    – rows of the latest DtcScanResult (raw code always shown).
 * - EventLogModel   – bounded ring buffer of structured developer events (brief §17: no
 *                     unbounded raw log); EventFilterModel filters by category and text.
 */

#include <QAbstractTableModel>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QVector>

#include "services/dtc_scan_service.hpp"

class LiveDataService;

/// Custom role carrying the row's state as int (LiveParameter::State / EnrichmentStatus).
constexpr int kStateRole = Qt::UserRole + 1;

class LiveDataModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Value, Unit, State, Updated, Source, ColumnCount };
    explicit LiveDataModel(const LiveDataService* service, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    static QString stateText(int state);

private:
    const LiveDataService* service_;
};

class DtcListModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Code, Status, Title, System, ColumnCount };
    using QAbstractTableModel::QAbstractTableModel;

    void setResult(const DtcScanResult& result);
    const DtcScanResult& result() const { return result_; }
    bool hasResult() const { return hasResult_; }
    const DtcEntry* entry(int row) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    static QString statusText(EnrichmentStatus status);

private:
    DtcScanResult result_;
    bool hasResult_ = false;
};

struct LogEntry {
    QDateTime time;
    QString category;   ///< CONNECTION, PROTOCOL, REQUEST, EVENT, CAN, SCAN, LIVE
    QString kind;       ///< e.g. READ_DTC, LIVE_ERR, CAN_RX
    QString text;
};

class EventLogModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Category, Kind, Text, ColumnCount };
    explicit EventLogModel(int capacity = 2000, QObject* parent = nullptr);

    void append(const QString& category, const QString& kind, const QString& text);
    void clear();
    int capacity() const { return capacity_; }
    /// Entries dropped because the buffer was full (shown so truncation is never silent).
    quint64 dropped() const { return dropped_; }
    QStringList categories() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<LogEntry> entries_;
    int capacity_;
    quint64 dropped_ = 0;
};

class EventFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    /// Empty = all categories.
    void setCategory(const QString& category);
    void setText(const QString& text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString category_;
    QString text_;
};
