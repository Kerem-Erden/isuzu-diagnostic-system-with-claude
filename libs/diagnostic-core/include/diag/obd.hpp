#pragma once
/**
 * @file obd.hpp
 * @brief OBD-II (SAE J1979 / ISO 15031-5) service codecs: Mode 01 live data, Mode 03 stored DTCs.
 *
 * These functions work on ISO-TP *payloads* (the bytes after the PCI), never on CAN frames.
 * A positive response echoes the service id + 0x40; a negative response is `7F <service> <NRC>`.
 *
 * Mode 04 (clear DTCs) is a write operation that erases freeze frames and readiness monitors on
 * a real vehicle. It is compiled only when DIAG_ENABLE_MODE04 is defined to 1 (ADR-008).
 */

#include <cstddef>
#include <cstdint>

#include "diag/dtc.hpp"
#include "diag/error.hpp"

#ifndef DIAG_ENABLE_MODE04
#define DIAG_ENABLE_MODE04 0
#endif

namespace diag {
namespace obd {

constexpr std::uint8_t kServiceCurrentData = 0x01;
constexpr std::uint8_t kServiceStoredDtc = 0x03;
constexpr std::uint8_t kServiceClearDtc = 0x04;
constexpr std::uint8_t kPositiveResponseOffset = 0x40;
constexpr std::uint8_t kNegativeResponse = 0x7F;
/// NRC 0x78 requestCorrectlyReceived-ResponsePending: the ECU needs more time (P2* applies).
constexpr std::uint8_t kNrcResponsePending = 0x78;

/// PIDs supported by the MVP (SAE J1979 Appendix B).
enum class Pid : std::uint8_t {
    SupportedPids01To20 = 0x00,
    CalculatedEngineLoad = 0x04,
    CoolantTemperature = 0x05,
    EngineRpm = 0x0C,
    VehicleSpeed = 0x0D,
    ControlModuleVoltage = 0x42,
};

/// Static description of one PID: wire size, display name, unit and physical range.
struct PidDefinition {
    std::uint8_t pid;
    const char* name;     ///< protocol/UI key, e.g. "RPM"
    const char* unit;     ///< e.g. "rpm", "degC", "km/h", "%", "V"
    std::uint8_t dataBytes;
    double minimum;
    double maximum;
};

/// @return definition for @p pid or nullptr if the MVP does not decode this PID.
const PidDefinition* findPid(std::uint8_t pid) noexcept;
/// @return definition whose name equals @p name, or nullptr.
const PidDefinition* findPidByName(const char* name) noexcept;
/// All decodable live-data PIDs (excludes the 0x00 support bitmap), for schedulers and UIs.
const PidDefinition* livePids(std::size_t& count) noexcept;

/// Request payload for Mode 01 PID: `01 <pid>`.
void buildMode01Request(std::uint8_t pid, std::uint8_t (&out)[2]) noexcept;

/**
 * @brief Decodes a positive Mode 01 response for @p requestedPid.
 *
 * Expected payload: `41 <pid> A [B]`. Formulas:
 *  - 0x04 load    = A * 100 / 255 [%]
 *  - 0x05 coolant = A - 40 [°C]
 *  - 0x0C rpm     = (256A + B) / 4 [rpm]
 *  - 0x0D speed   = A [km/h]
 *  - 0x42 voltage = (256A + B) / 1000 [V]
 *
 * @return physical value, or Stage::ObdDecode with WrongService/WrongPid/MalformedPayload/
 *         UnsupportedPid.
 */
Result<double> decodeMode01(std::uint8_t requestedPid, const std::uint8_t* payload, std::size_t length) noexcept;

/// Decodes `41 00 A B C D` into a bitmap where bit (32 - pid) is set when PID 0x01..0x20 is supported.
Result<std::uint32_t> decodeSupportedPids(const std::uint8_t* payload, std::size_t length) noexcept;
/// @return true if @p pid (0x01..0x20) is set in a bitmap from decodeSupportedPids().
constexpr bool isPidSupported(std::uint32_t bitmap, std::uint8_t pid) noexcept {
    return pid >= 0x01 && pid <= 0x20 && ((bitmap >> (32 - pid)) & 1u) != 0;
}

/// Request payload for Mode 03: `03`.
constexpr std::uint8_t kMode03Request[1] = {kServiceStoredDtc};

/**
 * @brief Decodes a Mode 03 response as used on CAN (ISO 15765-4): `43 NN (A B)*NN`.
 *
 * On CAN the response contains the DTC count byte NN (unlike K-line). The ISO-TP length is
 * exact, so the payload must be exactly 2 + 2*NN bytes. A 0x0000 pair is rejected, because with
 * an explicit count there is no padding.
 */
Result<DtcList> decodeMode03(const std::uint8_t* payload, std::size_t length) noexcept;

/// @return true if @p payload is `7F <service> <nrc>` for @p service; sets @p nrc.
bool isNegativeResponse(const std::uint8_t* payload, std::size_t length, std::uint8_t service,
                        std::uint8_t& nrc) noexcept;

/// Human-readable ISO 14229 NRC name ("conditionsNotCorrect"); "unknownNrc" otherwise.
const char* nrcName(std::uint8_t nrc) noexcept;

#if DIAG_ENABLE_MODE04
// WARNING: Mode 04 clears stored DTCs, freeze frames and readiness monitors on a REAL ECU.
// Compiled only for simulation tests or after explicit user approval (ADR-008).
constexpr std::uint8_t kMode04Request[1] = {kServiceClearDtc};
/// Positive Mode 04 response is exactly `44`.
inline Error validateMode04Response(const std::uint8_t* payload, std::size_t length) noexcept {
    if (payload == nullptr || length != 1 || payload[0] != kServiceClearDtc + kPositiveResponseOffset) {
        return fail(Stage::ObdDecode, Code::MalformedPayload, static_cast<std::uint32_t>(length));
    }
    return Error{};
}
#endif

}  // namespace obd
}  // namespace diag
