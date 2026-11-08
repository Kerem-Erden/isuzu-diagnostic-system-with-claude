#include "diag/obd.hpp"
#include "diag/obd_transaction.hpp"
#include "test_harness.hpp"

#include <initializer_list>

using namespace diag;

namespace {

constexpr std::uint64_t kStart = 5'000'000;
const std::uint8_t kRpmRequest[] = {0x01, 0x0C};

CanFrame ecu(std::initializer_list<std::uint8_t> bytes, std::uint64_t offsetUs) {
    CanFrame result;
    result.id = 0x7E8;
    result.dlc = 8;
    std::size_t i = 0;
    for (std::uint8_t b : bytes) result.data[i++] = b;
    result.timestampUs = kStart + offsetUs;
    return result;
}

ObdTransaction::Timing timing() {
    ObdTransaction::Timing t;
    t.p2Ms = 100;
    t.p2StarMs = 1000;
    t.maxPendingResponses = 2;
    return t;
}

}  // namespace

TEST_CASE(transaction_happy_path_rpm) {
    ObdTransaction tx{IsoTpConfig{}};
    Result<CanFrame> request = tx.begin(kRpmRequest, 2, kStart, timing());
    REQUIRE(request.ok());
    CHECK_EQ(request.value().data[0], 0x02);
    tx.onEnqueueResult(Error{});
    tx.onTxComplete(true);
    CHECK(tx.state() == ObdTransaction::State::AwaitingResponse);
    tx.onFrame(ecu({0x04, 0x41, 0x0C, 0x1A, 0xF8}, 10'000));
    REQUIRE(tx.state() == ObdTransaction::State::Complete);
    Result<double> rpm = obd::decodeMode01(0x0C, tx.payload(), tx.length());
    CHECK(rpm.ok() && rpm.value() == 1726.0);
}

TEST_CASE(transaction_enqueue_failure_is_can_enqueue_stage) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.onEnqueueResult(fail(Stage::Internal, Code::QueueFull));
    CHECK(tx.error() == fail(Stage::CanEnqueue, Code::QueueFull));
}

TEST_CASE(transaction_tx_completion_failure_is_reported) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.onTxComplete(false);
    CHECK(tx.error() == fail(Stage::CanTxCompletion, Code::TxFailed));
}

TEST_CASE(transaction_missing_tx_completion_is_not_blamed_on_ecu) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.poll(kStart + 100'000);
    CHECK(tx.error() == fail(Stage::CanTxCompletion, Code::Timeout));
}

TEST_CASE(transaction_silent_ecu_is_rx_timeout) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.onTxComplete(true);
    tx.poll(kStart + 99'999);
    CHECK(!tx.finished());
    tx.poll(kStart + 100'000);
    CHECK(tx.error() == fail(Stage::RxTimeout, Code::Timeout));
}

TEST_CASE(transaction_response_before_tx_callback_completes) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.onFrame(ecu({0x04, 0x41, 0x0C, 0x00, 0x00}, 1000));
    CHECK(tx.state() == ObdTransaction::State::Complete);
    tx.onTxComplete(true);  // late callback must not change the result
    CHECK(tx.state() == ObdTransaction::State::Complete);
}

TEST_CASE(transaction_negative_response_keeps_nrc) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(obd::kMode03Request, 1, kStart, timing());
    tx.onTxComplete(true);
    tx.onFrame(ecu({0x03, 0x7F, 0x03, 0x22}, 5000));
    CHECK(tx.error() == fail(Stage::NegativeResponse, Code::NegativeResponse, 0x22));
}

TEST_CASE(transaction_response_pending_extends_deadline_to_p2_star) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(obd::kMode03Request, 1, kStart, timing());
    tx.onTxComplete(true);
    tx.onFrame(ecu({0x03, 0x7F, 0x03, 0x78}, 50'000));
    CHECK(tx.pendingResponses() == 1);
    tx.poll(kStart + 50'000 + 999'999);  // P2 (100 ms) passed long ago, P2* (1000 ms) not yet
    CHECK(!tx.finished());
    tx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}, 900'000));
    tx.onFrame(ecu({0x21, 0x03, 0xFF, 0xC1, 0x23, 0, 0, 0}, 901'000));
    REQUIRE(tx.state() == ObdTransaction::State::Complete);
    CHECK(obd::decodeMode03(tx.payload(), tx.length()).value().count == 4);
}

TEST_CASE(transaction_response_pending_is_bounded) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(obd::kMode03Request, 1, kStart, timing());
    tx.onTxComplete(true);
    tx.onFrame(ecu({0x03, 0x7F, 0x03, 0x78}, 10'000));
    tx.onFrame(ecu({0x03, 0x7F, 0x03, 0x78}, 20'000));
    tx.onFrame(ecu({0x03, 0x7F, 0x03, 0x78}, 30'000));  // third exceeds maxPendingResponses = 2
    CHECK(tx.error() == fail(Stage::NegativeResponse, Code::ResponsePendingLimit, 0x78));
}

TEST_CASE(transaction_wrong_positive_service_is_decode_error) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(obd::kMode03Request, 1, kStart, timing());
    tx.onTxComplete(true);
    tx.onFrame(ecu({0x02, 0x41, 0x0C}, 1000));
    CHECK(tx.error() == fail(Stage::ObdDecode, Code::WrongService, 0x41));
}

TEST_CASE(transaction_isotp_error_propagates) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(obd::kMode03Request, 1, kStart, timing());
    tx.onTxComplete(true);
    auto fc = tx.onFrame(ecu({0x10, 0x0A, 0x43, 0x04, 0x01, 0x0A, 0x04, 0x01}, 1000));
    CHECK(fc.sendFrame);
    tx.onFrame(ecu({0x25, 0, 0, 0, 0, 0, 0, 0}, 2000));
    CHECK(tx.error().stage == Stage::IsoTp);
    CHECK(tx.error().code == Code::BadSequence);
}

TEST_CASE(transaction_is_single_outstanding_and_never_retries) {
    ObdTransaction tx{IsoTpConfig{}};
    REQUIRE(tx.begin(kRpmRequest, 2, kStart, timing()).ok());
    CHECK(tx.begin(kRpmRequest, 2, kStart + 1, timing()).error() == fail(Stage::Internal, Code::Busy));
    CHECK(tx.state() == ObdTransaction::State::AwaitingTxCompletion);  // first exchange unaffected
    tx.onTxComplete(false);
    CHECK(tx.state() == ObdTransaction::State::Failed);  // no hidden retransmission
    CHECK(tx.begin(kRpmRequest, 2, kStart + 2, timing()).ok());  // only an explicit new request
}

TEST_CASE(transaction_cancel_returns_to_idle) {
    ObdTransaction tx{IsoTpConfig{}};
    tx.begin(kRpmRequest, 2, kStart, timing());
    tx.cancel();
    CHECK(tx.state() == ObdTransaction::State::Idle);
    tx.onFrame(ecu({0x04, 0x41, 0x0C, 0x00, 0x00}, 1000));
    CHECK(tx.state() == ObdTransaction::State::Idle);
}
