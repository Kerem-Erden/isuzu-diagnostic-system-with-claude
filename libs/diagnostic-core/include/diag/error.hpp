#pragma once
/**
 * @file error.hpp
 * @brief Structured error model shared by firmware, serial protocol and desktop (ADR-005).
 *
 * The old system collapsed every failure into `ESP_FAIL`, so the desktop could not tell a
 * negative ECU response from a broken ISO-TP sequence. Here every failure carries:
 *  - a @ref diag::Stage – the layer that detected the problem,
 *  - a @ref diag::Code  – what went wrong,
 *  - a 32-bit @c detail – numeric context (NRC byte, expected/received sequence, length …).
 *
 * The type is trivially copyable, has no heap allocation and no exceptions, so it can be
 * returned from code running in a FreeRTOS task and serialised over UART unchanged.
 */

#include <cstdint>
#include <type_traits>
#include <utility>

namespace diag {

/// Layer in which an error was detected. Names are sent verbatim over the serial protocol.
enum class Stage : std::uint8_t {
    None = 0,
    CanEnqueue,       ///< Frame could not be queued in the CAN driver (queue full, bus off …).
    CanTxCompletion,  ///< Frame was queued but the controller reported TX failure (e.g. no ACK).
    CanBus,           ///< Bus-level error state (error passive / bus off) observed.
    RxTimeout,        ///< No (complete) response before the absolute deadline.
    Responder,        ///< Response came from an unexpected CAN ID / ECU.
    IsoTp,            ///< ISO 15765-2 transport violation (PCI, sequence, length, overflow).
    NegativeResponse, ///< ECU answered with 0x7F service NRC.
    ObdDecode,        ///< Positive response present but payload could not be decoded.
    SerialTransport,  ///< UART / USB serial link problem.
    Protocol,         ///< Serial protocol framing / parsing / correlation problem.
    Capability,       ///< Operation disabled by compile-time or runtime capability.
    Audit,            ///< Audit/history persistence failed (desktop only).
    Database,         ///< Knowledge database failure (desktop only).
    Internal,         ///< Programming error / violated precondition.
};

/// What went wrong. Codes are not stage-specific so they can be reused across layers.
enum class Code : std::uint8_t {
    Ok = 0,
    Timeout,
    QueueFull,
    TxFailed,
    BusOff,
    ErrorPassive,
    NotStarted,
    WrongResponder,
    InvalidPci,
    InvalidLength,
    BadSequence,
    BufferOverflow,
    FlowControlOverflow,
    NegativeResponse,
    ResponsePendingLimit,
    WrongService,
    WrongPid,
    UnsupportedPid,
    MalformedPayload,
    InvalidArgument,
    InvalidState,
    Disabled,
    Busy,
    DuplicateRequest,
    UnknownCommand,
    BadChecksum,
    FrameTooLong,
    Disconnected,
    NotFound,
    StorageFailure,
};

/// Stable upper-case identifier of a stage, e.g. "ISOTP". Never returns nullptr.
const char* toString(Stage stage) noexcept;
/// Stable upper-case identifier of a code, e.g. "BAD_SEQUENCE". Never returns nullptr.
const char* toString(Code code) noexcept;
/// Reverse lookup for protocol parsing. @return false when @p text is not a known stage.
bool stageFromString(const char* text, Stage& out) noexcept;
/// Reverse lookup for protocol parsing. @return false when @p text is not a known code.
bool codeFromString(const char* text, Code& out) noexcept;

/**
 * @brief A single error value.
 *
 * @c detail meaning depends on the code:
 *  - NegativeResponse: NRC byte (e.g. 0x22 conditionsNotCorrect)
 *  - BadSequence: (expected << 8) | received sequence number
 *  - InvalidLength / BufferOverflow: offending length
 *  - WrongResponder: received CAN identifier
 */
struct Error {
    Stage stage = Stage::None;
    Code code = Code::Ok;
    std::uint32_t detail = 0;

    constexpr bool isOk() const noexcept { return code == Code::Ok; }
    friend constexpr bool operator==(const Error& a, const Error& b) noexcept {
        return a.stage == b.stage && a.code == b.code && a.detail == b.detail;
    }
    friend constexpr bool operator!=(const Error& a, const Error& b) noexcept { return !(a == b); }
};

/// Convenience factory: `fail(Stage::IsoTp, Code::BadSequence, 0x0203)`.
constexpr Error fail(Stage stage, Code code, std::uint32_t detail = 0) noexcept {
    return Error{stage, code, detail};
}

/**
 * @brief Value-or-error return type (a minimal, allocation-free `expected`).
 *
 * @tparam T must be default constructible; the value is only meaningful when ok().
 * We avoid std::variant/std::optional here only to keep the type trivially readable for
 * learners and identical between C++17 (firmware) and C++20 (desktop).
 */
template <typename T>
class Result {
    static_assert(std::is_default_constructible<T>::value, "Result<T> requires default-constructible T");

public:
    /* implicit */ Result(T value) : value_(std::move(value)) {}
    /* implicit */ Result(Error error) : error_(error) {}

    bool ok() const noexcept { return error_.isOk(); }
    explicit operator bool() const noexcept { return ok(); }
    const Error& error() const noexcept { return error_; }
    /// @pre ok() – reading the value of a failed result is a programming error.
    const T& value() const noexcept { return value_; }
    T& value() noexcept { return value_; }

private:
    T value_{};
    Error error_{};
};

}  // namespace diag
