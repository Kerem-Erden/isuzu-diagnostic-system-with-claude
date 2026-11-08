#include "diag/obd.hpp"

#include <cstring>

namespace diag {
namespace obd {
namespace {

// Ranges come from SAE J1979 (theoretical encoding range), not from Isuzu specifications;
// they are used only to label obviously broken values, never to diagnose faults.
constexpr PidDefinition kPids[] = {
    {0x04, "ENGINE_LOAD", "%", 1, 0.0, 100.0},
    {0x05, "COOLANT_TEMP", "degC", 1, -40.0, 215.0},
    {0x0C, "RPM", "rpm", 2, 0.0, 16383.75},
    {0x0D, "SPEED", "km/h", 1, 0.0, 255.0},
    {0x42, "MODULE_VOLTAGE", "V", 2, 0.0, 65.535},
};
constexpr std::size_t kPidCount = sizeof(kPids) / sizeof(kPids[0]);

}  // namespace

const PidDefinition* findPid(std::uint8_t pid) noexcept {
    for (const auto& definition : kPids) {
        if (definition.pid == pid) return &definition;
    }
    return nullptr;
}

const PidDefinition* findPidByName(const char* name) noexcept {
    if (name == nullptr) return nullptr;
    for (const auto& definition : kPids) {
        if (std::strcmp(definition.name, name) == 0) return &definition;
    }
    return nullptr;
}

const PidDefinition* livePids(std::size_t& count) noexcept {
    count = kPidCount;
    return kPids;
}

void buildMode01Request(std::uint8_t pid, std::uint8_t (&out)[2]) noexcept {
    out[0] = kServiceCurrentData;
    out[1] = pid;
}

Result<double> decodeMode01(std::uint8_t requestedPid, const std::uint8_t* payload, std::size_t length) noexcept {
    if (payload == nullptr || length < 2) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    if (payload[0] != kServiceCurrentData + kPositiveResponseOffset) {
        return fail(Stage::ObdDecode, Code::WrongService, payload[0]);
    }
    if (payload[1] != requestedPid) {
        return fail(Stage::ObdDecode, Code::WrongPid, payload[1]);
    }
    const PidDefinition* definition = findPid(requestedPid);
    if (definition == nullptr) {
        return fail(Stage::ObdDecode, Code::UnsupportedPid, requestedPid);
    }
    // Exact length: a single-PID request must return exactly the PID's data bytes.
    if (length != 2u + definition->dataBytes) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    const double a = payload[2];
    const double b = definition->dataBytes > 1 ? payload[3] : 0.0;
    switch (requestedPid) {
        case 0x04: return a * 100.0 / 255.0;
        case 0x05: return a - 40.0;
        case 0x0C: return (a * 256.0 + b) / 4.0;
        case 0x0D: return a;
        case 0x42: return (a * 256.0 + b) / 1000.0;
        default: return fail(Stage::ObdDecode, Code::UnsupportedPid, requestedPid);
    }
}

Result<std::uint32_t> decodeSupportedPids(const std::uint8_t* payload, std::size_t length) noexcept {
    if (payload == nullptr || length != 6) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    if (payload[0] != kServiceCurrentData + kPositiveResponseOffset) {
        return fail(Stage::ObdDecode, Code::WrongService, payload[0]);
    }
    if (payload[1] != 0x00) {
        return fail(Stage::ObdDecode, Code::WrongPid, payload[1]);
    }
    return (static_cast<std::uint32_t>(payload[2]) << 24) | (static_cast<std::uint32_t>(payload[3]) << 16) |
           (static_cast<std::uint32_t>(payload[4]) << 8) | payload[5];
}

Result<DtcList> decodeMode03(const std::uint8_t* payload, std::size_t length) noexcept {
    if (payload == nullptr || length < 2) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    if (payload[0] != kServiceStoredDtc + kPositiveResponseOffset) {
        return fail(Stage::ObdDecode, Code::WrongService, payload[0]);
    }
    const std::size_t count = payload[1];
    if (count > DtcList::kCapacity || length != 2 + 2 * count) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    DtcList list;
    for (std::size_t i = 0; i < count; ++i) {
        const Dtc dtc{payload[2 + 2 * i], payload[3 + 2 * i]};
        if (dtc.isNull()) {
            return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(i));
        }
        list.push(dtc);
    }
    return list;
}

bool isNegativeResponse(const std::uint8_t* payload, std::size_t length, std::uint8_t service,
                        std::uint8_t& nrc) noexcept {
    if (payload == nullptr || length != 3 || payload[0] != kNegativeResponse || payload[1] != service) {
        return false;
    }
    nrc = payload[2];
    return true;
}

const char* nrcName(std::uint8_t nrc) noexcept {
    switch (nrc) {
        case 0x10: return "generalReject";
        case 0x11: return "serviceNotSupported";
        case 0x12: return "subFunctionNotSupported";
        case 0x13: return "incorrectMessageLengthOrInvalidFormat";
        case 0x21: return "busyRepeatRequest";
        case 0x22: return "conditionsNotCorrect";
        case 0x31: return "requestOutOfRange";
        case 0x33: return "securityAccessDenied";
        case 0x78: return "requestCorrectlyReceivedResponsePending";
        case 0x7F: return "serviceNotSupportedInActiveSession";
        default: return "unknownNrc";
    }
}

}  // namespace obd
}  // namespace diag
