#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::text {

	class TextFaceNames {
	public:
		explicit TextFaceNames(std::size_t max_names = 65536, std::size_t max_code_units = 1024 * 1024);
		TextFaceNames(const TextFaceNames&) = delete;
		TextFaceNames& operator=(const TextFaceNames&) = delete;
		[[nodiscard]] std::uint32_t Intern(std::string_view utf8_name);
		// Internal resource/layout helper: portable name input and output use Intern/Utf8Name.
		[[nodiscard]] std::uint32_t InternCodeUnits(std::span<const std::uint16_t> name);
		// Names are immutable; this span survives later interning and borrows the owner until destruction.
		[[nodiscard]] std::span<const std::uint16_t> CodeUnits(std::uint32_t index) const;
		[[nodiscard]] std::string Utf8Name(std::uint32_t index) const;
		[[nodiscard]] std::size_t Count() const noexcept;
		[[nodiscard]] std::size_t CodeUnitCount() const noexcept;

	private:
		std::deque<std::vector<std::uint16_t>> m_Names;
		std::map<std::vector<std::uint16_t>, std::uint32_t> m_NameIndices;
		std::size_t m_MaxNames;
		std::size_t m_MaxCodeUnits;
		std::size_t m_CodeUnitCount = 0;
	};

} // namespace sgre::text
