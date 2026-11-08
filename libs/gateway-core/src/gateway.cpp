#include "gw/gateway.hpp"

#include "diag/obd.hpp"

namespace gw {
namespace {

using diag::Code;
using diag::Stage;

constexpr std::uint64_t msToUs(std::uint32_t ms) noexcept { return static_cast<std::uint64_t>(ms) * 1000u; }
constexpr std::uint32_t usToMs(std::uint64_t us) noexcept { return static_cast<std::uint32_t>(us / 1000u); }

/// NRCs that mean "this PID/service will never work on this ECU": stop asking for it.
constexpr bool isPermanentlyUnsupported(const diag::Error& error) noexcept {
    return error.stage == Stage::NegativeResponse && error.code == Code::NegativeResponse &&
           (error.detail == 0x11 || error.detail == 0x12 || error.detail == 0x31);
}

/// Failures that suggest nobody is answering; repeated ones stop live polling (bus protection).
constexpr bool isSilenceFailure(const diag::Error& error) noexcept {
    return error.stage == Stage::RxTimeout || error.stage == Stage::CanTxCompletion ||
           error.stage == Stage::CanEnqueue || error.stage == Stage::CanBus;
}

const diag::obd::PidDefinition& livePid(std::size_t index) noexcept {
    std::size_t count = 0;
    const diag::obd::PidDefinition* table = diag::obd::livePids(count);
    // Gateway::kLivePidCount must equal the table size (checked by gateway_live_pid_table_size).
    return table[index % count];
}

}  // namespace

const char* toString(BusState state) noexcept {
    switch (state) {
        case BusState::Stopped: return "STOPPED";
        case BusState::ListenOnly: return "LISTEN_ONLY";
        case BusState::Active: return "ACTIVE";
        case BusState::Warning: return "WARNING";
        case BusState::Passive: return "PASSIVE";
        case BusState::BusOff: return "BUS_OFF";
    }
    return "UNKNOWN";
}

const char* toString(DataSource source) noexcept {
    return source == DataSource::Vehicle ? "VEHICLE" : "SIMULATION";
}

Gateway::Gateway(const GatewayConfig& config, CanPort& can, SerialSink& serial) noexcept
    : config_(config), can_(can), serial_(serial), transaction_(config.isotp) {}

// ------------------------------------------------------------------ I/O helpers

void Gateway::emit(std::string_view frame) noexcept {
    // An empty view means the writer refused the frame (overflow / invalid char). Sending nothing
    // is safer than sending a truncated frame; count it so the bug is visible in STATUS.
    if (frame.empty()) {
        ++counters_.badFrames;
        return;
    }
    serial_.write(frame);
}

proto::FrameWriter& Gateway::beginEvent(std::string_view kind) noexcept {
    proto::beginEvent(writer_, eventSequence_, kind);
    eventSequence_ = eventSequence_ == proto::kMaxId ? 0 : eventSequence_ + 1;
    return writer_;
}

void Gateway::traceFrame(const char* kind, const diag::CanFrame& frame) noexcept {
    if (!trace_) return;
    static constexpr char kHex[] = "0123456789ABCDEF";
    char data[16];
    for (std::size_t i = 0; i < frame.dlc; ++i) {
        data[2 * i] = kHex[frame.data[i] >> 4];
        data[2 * i + 1] = kHex[frame.data[i] & 0x0F];
    }
    beginEvent(kind).keyHex("id", frame.id, 3).keyValue("dlc", frame.dlc);
    if (frame.dlc > 0) writer_.keyValue("data", std::string_view(data, 2u * frame.dlc));
    emit(writer_.keyValue("t", usToMs(frame.timestampUs)).finish());
}

void Gateway::sendError(std::uint32_t id, const diag::Error& error) noexcept {
    emit(proto::beginError(writer_, id, error).finish());
}

void Gateway::sendOkSimple(std::uint32_t id, std::string_view key, std::uint32_t value) noexcept {
    emit(proto::beginOk(writer_, id).keyValue(key, value).finish());
}

// ------------------------------------------------------------------ lifecycle

void Gateway::start(std::uint64_t nowUs) noexcept {
    nowUs_ = nowUs;
    // Boot in listen-only: nothing is transmitted until a desktop asks for data (ADR-009).
    can_.setTransmitEnabled(false);
    transmitEnabled_ = false;
    emit(beginEvent("BOOT")
             .keyValue("proto", proto::kProtocolVersion)
             .keyValue("source", toString(config_.source))
             .finish());
}

void Gateway::onSerialBytes(const char* data, std::size_t length, std::uint64_t nowUs) noexcept {
    nowUs_ = nowUs;
    reader_.feed(data, length, [this, nowUs](std::string_view body) { handleBody(body, nowUs); });
}

void Gateway::onCanFrame(const diag::CanFrame& frame) noexcept {
    if (frame.id == config_.isotp.rxId) traceFrame("CAN_RX", frame);
    diag::IsoTpReceiver::Output output = transaction_.onFrame(frame);
    if (output.sendFrame) {
        output.frame.timestampUs = frame.timestampUs;
        traceFrame("CAN_TX", output.frame);
        // A failed FC enqueue makes the ECU stop after the FF; the transaction then ends with an
        // RX timeout whose detail (bytes received) shows that only the FF arrived.
        if (!can_.enqueue(output.frame).isOk()) ++counters_.flowControlEnqueueFailures;
    }
}

void Gateway::onTxComplete(bool success) noexcept { transaction_.onTxComplete(success); }

void Gateway::poll(std::uint64_t nowUs) noexcept {
    nowUs_ = nowUs;
    transaction_.poll(nowUs);
    if (active_.kind != Kind::None && transaction_.finished()) finishTransaction(nowUs);

    if (sessionActive_ && nowUs - lastHostActivityUs_ >= msToUs(config_.sessionTimeoutMs)) {
        ++counters_.sessionTimeouts;
        endSession();
    }
    if (active_.kind == Kind::None) {
        if (queued_.kind != Kind::None) {
            const PendingRequest next = queued_;
            queued_ = PendingRequest{};
            startTransaction(next, nowUs);
        } else {
            scheduleLive(nowUs);
        }
    }
    updateTransmitState();
}

void Gateway::reportDiagnostic(std::string_view category, std::string_view key, std::string_view value) noexcept {
    emit(beginEvent("DIAG").field(category).keyValue(key, value).finish());
}

void Gateway::endSession() noexcept {
    sessionActive_ = false;
    if (liveActive_) stopLive("session_timeout");
    queued_ = PendingRequest{};
    emit(beginEvent("DIAG").field("session").keyValue("state", "ENDED").finish());
}

void Gateway::updateTransmitState() noexcept {
    // Return to listen-only as soon as nothing needs the bus: outside a session, or idle with
    // live polling off. A running transaction always keeps TX enabled until it finishes.
    const bool needed = active_.kind != Kind::None || (sessionActive_ && (liveActive_ || queued_.kind != Kind::None));
    if (!needed && transmitEnabled_) {
        can_.setTransmitEnabled(false);
        transmitEnabled_ = false;
    }
}

// ------------------------------------------------------------------ requests

void Gateway::handleBody(std::string_view body, std::uint64_t nowUs) noexcept {
    diag::Result<proto::Message> parsed = proto::parseBody(body);
    if (!parsed.ok() || parsed.value().type != proto::MessageType::Request) {
        // Without a valid id we cannot answer; report through an event instead.
        ++counters_.badFrames;
        const diag::Error error = parsed.ok() ? diag::fail(Stage::Protocol, Code::InvalidState) : parsed.error();
        emit(beginEvent("DIAG")
                 .field("protocol")
                 .keyValue("error", diag::toString(error.code))
                 .finish());
        return;
    }
    ++counters_.requests;
    lastHostActivityUs_ = nowUs;
    handleRequest(parsed.value(), nowUs);
}

bool Gateway::isDuplicate(std::uint32_t id) noexcept {
    for (std::uint32_t seen : recentIds_) {
        if (seen == id) {
            ++counters_.duplicates;
            return true;
        }
    }
    recentIds_[recentNext_] = id;
    recentNext_ = (recentNext_ + 1) % recentIds_.size();
    return false;
}

void Gateway::handleRequest(const proto::Message& request, std::uint64_t nowUs) noexcept {
    const std::string_view command = request.name;
    const std::uint32_t id = request.id;

    if (command == "HELLO") {
        sessionActive_ = true;
        sendHello(id);
        return;
    }
    if (command == "PING") {
        emit(proto::beginOk(writer_, id).field("pong").finish());
        return;
    }
    if (command == "STATUS") {
        sendStatus(id);
        return;
    }
    if (!sessionActive_) {
        // No CAN activity without a session: the desktop must identify the source first.
        sendError(id, diag::fail(Stage::Protocol, Code::InvalidState));
        return;
    }
    if (command == "LIVE_STOP") {
        if (liveActive_) stopLive("requested");
        sendOkSimple(id, "live", 0);
        return;
    }
    if (command == "TRACE") {
        std::uint32_t on = 0;
        if (request.argCount != 1 || !proto::parseDecimal(request.args[0], 1, on)) {
            sendError(id, diag::fail(Stage::Protocol, Code::InvalidArgument));
            return;
        }
        trace_ = on == 1;
        sendOkSimple(id, "trace", on);
        return;
    }

    const bool sideEffect = command == "READ_PID" || command == "READ_DTC" || command == "LIVE_START" ||
                            command == "CLEAR_DTC";
    if (!sideEffect) {
        sendError(id, diag::fail(Stage::Protocol, Code::UnknownCommand));
        return;
    }
    if (isDuplicate(id)) {
        // Never repeat a CAN transmission because the desktop resent the same request id.
        sendError(id, diag::fail(Stage::Protocol, Code::DuplicateRequest));
        return;
    }

    if (command == "CLEAR_DTC") {
        // WARNING: Mode 04 is intentionally not wired into this build (ADR-008). Enabling it
        // requires vehicle-validated Mode 01/03, explicit user approval and the guarded flow.
        sendError(id, diag::fail(Stage::Capability, Code::Disabled));
        return;
    }
    if (command == "LIVE_START") {
        std::uint32_t period = 0;
        if (request.argCount != 1 || !proto::parseDecimal(request.args[0], 600000, period) ||
            period < config_.minLivePeriodMs) {
            sendError(id, diag::fail(Stage::Protocol, Code::InvalidArgument, config_.minLivePeriodMs));
            return;
        }
        liveActive_ = true;
        liveIntervalUs_ = msToUs(period) / kLivePidCount;
        liveNextDueUs_ = nowUs;
        liveFailures_ = 0;
        sendOkSimple(id, "period", period);
        return;
    }
    if (command == "READ_PID") {
        std::uint32_t pid = 0;
        if (request.argCount != 1 || !proto::parseHex(request.args[0], pid) || pid > 0xFF ||
            diag::obd::findPid(static_cast<std::uint8_t>(pid)) == nullptr) {
            sendError(id, diag::fail(Stage::Protocol, Code::InvalidArgument));
            return;
        }
        submitUser(PendingRequest{Kind::UserPid, id, static_cast<std::uint8_t>(pid)}, nowUs);
        return;
    }
    submitUser(PendingRequest{Kind::UserDtc, id, 0}, nowUs);  // READ_DTC
}

void Gateway::submitUser(const PendingRequest& request, std::uint64_t nowUs) noexcept {
    const bool userBusy = active_.kind == Kind::UserPid || active_.kind == Kind::UserDtc || queued_.kind != Kind::None;
    if (userBusy) {
        sendError(request.id, diag::fail(Stage::Internal, Code::Busy));
        return;
    }
    if (active_.kind == Kind::LivePid) {
        queued_ = request;  // runs right after the current live sample; live is not cancelled mid-frame
        return;
    }
    startTransaction(request, nowUs);
}

void Gateway::sendHello(std::uint32_t id) noexcept {
    emit(proto::beginOk(writer_, id)
             .keyValue("proto", proto::kProtocolVersion)
             .keyValue("fw", config_.firmwareVersion)
             .keyValue("build", config_.buildId)
             .keyValue("source", toString(config_.source))
             .keyValue("bitrate", config_.bitrate)
             .keyHex("tx", config_.isotp.txId, 3)
             .keyHex("rx", config_.isotp.rxId, 3)
             .keyValue("ids", "STD")
             .keyValue("mode04", 0u)
             .keyValue("maxframe", static_cast<std::uint32_t>(proto::kMaxFrameLength))
             .finish());
}

void Gateway::sendStatus(std::uint32_t id) noexcept {
    const BusStatus bus = can_.status();
    emit(proto::beginOk(writer_, id)
             .keyValue("bus", toString(bus.state))
             .keyValue("tec", bus.tec)
             .keyValue("rec", bus.rec)
             .keyValue("rxdrop", bus.rxDropped)
             .keyValue("buserr", bus.busErrors)
             .keyValue("live", liveActive_ ? 1u : 0u)
             .keyValue("tx", transmitEnabled_ ? 1u : 0u)
             .keyValue("badframes", counters_.badFrames)
             .keyValue("crcerr", reader_.counters().badChecksum)
             .finish());
}

// ------------------------------------------------------------------ transactions

void Gateway::startTransaction(const PendingRequest& request, std::uint64_t nowUs) noexcept {
    std::uint8_t payload[2] = {};
    std::size_t length = 0;
    diag::ObdTransaction::Timing timing = config_.pidTiming;
    if (request.kind == Kind::UserDtc) {
        payload[0] = diag::obd::kServiceStoredDtc;
        length = 1;
        timing = config_.dtcTiming;
    } else {
        diag::obd::buildMode01Request(request.pid, payload);
        length = 2;
    }
    active_ = request;
    ++counters_.transactions;

    const BusStatus bus = can_.status();
    if (bus.state == BusState::BusOff) {
        // Do not even try: a bus-off controller cannot send, and the desktop should see why.
        transaction_.cancel();
        active_ = PendingRequest{};
        const diag::Error error = diag::fail(Stage::CanBus, Code::BusOff, bus.tec);
        if (request.kind == Kind::LivePid) {
            beginEvent("LIVE_ERR").field(livePid(liveIndex_).name);
            emit(proto::appendError(writer_, error).finish());
            stopLive("bus_off");
        } else {
            sendError(request.id, error);
        }
        ++counters_.transactionFailures;
        return;
    }
    if (!transmitEnabled_) {
        const diag::Error enabled = can_.setTransmitEnabled(true);
        transmitEnabled_ = enabled.isOk();
    }

    diag::Result<diag::CanFrame> frame = transaction_.begin(payload, length, nowUs, timing);
    if (frame.ok()) {
        traceFrame("CAN_TX", frame.value());
        transaction_.onEnqueueResult(transmitEnabled_ ? can_.enqueue(frame.value())
                                                      : diag::fail(Stage::CanEnqueue, Code::InvalidState));
    }
    if (transaction_.finished()) finishTransaction(nowUs);
}

void Gateway::finishTransaction(std::uint64_t nowUs) noexcept {
    if (transaction_.state() == diag::ObdTransaction::State::Failed) ++counters_.transactionFailures;
    switch (active_.kind) {
        case Kind::LivePid: finishLive(nowUs); break;
        case Kind::UserPid: finishUserPid(); break;
        case Kind::UserDtc: finishUserDtc(); break;
        case Kind::None: break;
    }
    active_ = PendingRequest{};
    transaction_.cancel();  // back to Idle, ready for the next begin()
}

void Gateway::finishLive(std::uint64_t nowUs) noexcept {
    const std::size_t index = liveIndex_;
    const diag::obd::PidDefinition& pid = livePid(index);
    liveIndex_ = (liveIndex_ + 1) % kLivePidCount;
    diag::Error error = transaction_.error();
    double value = 0.0;
    if (transaction_.state() == diag::ObdTransaction::State::Complete) {
        const diag::Result<double> decoded = diag::obd::decodeMode01(pid.pid, transaction_.payload(), transaction_.length());
        if (decoded.ok()) value = decoded.value();
        error = decoded.error();
    }
    if (error.isOk()) {
        liveFailures_ = 0;
        emit(beginEvent("LIVE").field(pid.name).fixed2(value).field(pid.unit).keyValue("t", usToMs(nowUs)).finish());
        return;
    }
    beginEvent("LIVE_ERR").field(pid.name);
    emit(proto::appendError(writer_, error).finish());
    if (isPermanentlyUnsupported(error)) liveUnsupported_[index] = true;
    if (isSilenceFailure(error) && ++liveFailures_ >= config_.maxConsecutiveLiveFailures) {
        stopLive("no_response");
    }
}

void Gateway::finishUserPid() noexcept {
    if (transaction_.state() != diag::ObdTransaction::State::Complete) {
        sendError(active_.id, transaction_.error());
        return;
    }
    const diag::Result<double> decoded = diag::obd::decodeMode01(active_.pid, transaction_.payload(), transaction_.length());
    if (!decoded.ok()) {
        sendError(active_.id, decoded.error());
        return;
    }
    const diag::obd::PidDefinition* pid = diag::obd::findPid(active_.pid);  // validated on submit
    emit(proto::beginOk(writer_, active_.id)
             .keyHex("pid", active_.pid, 2)
             .keyValue("name", pid->name)
             .keyFixed2("value", decoded.value())
             .keyValue("unit", pid->unit)
             .finish());
}

void Gateway::finishUserDtc() noexcept {
    if (transaction_.state() != diag::ObdTransaction::State::Complete) {
        sendError(active_.id, transaction_.error());
        return;
    }
    const diag::Result<diag::DtcList> decoded = diag::obd::decodeMode03(transaction_.payload(), transaction_.length());
    if (!decoded.ok()) {
        sendError(active_.id, decoded.error());
        return;
    }
    const diag::DtcList& list = decoded.value();
    // Long lists are sent as DTC_PART events before the final response, so no code is dropped
    // to fit a frame. The response states how many parts preceded it.
    std::uint32_t parts = 0;
    if (list.count > kDtcsPerChunk) {
        for (std::size_t start = 0; start < list.count; start += kDtcsPerChunk) {
            beginEvent("DTC_PART").keyValue("req", active_.id).beginField().append("dtcs=");
            for (std::size_t i = start; i < list.count && i < start + kDtcsPerChunk; ++i) {
                char text[diag::kDtcTextLength + 1];
                list.items[i].toText(text);
                if (i != start) writer_.append(",");
                writer_.append(text);
            }
            emit(writer_.finish());
            ++parts;
        }
    }
    proto::beginOk(writer_, active_.id).keyValue("count", static_cast<std::uint32_t>(list.count));
    if (parts == 0) {
        writer_.beginField().append("dtcs=");
        for (std::size_t i = 0; i < list.count; ++i) {
            char text[diag::kDtcTextLength + 1];
            list.items[i].toText(text);
            if (i != 0) writer_.append(",");
            writer_.append(text);
        }
    } else {
        writer_.keyValue("parts", parts);
    }
    emit(writer_.keyHex("ecu", config_.isotp.rxId, 3).finish());
}

// ------------------------------------------------------------------ live scheduling

void Gateway::scheduleLive(std::uint64_t nowUs) noexcept {
    if (!liveActive_ || !sessionActive_ || nowUs < liveNextDueUs_) return;
    // Skip PIDs the ECU declared unsupported; if all are unsupported, stop.
    for (std::size_t tries = 0; tries < kLivePidCount; ++tries) {
        if (!liveUnsupported_[liveIndex_]) {
            liveNextDueUs_ = nowUs + liveIntervalUs_;
            startTransaction(PendingRequest{Kind::LivePid, 0, livePid(liveIndex_).pid}, nowUs);
            return;
        }
        liveIndex_ = (liveIndex_ + 1) % kLivePidCount;
    }
    stopLive("all_unsupported");
}

void Gateway::stopLive(const char* reason) noexcept {
    liveActive_ = false;
    emit(beginEvent("DIAG").field("live").keyValue("state", "STOPPED").keyValue("reason", reason).finish());
}

}  // namespace gw
