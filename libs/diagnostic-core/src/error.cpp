#include "diag/error.hpp"

#include <cstring>

namespace diag {
namespace {

// The tables are indexed by the enum value; order MUST match the enum declarations.
// The strings are part of the serial protocol (docs/serial-protocol.md §6): renaming one
// is a protocol-breaking change.
constexpr const char* kStageNames[] = {
    "NONE",     "CAN_ENQUEUE", "CAN_TX",  "CAN_BUS",  "RX_TIMEOUT", "RESPONDER", "ISOTP", "NRC",
    "OBD_DECODE", "SERIAL",    "PROTOCOL", "CAPABILITY", "AUDIT",    "DATABASE",  "INTERNAL",
};

constexpr const char* kCodeNames[] = {
    "OK",
    "TIMEOUT",
    "QUEUE_FULL",
    "TX_FAILED",
    "BUS_OFF",
    "ERROR_PASSIVE",
    "NOT_STARTED",
    "WRONG_RESPONDER",
    "INVALID_PCI",
    "INVALID_LENGTH",
    "BAD_SEQUENCE",
    "BUFFER_OVERFLOW",
    "FLOW_CONTROL_OVERFLOW",
    "NEGATIVE_RESPONSE",
    "RESPONSE_PENDING_LIMIT",
    "WRONG_SERVICE",
    "WRONG_PID",
    "UNSUPPORTED_PID",
    "MALFORMED_PAYLOAD",
    "INVALID_ARGUMENT",
    "INVALID_STATE",
    "DISABLED",
    "BUSY",
    "DUPLICATE_REQUEST",
    "UNKNOWN_COMMAND",
    "BAD_CHECKSUM",
    "FRAME_TOO_LONG",
    "DISCONNECTED",
    "NOT_FOUND",
    "STORAGE_FAILURE",
};

static_assert(sizeof(kStageNames) / sizeof(kStageNames[0]) == static_cast<unsigned>(Stage::Internal) + 1,
              "kStageNames out of sync with Stage");
static_assert(sizeof(kCodeNames) / sizeof(kCodeNames[0]) == static_cast<unsigned>(Code::StorageFailure) + 1,
              "kCodeNames out of sync with Code");

template <typename Enum, std::size_t N>
bool lookup(const char* const (&names)[N], const char* text, Enum& out) noexcept {
    if (text == nullptr) return false;
    for (std::size_t i = 0; i < N; ++i) {
        if (std::strcmp(names[i], text) == 0) {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    return false;
}

}  // namespace

const char* toString(Stage stage) noexcept {
    const auto index = static_cast<unsigned>(stage);
    return index < sizeof(kStageNames) / sizeof(kStageNames[0]) ? kStageNames[index] : "UNKNOWN";
}

const char* toString(Code code) noexcept {
    const auto index = static_cast<unsigned>(code);
    return index < sizeof(kCodeNames) / sizeof(kCodeNames[0]) ? kCodeNames[index] : "UNKNOWN";
}

bool stageFromString(const char* text, Stage& out) noexcept { return lookup(kStageNames, text, out); }
bool codeFromString(const char* text, Code& out) noexcept { return lookup(kCodeNames, text, out); }

}  // namespace diag
