#pragma once
// Test fixture: Gateway + SimulatedEcu + a fake desktop that speaks protocol v1.
// Time advances in 1 ms steps; everything runs in one thread, exactly like the firmware task.

#include <string>
#include <vector>

#include "gw/gateway.hpp"
#include "gw/simulated_ecu.hpp"
#include "proto/frame.hpp"
#include "proto/message.hpp"

// Note: Messages returned by response() hold string_views into `bodies`; use them before the
// next run()/request() call, which may grow the vector and move the strings.
class Host : public gw::SerialSink {
public:
    explicit Host(gw::EcuScenario scenario = gw::EcuScenario::Normal, gw::GatewayConfig config = {})
        : ecu(config.isotp), gateway(config, ecu, *this) {
        ecu.attach(gateway);
        ecu.setScenario(scenario);
        gateway.start(nowUs);
    }

    void write(std::string_view frame) override {
        reader.feed(frame.data(), frame.size(), [this](std::string_view body) { bodies.emplace_back(body); });
    }

    /// Sends `#REQ|id|command|args…` as raw bytes (optionally split to test fragmentation).
    void request(std::uint32_t id, const std::string& command, const std::vector<std::string>& args = {}) {
        proto::FrameWriter writer;
        proto::beginRequest(writer, id, command);
        for (const auto& arg : args) writer.field(arg);
        const std::string_view frame = writer.finish();
        gateway.onSerialBytes(frame.data(), frame.size(), nowUs);
    }

    void raw(const std::string& bytes) { gateway.onSerialBytes(bytes.data(), bytes.size(), nowUs); }

    void run(std::uint32_t milliseconds) {
        for (std::uint32_t i = 0; i < milliseconds; ++i) {
            nowUs += 1000;
            ecu.poll(nowUs);
            gateway.poll(nowUs);
        }
    }

    /// Last response for request @p id, parsed. Returns false if none.
    bool response(std::uint32_t id, proto::Message& out) const {
        for (auto it = bodies.rbegin(); it != bodies.rend(); ++it) {
            auto parsed = proto::parseBody(*it);
            if (parsed.ok() && parsed.value().type == proto::MessageType::Response && parsed.value().id == id) {
                out = parsed.value();
                return true;
            }
        }
        return false;
    }

    std::size_t countEvents(std::string_view kind) const {
        std::size_t n = 0;
        for (const auto& body : bodies) {
            auto parsed = proto::parseBody(body);
            if (parsed.ok() && parsed.value().type == proto::MessageType::Event && parsed.value().name == kind) ++n;
        }
        return n;
    }

    bool hasBodyContaining(std::string_view text) const {
        for (const auto& body : bodies) {
            if (body.find(text) != std::string::npos) return true;
        }
        return false;
    }

    void hello() {
        request(1, "HELLO");
    }

    std::uint64_t nowUs = 1'000'000;
    gw::SimulatedEcu ecu;
    gw::Gateway gateway;
    proto::FrameReader reader;
    std::vector<std::string> bodies;  // stable storage: Message views must point here
};
