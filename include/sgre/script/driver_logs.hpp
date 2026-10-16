#pragma once

#include "sgre/script/native_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace sgre::script {

	class ScriptDriverLogger {
	public:
		static constexpr std::size_t kSlots = 100;
		static constexpr std::size_t kBufferBytes = 256;
		void Append(std::string_view formatted_message);
		[[nodiscard]] std::uint32_t Cursor() const noexcept;
		// Native strncpy stores bytes; truncation may split a UTF-8 sequence. This is not a normalized display string.
		[[nodiscard]] std::span<const char, kBufferBytes> Buffer(std::size_t physical_slot) const;
		[[nodiscard]] std::string_view MessageBytes(std::size_t physical_slot) const;

	private:
		std::array<std::array<char, kBufferBytes>, kSlots> m_Buffers{};
		std::uint32_t m_Cursor = 0;
	};

	class ScriptDriverLogs {
	public:
		using ReportError = std::function<void(std::string_view)>;
		// The actual native/portable platform error reporter is required; no empty diagnostic callback is substituted.
		explicit ScriptDriverLogs(ReportError report_error);
		ScriptDriverLogs(const ScriptDriverLogs&) = delete;
		ScriptDriverLogs& operator=(const ScriptDriverLogs&) = delete;
		ScriptDriverLogs(ScriptDriverLogs&&) = delete;
		ScriptDriverLogs& operator=(ScriptDriverLogs&&) = delete;

		// Native SQDriver scene setup creates print then error histories after its actual task graph.
		void Initialize();
		// Called after the actual VM/native print callbacks retire; native driver drops error history before print.
		void Close() noexcept;
		[[nodiscard]] bool IsInitialized() const noexcept;
		[[nodiscard]] const ScriptDriverLogger& Print() const;
		[[nodiscard]] const ScriptDriverLogger& Error() const;
		// Formatted UTF-8 input reaches the actual error reporter before the current error logger is consulted.
		// Before Initialize, native normal history is discarded and errors still reach the actual reporting service.
		void EmitFormatted(std::string_view formatted_message, bool error);
		// This owner/report service outlive every VM/native printing closure holding this borrowed callback.
		[[nodiscard]] NativePrintCallback OutputCallback();

	private:
		ReportError m_ReportError;
		std::unique_ptr<ScriptDriverLogger> m_Print;
		std::unique_ptr<ScriptDriverLogger> m_Error;
		bool m_Closed = false;
	};

} // namespace sgre::script
