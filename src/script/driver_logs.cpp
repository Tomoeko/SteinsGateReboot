#include "sgre/script/driver_logs.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	void ScriptDriverLogger::Append(std::string_view formatted_message) {
		const auto visible = formatted_message.substr(0, formatted_message.find('\0'));
		const auto count = std::min(visible.size(), kBufferBytes - 1);
		std::array<char, kBufferBytes - 1> prepared{};
		if (count != 0) {
			std::copy_n(visible.begin(), count, prepared.begin());
		}
		// The native formatter owns separate scratch. Preserve that independence for authored aliased views too.
		auto& buffer = m_Buffers[m_Cursor];
		std::fill(buffer.begin(), buffer.end() - 1, '\0');
		if (count != 0) {
			std::copy_n(prepared.begin(), count, buffer.begin());
		}
		m_Cursor = (m_Cursor + 1) % static_cast<std::uint32_t>(kSlots);
	}

	std::uint32_t ScriptDriverLogger::Cursor() const noexcept {
		return m_Cursor;
	}

	std::span<const char, ScriptDriverLogger::kBufferBytes> ScriptDriverLogger::Buffer(std::size_t physical_slot) const {
		if (physical_slot >= kSlots) {
			throw std::runtime_error("SQDriverLogger physical slot exceeds its native 100-buffer history");
		}

		return m_Buffers[physical_slot];
	}

	std::string_view ScriptDriverLogger::MessageBytes(std::size_t physical_slot) const {
		const auto buffer = Buffer(physical_slot);
		const auto end = std::find(buffer.begin(), buffer.end(), '\0');

		return {buffer.data(), static_cast<std::size_t>(end - buffer.begin())};
	}

	ScriptDriverLogs::ScriptDriverLogs(ReportError report_error)
	    : m_ReportError(std::move(report_error)) {
		if (!m_ReportError) {
			throw std::runtime_error("SQDriver log routing requires its actual error-reporting service");
		}
	}

	void ScriptDriverLogs::Initialize() {
		if (m_Closed || m_Print || m_Error) {
			throw std::runtime_error("SQDriver histories require one native setup before logger retirement");
		}

		auto print = std::make_unique<ScriptDriverLogger>();
		auto error = std::make_unique<ScriptDriverLogger>();
		m_Print = std::move(print);
		m_Error = std::move(error);
	}

	void ScriptDriverLogs::Close() noexcept {
		m_Closed = true;
		m_Error.reset();
		m_Print.reset();
	}

	bool ScriptDriverLogs::IsInitialized() const noexcept {
		return m_Print && m_Error;
	}

	const ScriptDriverLogger& ScriptDriverLogs::Print() const {
		if (!m_Print) {
			throw std::runtime_error("SQDriver print history has no live native logger");
		}

		return *m_Print;
	}

	const ScriptDriverLogger& ScriptDriverLogs::Error() const {
		if (!m_Error) {
			throw std::runtime_error("SQDriver error history has no live native logger");
		}

		return *m_Error;
	}

	void ScriptDriverLogs::EmitFormatted(std::string_view formatted_message, bool error) {
		if (m_Closed) {
			throw std::runtime_error("SQDriver log routing has retired");
		}

		const auto visible = formatted_message.substr(0, formatted_message.find('\0'));
		if (error) {
			m_ReportError(visible);
			if (m_Error) {
				m_Error->Append(visible);
			}
		} else if (m_Print) {
			m_Print->Append(visible);
		}
	}

	NativePrintCallback ScriptDriverLogs::OutputCallback() {
		return [this](std::string_view formatted_message, bool error) {
			EmitFormatted(formatted_message, error);
		};
	}

} // namespace sgre::script
