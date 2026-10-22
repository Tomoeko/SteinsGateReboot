#include "sgre/debug/game_test_trace.hpp"

#include "sgre/debug/test_json.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace sgre::debug {

	GameTestTrace::GameTestTrace(std::ostream& output) : m_Output(output) {
	}

	void GameTestTrace::Emit(std::string_view event, std::string_view fields) {
		constexpr std::size_t kMaximumEvents = 200000;
		constexpr std::size_t kMaximumBytes = 64 * 1024 * 1024;
		if (m_Sequence == kMaximumEvents || fields.size() > kMaximumBytes - m_Bytes) {
			throw std::runtime_error("headless game test trace exceeded its explicit work/byte limit");
		}
		++m_Sequence;
		m_Bytes += fields.size();
		std::ostringstream record;
		record << "sgre-test {\"schema\":1,\"sequence\":" << m_Sequence << ",\"event\":";
		WriteTestJsonString(record, event);
		record << fields << "}\n";
		m_Output << record.str();
		m_Output.flush();
		if (!m_Output) {
			throw std::runtime_error("headless game test trace output failed");
		}
	}

	void GameTestTrace::Begin(std::string_view identity, std::string_view parent) {
		std::ostringstream fields;
		fields << ",\"identity\":";
		WriteTestJsonString(fields, identity);
		fields << ",\"parent\":";
		WriteTestJsonString(fields, parent);
		fields << ",\"headless\":true,\"hardware_audio\":false,\"width\":1280,\"height\":720";
		Emit("begin", fields.str());
	}

	void GameTestTrace::Snapshot(const script::OriginalCheckpointLocation& location, std::string_view digest) {
		std::ostringstream fields;
		fields << ",\"storage\":";
		WriteTestJsonString(fields, location.Storage);
		fields << ",\"label\":";
		WriteTestJsonString(fields, location.Label);
		fields << ",\"line\":" << location.SourceLine << ",\"point\":" << location.Point << ",\"record\":" << location.Record << ",\"digest\":";
		WriteTestJsonString(fields, digest);
		Emit("snapshot", fields.str());
	}

	void GameTestTrace::Seek(const script::OriginalSceneSeekObservation& observation) {
		std::ostringstream fields;
		fields << ",\"phase\":" << static_cast<std::uint32_t>(observation.Phase) << ",\"storage\":";
		WriteTestJsonString(fields, observation.Storage);
		fields << ",\"line\":" << observation.SourceLine << ",\"point\":" << observation.Point << ",\"snapshots\":" << observation.Snapshots << ",\"reason\":";
		WriteTestJsonString(fields, observation.Reason);
		fields << ",\"source\":";
		WriteTestJsonString(fields, observation.Source);
		fields << ",\"function\":";
		WriteTestJsonString(fields, observation.Function);
		fields << ",\"script_line\":" << observation.ScriptLine;
		Emit("seek", fields.str());
	}

	void GameTestTrace::ScriptOutput(std::string_view message, bool error) {
		std::ostringstream fields;
		fields << ",\"error\":" << (error ? "true" : "false") << ",\"message\":";
		WriteTestJsonString(fields, message);
		Emit("script", fields.str());
	}

	void GameTestTrace::Frame(std::uint64_t updates, std::uint64_t draws) {
		Emit("frame", ",\"updates\":" + std::to_string(updates) + ",\"draws\":" + std::to_string(draws));
	}

	void GameTestTrace::Boundary(std::string_view kind, std::string_view reason) {
		std::ostringstream fields;
		fields << ",\"kind\":";
		WriteTestJsonString(fields, kind);
		fields << ",\"reason\":";
		WriteTestJsonString(fields, reason);
		Emit("boundary", fields.str());
	}

} // namespace sgre::debug
