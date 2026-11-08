#include "services/live_data_service.hpp"

#include <algorithm>

#include "diag/obd.hpp"
#include "gateway/gateway_client.hpp"

namespace {

constexpr int kAgeCheckMs = 250;
constexpr int kMinStaleMs = 1500;

QString displayName(const QString& key) {
    if (key == QLatin1String("RPM")) return QStringLiteral("Engine RPM");
    if (key == QLatin1String("COOLANT_TEMP")) return QStringLiteral("Coolant temperature");
    if (key == QLatin1String("SPEED")) return QStringLiteral("Vehicle speed");
    if (key == QLatin1String("ENGINE_LOAD")) return QStringLiteral("Calculated engine load");
    if (key == QLatin1String("MODULE_VOLTAGE")) return QStringLiteral("Control module voltage");
    return key;
}

QString displayUnit(const char* unit) {
    const QString text = QString::fromLatin1(unit);
    return text == QLatin1String("degC") ? QStringLiteral("°C") : text;
}

/// NRCs meaning "this ECU will never provide the PID" (same rule as the gateway's skip list).
bool isUnsupported(const diag::Error& error) {
    return error.stage == diag::Stage::NegativeResponse && error.code == diag::Code::NegativeResponse &&
           (error.detail == 0x11 || error.detail == 0x12 || error.detail == 0x31);
}

}  // namespace

LiveDataService::LiveDataService(GatewayClient* client, QObject* parent) : QObject(parent), client_(client) {
    // The PID table comes from diagnostic-core, so desktop and firmware can never disagree on it.
    std::size_t count = 0;
    const diag::obd::PidDefinition* pids = diag::obd::livePids(count);
    for (std::size_t i = 0; i < count; ++i) {
        LiveParameter p;
        p.key = QString::fromLatin1(pids[i].name);
        p.displayName = displayName(p.key);
        p.unit = displayUnit(pids[i].unit);
        p.pid = pids[i].pid;
        parameters_.append(p);
    }
    clock_.start();
    ageTimer_.setInterval(kAgeCheckMs);
    connect(&ageTimer_, &QTimer::timeout, this, &LiveDataService::ageValues);
    ageTimer_.start();
    connect(client_, &GatewayClient::replyReceived, this, &LiveDataService::onReply);
    connect(client_, &GatewayClient::eventReceived, this, &LiveDataService::onEvent);
    connect(client_, &GatewayClient::connectionLost, this, &LiveDataService::onDisconnected);
}

int LiveDataService::staleAfterMs() const {
    if (staleOverrideMs_ > 0) return staleOverrideMs_;
    return std::max(kMinStaleMs, 3 * periodMs_);
}

int LiveDataService::indexOf(const QString& key) const {
    for (int i = 0; i < parameters_.size(); ++i) {
        if (parameters_[i].key == key) return i;
    }
    return -1;
}

void LiveDataService::start(int periodMs) {
    periodMs_ = periodMs;
    // A new run gives every PID a fresh chance, including ones the ECU rejected before.
    for (int i = 0; i < parameters_.size(); ++i) {
        if (parameters_[i].state == LiveParameter::State::Unsupported) {
            parameters_[i].state = LiveParameter::State::NoData;
            emit parameterChanged(i);
        }
    }
    startId_ = client_->send(QStringLiteral("LIVE_START"), {QString::number(periodMs)});
}

void LiveDataService::stop() {
    if (!running_) return;
    stopId_ = client_->send(QStringLiteral("LIVE_STOP"));
}

void LiveDataService::onReply(const GatewayReply& reply) {
    if (reply.id == startId_ && startId_ != 0) {
        startId_ = 0;
        if (reply.ok) {
            running_ = true;
            emit started(periodMs_);
        } else {
            emit startFailed(reply.error);
        }
    } else if (reply.id == stopId_ && stopId_ != 0) {
        stopId_ = 0;
        // Even if LIVE_STOP failed (link dead), no more samples will be trusted as fresh.
        setStopped(QStringLiteral("requested"));
    }
}

void LiveDataService::setStopped(const QString& reason) {
    if (!running_) return;
    running_ = false;
    emit stopped(reason);
}

void LiveDataService::onEvent(const GatewayEvent& event) {
    if (event.kind == QLatin1String("LIVE")) {
        // LIVE|<NAME>|<value>|<unit>|t=<ms>
        const int index = indexOf(event.fields.value(0));
        bool numeric = false;
        const double value = event.fields.value(1).toDouble(&numeric);
        if (index < 0 || !numeric) return;
        LiveParameter& p = parameters_[index];
        p.value = value;
        p.hasValue = true;
        p.state = LiveParameter::State::Fresh;
        p.lastError = diag::Error{};
        p.receivedAtMs = clock_.elapsed();
        p.gatewayTimeMs = event.fields.value(3).mid(2).toUInt();  // "t=12345"
        p.source = client_->identity().source;
        emit parameterChanged(index);
    } else if (event.kind == QLatin1String("LIVE_ERR")) {
        // LIVE_ERR|<NAME>|<STAGE>|<CODE>|<detail hex>
        const int index = indexOf(event.fields.value(0));
        if (index < 0) return;
        diag::Error error;
        const QByteArray stage = event.fields.value(1).toLatin1();
        const QByteArray code = event.fields.value(2).toLatin1();
        diag::stageFromString(stage.constData(), error.stage);
        diag::codeFromString(code.constData(), error.code);
        error.detail = event.fields.value(3).toUInt(nullptr, 16);
        LiveParameter& p = parameters_[index];
        p.lastError = error;
        p.state = isUnsupported(error) ? LiveParameter::State::Unsupported : LiveParameter::State::Error;
        p.source = client_->identity().source;
        emit parameterChanged(index);
    } else if (event.kind == QLatin1String("DIAG") && event.fields.value(0) == QLatin1String("live")) {
        // DIAG|live|state=STOPPED|reason=<why> – the gateway stopped polling by itself.
        for (const QString& field : event.fields) {
            if (field.startsWith(QLatin1String("reason="))) setStopped(field.mid(7));
        }
    }
}

void LiveDataService::onDisconnected() {
    startId_ = stopId_ = 0;
    setStopped(QStringLiteral("disconnected"));
    // Without a link nothing can be current: age everything immediately instead of waiting.
    for (int i = 0; i < parameters_.size(); ++i) {
        if (parameters_[i].state == LiveParameter::State::Fresh) {
            parameters_[i].state = LiveParameter::State::Stale;
            emit parameterChanged(i);
        }
    }
}

void LiveDataService::ageValues() {
    const qint64 now = clock_.elapsed();
    for (int i = 0; i < parameters_.size(); ++i) {
        LiveParameter& p = parameters_[i];
        if (p.state == LiveParameter::State::Fresh && now - p.receivedAtMs > staleAfterMs()) {
            p.state = LiveParameter::State::Stale;
            emit parameterChanged(i);
        }
    }
}
