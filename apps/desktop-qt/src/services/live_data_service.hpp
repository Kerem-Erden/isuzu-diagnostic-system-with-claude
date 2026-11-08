#pragma once
/**
 * @file live_data_service.hpp
 * @brief Mode 01 live data: one state record per MVP PID, fed by gateway LIVE / LIVE_ERR events.
 *
 * States (brief §11, §17):
 * | State       | Meaning                                                                |
 * |-------------|------------------------------------------------------------------------|
 * | NoData      | nothing received since start                                           |
 * | Fresh       | last sample younger than the stale threshold                          |
 * | Stale       | last value is kept for display but is too old to trust                |
 * | Unsupported | ECU rejected the PID (NRC 0x11/0x12/0x31); the gateway stops asking   |
 * | Error       | last request failed (timeout, TX failure, decode error …); value kept |
 *
 * The polling itself (round robin, back-off after silence) runs in the gateway (gateway-core);
 * this service only starts/stops it and interprets the events. A value is never shown as Fresh
 * after the link is lost or polling stopped – it ages into Stale by the same timer.
 */

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include "diag/error.hpp"
#include "gateway/gateway_types.hpp"

class GatewayClient;

struct LiveParameter {
    enum class State { NoData, Fresh, Stale, Unsupported, Error };

    QString key;            ///< protocol name, e.g. "RPM"
    QString displayName;    ///< "Engine RPM"
    QString unit;           ///< display unit, e.g. "rpm", "°C"
    quint8 pid = 0;
    State state = State::NoData;
    bool hasValue = false;
    double value = 0.0;
    quint32 gatewayTimeMs = 0;   ///< firmware timestamp of the sample (t=…)
    qint64 receivedAtMs = -1;    ///< desktop monotonic time of the last sample
    diag::Error lastError{};     ///< for Error / Unsupported
    QString source;              ///< SIMULATION / VEHICLE from HELLO – shown with every value
};

class LiveDataService final : public QObject {
    Q_OBJECT
public:
    explicit LiveDataService(GatewayClient* client, QObject* parent = nullptr);

    /// Requests cyclic polling. Result arrives as started() or startFailed().
    void start(int periodMs = 1000);
    void stop();
    bool running() const { return running_; }

    const QList<LiveParameter>& parameters() const { return parameters_; }
    /// A sample older than this becomes Stale. Default: 3 polling periods, at least 1.5 s.
    void setStaleAfterMs(int ms) { staleOverrideMs_ = ms; }

signals:
    void started(int periodMs);
    void startFailed(const diag::Error& error);
    /// Polling ended: "requested", "no_response", "session_timeout", "disconnected", …
    void stopped(const QString& reason);
    void parameterChanged(int index);

private:
    void onReply(const GatewayReply& reply);
    void onEvent(const GatewayEvent& event);
    void onDisconnected();
    void ageValues();
    int indexOf(const QString& key) const;
    int staleAfterMs() const;
    void setStopped(const QString& reason);

    GatewayClient* client_;
    QList<LiveParameter> parameters_;
    QElapsedTimer clock_;
    QTimer ageTimer_;
    quint32 startId_ = 0;
    quint32 stopId_ = 0;
    int periodMs_ = 1000;
    int staleOverrideMs_ = 0;
    bool running_ = false;
};
