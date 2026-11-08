#include "diag/obd.hpp"
#include "gateway_fixture.hpp"
#include "test_harness.hpp"

using diag::Code;
using diag::Stage;
using gw::EcuScenario;

namespace {
bool value(const proto::Message& m, std::string_view key, std::string_view expected) {
    std::string_view v;
    return m.find(key, v) && v == expected;
}
}  // namespace

TEST_CASE(gateway_live_pid_table_size) {
    std::size_t count = 0;
    diag::obd::livePids(count);
    CHECK_EQ(count, gw::Gateway::kLivePidCount);
}

TEST_CASE(gateway_boots_listen_only_and_announces_source) {
    Host host;
    CHECK(host.countEvents("BOOT") == 1);
    CHECK(host.hasBodyContaining("source=SIMULATION"));
    CHECK(!host.gateway.transmitEnabled());
    CHECK(host.ecu.status().state == gw::BusState::ListenOnly);
}

TEST_CASE(gateway_hello_reports_identity_and_can_config) {
    Host host;
    host.hello();
    proto::Message m;
    REQUIRE(host.response(1, m));
    CHECK(m.isOk());
    CHECK(value(m, "proto", "1"));
    CHECK(value(m, "source", "SIMULATION"));
    CHECK(value(m, "bitrate", "500000"));
    CHECK(value(m, "tx", "7E0"));
    CHECK(value(m, "rx", "7E8"));
    CHECK(value(m, "mode04", "0"));
    CHECK(host.gateway.sessionActive());
}

TEST_CASE(gateway_rejects_can_commands_before_hello) {
    Host host;
    host.request(5, "READ_DTC");
    proto::Message m;
    REQUIRE(host.response(5, m));
    CHECK(m.error == diag::fail(Stage::Protocol, Code::InvalidState));
    CHECK_EQ(host.ecu.requestsReceived(), 0u);  // nothing reached the bus
}

TEST_CASE(gateway_reads_multi_frame_dtcs_end_to_end) {
    Host host;
    host.hello();
    host.request(2, "READ_DTC");
    host.run(50);
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(m.isOk());
    CHECK(value(m, "count", "3"));
    CHECK(value(m, "dtcs", "P1093,P0087,P3FFF"));
    CHECK(value(m, "ecu", "7E8"));
    CHECK_EQ(host.ecu.flowControlsReceived(), 1u);  // 8-byte payload needed FF + FC + CF
}

TEST_CASE(gateway_reads_zero_and_single_dtc) {
    Host none(EcuScenario::NoDtcs);
    none.hello();
    none.request(2, "READ_DTC");
    none.run(30);
    proto::Message m;
    REQUIRE(none.response(2, m));
    CHECK(value(m, "count", "0"));
    CHECK(value(m, "dtcs", ""));

    Host single(EcuScenario::SingleDtc);
    single.hello();
    single.request(2, "READ_DTC");
    single.run(30);
    REQUIRE(single.response(2, m));
    CHECK(value(m, "dtcs", "P0087"));
}

TEST_CASE(gateway_splits_many_dtcs_into_parts) {
    Host host(EcuScenario::ManyDtcs);
    host.hello();
    host.request(2, "READ_DTC");
    host.run(100);
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(value(m, "count", "40"));
    CHECK(value(m, "parts", "2"));
    CHECK_EQ(host.countEvents("DTC_PART"), 2u);
}

TEST_CASE(gateway_reports_negative_response_with_nrc) {
    Host host(EcuScenario::NegativeResponse);
    host.hello();
    host.request(2, "READ_DTC");
    host.run(30);
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(m.error == diag::fail(Stage::NegativeResponse, Code::NegativeResponse, 0x22));
}

TEST_CASE(gateway_waits_through_response_pending) {
    Host host(EcuScenario::ResponsePending);
    host.hello();
    host.request(2, "READ_DTC");
    host.run(100);
    proto::Message m;
    CHECK(!host.response(2, m));  // P2 (1000 ms) would not have expired, but answer not yet there
    host.run(300);
    REQUIRE(host.response(2, m));
    CHECK(value(m, "count", "3"));
}

