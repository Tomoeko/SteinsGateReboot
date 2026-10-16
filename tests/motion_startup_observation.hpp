#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace sgre::test {

	inline constexpr std::size_t kStartupFailureSamples = 32;
	inline constexpr std::size_t kStartupObservationBytes = 8192;
	inline constexpr std::string_view kStartupMotionRuntimeUnavailable = "startup Motion construction requires an actual registered Layer runtime with prepared application clock and shared624-word seed";
	inline constexpr std::string_view kStartupMotionModeUnavailable = "startup Motion mode read requires its actual application-owned mutable mode service";

	enum class StartupFailureScript : std::uint8_t {
		Unknown,
		Main,
		Startup,
		BasicPicture
	};

	struct StartupFailureLocation {
		StartupFailureScript Script = StartupFailureScript::Unknown;
		std::optional<std::uint64_t> Instruction;
		bool MalformedInstruction = false;
		bool RuntimeUnavailable = false;
		bool ModeUnavailable = false;
	};

	inline StartupFailureLocation ObserveStartupFailure(std::string_view diagnostic) {
		StartupFailureLocation result;
		result.RuntimeUnavailable = diagnostic.find(kStartupMotionRuntimeUnavailable) != std::string_view::npos;
		result.ModeUnavailable = diagnostic.find(kStartupMotionModeUnavailable) != std::string_view::npos;
		constexpr std::string_view marker = "at instruction ";
		const auto position = diagnostic.find(marker);
		const auto prefix = diagnostic.substr(0, position);
		constexpr std::array<std::pair<std::string_view, StartupFailureScript>, 3> scripts = {{
		    {"main.nut:", StartupFailureScript::Main},
		    {"startup.nut:", StartupFailureScript::Startup},
		    {"basicpicture.nut:", StartupFailureScript::BasicPicture},
		}};
		std::size_t last_script = 0;
		bool found_script = false;
		for (const auto& [name, kind] : scripts) {
			const auto found = prefix.rfind(name);
			if (found != std::string_view::npos && (!found_script || found > last_script)) {
				result.Script = kind;
				last_script = found;
				found_script = true;
			}
		}
		if (position == std::string_view::npos) {
			return result;
		}

		const auto number = diagnostic.substr(position + marker.size());
		std::uint64_t instruction = 0;
		const auto parsed = std::from_chars(number.data(), number.data() + number.size(), instruction);
		if (parsed.ec != std::errc{} || parsed.ptr == number.data()) {
			result.MalformedInstruction = true;
		} else {
			result.Instruction = instruction;
		}

		return result;
	}

	struct MotionStartupObservation {
		std::size_t Requests = 0;
		std::uint64_t Instructions = 0;
		std::uint32_t MainStatus = 0;
		StartupFailureLocation MainFailure;
		std::size_t RuntimeLookups = 0;
		std::size_t ModeReads = 0;
		std::size_t LogBytes = 0;
		std::size_t Layers = 0;
		bool ActualClassRegistered = false;
		std::size_t FailureCount = 0;
		std::size_t StoredFailures = 0;
		std::array<StartupFailureLocation, kStartupFailureSamples> Failures{};

		void RecordFailure(std::string_view diagnostic) {
			if (FailureCount == std::numeric_limits<std::size_t>::max()) {
				throw std::runtime_error("startup failure observation count overflow");
			}

			++FailureCount;
			if (StoredFailures < Failures.size()) {
				Failures[StoredFailures++] = ObserveStartupFailure(diagnostic);
			}
		}
	};

	inline void WriteStartupFailureLocation(std::ostream& output, const StartupFailureLocation& failure) {
		output << "script=" << static_cast<std::uint32_t>(failure.Script) << ",instruction=";
		if (failure.Instruction) {
			output << *failure.Instruction;
		} else {
			output << "none";
		}
		output << ",malformed=" << failure.MalformedInstruction << ",runtime_unavailable=" << failure.RuntimeUnavailable << ",mode_unavailable=" << failure.ModeUnavailable;
	}

	inline void WriteMotionStartupObservation(std::ostream& output, const MotionStartupObservation& observation) {
		if (observation.StoredFailures > observation.Failures.size() || observation.StoredFailures > observation.FailureCount) {
			throw std::runtime_error("startup failure observation exceeds its sampled bound");
		}

		std::ostringstream report;
		report << "Motion startup observation; acceptance=PENDING; requests=" << observation.Requests << ";instructions=" << observation.Instructions << ";main_status=" << observation.MainStatus << ";failures=" << observation.FailureCount << ";sampled_failures=" << observation.StoredFailures << ";runtime_lookups=" << observation.RuntimeLookups << ";mode_reads=" << observation.ModeReads << ";log_bytes=" << observation.LogBytes << ";layers=" << observation.Layers << ";actual_class=" << observation.ActualClassRegistered << '\n';
		WriteStartupFailureLocation(report, observation.MainFailure);
		report << '\n';
		for (std::size_t index = 0; index < observation.StoredFailures; ++index) {
			WriteStartupFailureLocation(report, observation.Failures[index]);
			report << '\n';
		}
		const auto text = report.str();
		if (text.size() > kStartupObservationBytes) {
			throw std::runtime_error("startup aggregate observation exceeds its output bound");
		}

		// Only aggregate counts, a fixed script category and numeric locations leave the diagnostic owner.
		output << text;
	}

} // namespace sgre::test
