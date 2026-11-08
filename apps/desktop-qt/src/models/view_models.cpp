#include "models/view_models.hpp"

#include <QColor>
#include <QFont>
#include <algorithm>

#include "services/live_data_service.hpp"

namespace {
const QColor kText(QStringLiteral("#30343B"));
const QColor kMuted(QStringLiteral("#9AA0A8"));
const QColor kGood(QStringLiteral("#1E7B34"));
const QColor kWarn(QStringLiteral("#B26A00"));
const QColor kBad(QStringLiteral("#A5001E"));
}  // namespace

// ----------------------------------------------------------------------- LiveDataModel

LiveDataModel::LiveDataModel(const LiveDataService* service, QObject* parent)
    : QAbstractTableModel(parent), service_(service) {
    connect(service_, &LiveDataService::parameterChanged, this,
            [this](int row) { emit dataChanged(index(row, 0), index(row, ColumnCount - 1)); });
}

int LiveDataModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(service_->parameters().size());
}

int LiveDataModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QString LiveDataModel::stateText(int state) {
    switch (static_cast<LiveParameter::State>(state)) {
        case LiveParameter::State::NoData: return tr("no data");
        case LiveParameter::State::Fresh: return tr("fresh");
        case LiveParameter::State::Stale: return tr("stale");
        case LiveParameter::State::Unsupported: return tr("unsupported by ECU");
        case LiveParameter::State::Error: return tr("error");
    }
    return {};
}

QVariant LiveDataModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const LiveParameter& p = service_->parameters().at(index.row());
    using S = LiveParameter::State;
    if (role == kStateRole) return static_cast<int>(p.state);
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Name: return p.displayName;
            // Stale/error values stay visible (greyed) but are never presented as current.
            case Value: return p.hasValue ? QString::number(p.value, 'f', 2) : QStringLiteral("—");
            case Unit: return p.unit;
            case State:
                return p.state == S::Error || p.state == S::Unsupported
                           ? QStringLiteral("%1 (%2/%3)").arg(stateText(static_cast<int>(p.state)),
                                                              QLatin1String(diag::toString(p.lastError.stage)),
                                                              QLatin1String(diag::toString(p.lastError.code)))
                           : stateText(static_cast<int>(p.state));
            case Updated: return p.hasValue ? tr("t=%1 ms").arg(p.gatewayTimeMs) : QString();
            case Source: return p.source;
            default: return {};
        }
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == Value) return p.state == S::Fresh ? kText : kMuted;
        if (index.column() == State) {
            switch (p.state) {
                case S::Fresh: return kGood;
                case S::Stale: return kWarn;
                case S::Error: return kBad;
                default: return kMuted;
            }
        }
    }
    if (role == Qt::TextAlignmentRole && index.column() == Value) return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant LiveDataModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* const names[] = {"Parameter", "Value", "Unit", "State", "Gateway time", "Source"};
    return section >= 0 && section < ColumnCount ? tr(names[section]) : QVariant();
}

// ----------------------------------------------------------------------- DtcListModel

void DtcListModel::setResult(const DtcScanResult& result) {
    beginResetModel();
    result_ = result;
    hasResult_ = true;
    endResetModel();
}

const DtcEntry* DtcListModel::entry(int row) const {
    return row >= 0 && row < result_.entries.size() ? &result_.entries[row] : nullptr;
}

int DtcListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(result_.entries.size());
}

int DtcListModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QString DtcListModel::statusText(EnrichmentStatus status) {
    switch (status) {
        case EnrichmentStatus::Known: return tr("Known");
        case EnrichmentStatus::Unknown: return tr("Unknown DTC");
        case EnrichmentStatus::KnowledgeUnavailable: return tr("Knowledge unavailable");
    }
    return {};
}

QVariant DtcListModel::data(const QModelIndex& index, int role) const {
    const DtcEntry* e = entry(index.row());
    if (e == nullptr) return {};
    if (role == kStateRole) return static_cast<int>(e->status);
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case Code: return e->code;  // raw ECU code, never altered
            case Status: return statusText(e->status);
            case Title:
                return e->status == EnrichmentStatus::Known ? e->knowledge.title
                       : e->status == EnrichmentStatus::Unknown ? tr("Not in the knowledge database")
                                                                : tr("Description unavailable (%1)").arg(QLatin1String(diag::toString(e->knowledgeError.code)));
            case System: return e->knowledge.system;
            default: return {};
        }
    }
    if (role == Qt::ForegroundRole && index.column() == Status) {
        return e->status == EnrichmentStatus::Known ? kText : e->status == EnrichmentStatus::Unknown ? kWarn : kBad;
    }
    if (role == Qt::FontRole && index.column() == Code) {
        QFont font;
        font.setBold(true);
        return font;
    }
    return {};
}