TEST_CASE(gateway_reports_silent_ecu_as_rx_timeout) {
    Host host(EcuScenario::Silent);
    host.hello();
    host.request(2, "READ_PID", {"0C"});
    host.run(300);
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(m.error == diag::fail(Stage::RxTimeout, Code::Timeout));
}

TEST_CASE(gateway_reports_missing_ack_as_tx_failure) {
    Host host(EcuScenario::NoAck);
    host.hello();
    host.request(2, "READ_PID", {"0C"});
    host.run(20);
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(m.error == diag::fail(Stage::CanTxCompletion, Code::TxFailed));
    CHECK_EQ(host.ecu.status().tec, 8);
}

TEST_CASE(gateway_reports_malformed_and_bad_sequence_dtc_responses) {
    Host malformed(EcuScenario::MalformedDtc);
    malformed.hello();
    malformed.request(2, "READ_DTC");
    malformed.run(50);
    proto::Message m;
    REQUIRE(malformed.response(2, m));
    CHECK(m.error.stage == Stage::ObdDecode);
    CHECK(m.error.code == Code::MalformedPayload);

    Host sequence(EcuScenario::BadSequence);
    sequence.hello();
    sequence.request(2, "READ_DTC");
    sequence.run(50);
    REQUIRE(sequence.response(2, m));
    CHECK(m.error == diag::fail(Stage::IsoTp, Code::BadSequence, 0x0102));
}

TEST_CASE(gateway_reads_single_pid) {
    Host host;
    host.hello();
    host.request(3, "READ_PID", {"05"});
    host.run(20);
    proto::Message m;
    REQUIRE(host.response(3, m));
    CHECK(m.isOk());
    CHECK(value(m, "pid", "05"));
    CHECK(value(m, "name", "COOLANT_TEMP"));
    CHECK(value(m, "unit", "degC"));
}

TEST_CASE(gateway_rejects_unknown_pid_and_command) {
    Host host;
    host.hello();
    host.request(3, "READ_PID", {"10"});
    host.request(4, "FLASH_ECU");
    proto::Message m;
    REQUIRE(host.response(3, m));
    CHECK(m.error == diag::fail(Stage::Protocol, Code::InvalidArgument));
    REQUIRE(host.response(4, m));
    CHECK(m.error == diag::fail(Stage::Protocol, Code::UnknownCommand));
    CHECK_EQ(host.ecu.requestsReceived(), 0u);
}

TEST_CASE(gateway_duplicate_request_id_is_not_retransmitted) {
    Host host;
    host.hello();
    host.request(7, "READ_DTC");
    host.run(50);
    const std::uint32_t before = host.ecu.requestsReceived();
    host.request(7, "READ_DTC");
    host.run(50);
    proto::Message m;
    REQUIRE(host.response(7, m));
    CHECK(m.error == diag::fail(Stage::Protocol, Code::DuplicateRequest));
    CHECK_EQ(host.ecu.requestsReceived(), before);
}

TEST_CASE(gateway_mode04_is_disabled) {
    Host host;
    host.hello();
    host.request(9, "CLEAR_DTC");
    host.run(20);
    proto::Message m;
    REQUIRE(host.response(9, m));
    CHECK(m.error == diag::fail(Stage::Capability, Code::Disabled));
    CHECK_EQ(host.ecu.requestsReceived(), 0u);  // no 04 frame on the bus
}

TEST_CASE(gateway_live_polling_round_robin_and_stop) {
    Host host;
    host.hello();
    host.request(2, "LIVE_START", {"500"});
    host.run(1000);  // 2 full cycles of 5 PIDs at 100 ms spacing
    CHECK(host.countEvents("LIVE") >= 9);
    CHECK(host.hasBodyContaining("LIVE|RPM|"));
    CHECK(host.hasBodyContaining("LIVE|MODULE_VOLTAGE|"));
    host.request(3, "PING");  // keep the session alive
    host.request(4, "LIVE_STOP");
    const std::size_t samples = host.countEvents("LIVE");
    host.run(1000);
    CHECK_EQ(host.countEvents("LIVE"), samples);
    CHECK(!host.gateway.transmitEnabled());  // back to listen-only when idle
}

TEST_CASE(gateway_live_period_is_bounded) {
    Host host;
    host.hello();
    host.request(2, "LIVE_START", {"100"});
    proto::Message m;
    REQUIRE(host.response(2, m));
    CHECK(m.error.code == Code::InvalidArgument);
    CHECK(!host.gateway.liveActive());
}

TEST_CASE(gateway_live_backs_off_on_silent_bus) {
    Host host(EcuScenario::Silent);
    host.hello();
    host.request(2, "LIVE_START", {"500"});
    host.run(2000);
    CHECK(!host.gateway.liveActive());
    CHECK(host.hasBodyContaining("reason=no_response"));
    CHECK_EQ(host.ecu.requestsReceived(), 3u);  // exactly maxConsecutiveLiveFailures attempts
}

TEST_CASE(gateway_live_skips_unsupported_pid) {
    Host host(EcuScenario::UnsupportedVoltage);
    host.hello();
    host.request(2, "LIVE_START", {"500"});
    host.run(400);
    host.request(3, "PING");
    host.run(1600);
    CHECK(host.hasBodyContaining("LIVE_ERR|MODULE_VOLTAGE|NRC|NEGATIVE_RESPONSE|31"));
    std::size_t voltageErrors = 0;
    for (const auto& body : host.bodies) {
        if (body.find("LIVE_ERR|MODULE_VOLTAGE") != std::string::npos) ++voltageErrors;
    }
    CHECK_EQ(voltageErrors, 1u);  // asked once, then skipped
    CHECK(host.gateway.liveActive());
}

TEST_CASE(gateway_user_request_queues_behind_live_sample) {
    Host host;
    host.hello();
    host.request(2, "LIVE_START", {"500"});
    host.run(1);  // a live transaction is now in flight
    host.request(3, "READ_DTC");
    host.run(60);
    proto::Message m;
    REQUIRE(host.response(3, m));
    CHECK(value(m, "count", "3"));
}

TEST_CASE(gateway_session_timeout_stops_live_and_disables_tx) {
    Host host;
    host.hello();
    host.request(2, "LIVE_START", {"500"});
    host.run(5100);  // desktop silent for > 5 s (e.g. crashed or USB unplugged)
    CHECK(!host.gateway.sessionActive());
    CHECK(!host.gateway.liveActive());
    CHECK(!host.gateway.transmitEnabled());
    const std::uint32_t requests = host.ecu.requestsReceived();
    host.run(2000);
    CHECK_EQ(host.ecu.requestsReceived(), requests);
}

TEST_CASE(gateway_survives_noise_and_reports_bad_frames) {
    Host host;
    host.raw("I (32) boot: garbage\n#RES|1|OK*0000\n");
    host.hello();
    proto::Message m;
    CHECK(host.response(1, m));
    CHECK(host.gateway.serialCounters().badChecksum == 1);
}

TEST_CASE(gateway_status_reports_bus_counters) {
    Host host;
    host.request(1, "STATUS");
    proto::Message m;
    REQUIRE(host.response(1, m));
    CHECK(value(m, "bus", "LISTEN_ONLY"));
    CHECK(value(m, "tx", "0"));
}

TEST_CASE(gateway_trace_emits_can_frames) {
    Host host;
    host.hello();
    host.request(2, "TRACE", {"1"});
    host.request(3, "READ_PID", {"0C"});
    host.run(20);
    CHECK(host.hasBodyContaining("CAN_TX|id=7E0|dlc=8|data=02010C"));
    CHECK(host.countEvents("CAN_RX") == 1);
}