QVariant DtcListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* const names[] = {"Code", "Knowledge", "Description", "System"};
    return section >= 0 && section < ColumnCount ? tr(names[section]) : QVariant();
}

// ----------------------------------------------------------------------- EventLogModel

EventLogModel::EventLogModel(int capacity, QObject* parent) : QAbstractTableModel(parent), capacity_(std::max(1, capacity)) {
    entries_.reserve(capacity_);
}

void EventLogModel::append(const QString& category, const QString& kind, const QString& text) {
    if (entries_.size() >= capacity_) {
        // Bounded: drop the oldest row. The drop counter makes the truncation visible in the UI.
        beginRemoveRows({}, 0, 0);
        entries_.removeFirst();
        endRemoveRows();
        ++dropped_;
    }
    const int row = static_cast<int>(entries_.size());
    beginInsertRows({}, row, row);
    entries_.append(LogEntry{QDateTime::currentDateTime(), category, kind, text});
    endInsertRows();
}

void EventLogModel::clear() {
    beginResetModel();
    entries_.clear();
    endResetModel();
}

QString EventLogModel::toCsv(const QStringList& header) const {
    // RFC 4180: quote every field, double embedded quotes. Newlines inside a field stay quoted.
    const auto quote = [](QString field) { return QLatin1Char('"') + field.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'); };
    QString csv;
    for (const QString& line : header) csv += QStringLiteral("# %1\n").arg(line);
    if (dropped_ > 0) csv += QStringLiteral("# %1 older entries were dropped (buffer capacity %2)\n").arg(dropped_).arg(capacity_);
    csv += QStringLiteral("time,category,kind,details\n");
    for (const LogEntry& e : entries_) {
        csv += quote(e.time.toString(Qt::ISODateWithMs)) + QLatin1Char(',') + quote(e.category) + QLatin1Char(',') +
               quote(e.kind) + QLatin1Char(',') + quote(e.text) + QLatin1Char('\n');
    }
    return csv;
}

QStringList EventLogModel::categories() const {
    QStringList result;
    for (const LogEntry& e : entries_) {
        if (!result.contains(e.category)) result.append(e.category);
    }
    result.sort();
    return result;
}

int EventLogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(entries_.size()); }
int EventLogModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant EventLogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size() || role != Qt::DisplayRole) return {};
    const LogEntry& e = entries_.at(index.row());
    switch (index.column()) {
        case Time: return e.time.toString(QStringLiteral("HH:mm:ss.zzz"));
        case Category: return e.category;
        case Kind: return e.kind;
        case Text: return e.text;
        default: return {};
    }
}

QVariant EventLogModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* const names[] = {"Time", "Category", "Kind", "Details"};
    return section >= 0 && section < ColumnCount ? tr(names[section]) : QVariant();
}

// Qt 6.9 replaced invalidateFilter() by beginFilterChange()/endFilterChange(); CI builds with 6.8.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#define ISUZU_FILTER_CHANGE(statement)     beginFilterChange();                   statement;                             endFilterChange()
#else
#define ISUZU_FILTER_CHANGE(statement)     statement;                             invalidateFilter()
#endif

void EventFilterModel::setCategory(const QString& category) { ISUZU_FILTER_CHANGE(category_ = category); }

void EventFilterModel::setText(const QString& text) { ISUZU_FILTER_CHANGE(text_ = text); }

bool EventFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QAbstractItemModel* model = sourceModel();
    if (!category_.isEmpty() &&
        model->index(sourceRow, EventLogModel::Category, sourceParent).data().toString() != category_) {
        return false;
    }
    if (text_.isEmpty()) return true;
    for (int column = 0; column < EventLogModel::ColumnCount; ++column) {
        if (model->index(sourceRow, column, sourceParent).data().toString().contains(text_, Qt::CaseInsensitive)) return true;
    }
    return false;
}
