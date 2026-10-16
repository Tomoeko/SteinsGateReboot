#include "sgre/text/face_names.hpp"

#include "sgre/text/encoding.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sgre::text {

	static void AppendFaceScalar(std::string& output, std::uint32_t scalar) {
		if (scalar < 0x80) {
			output.push_back(static_cast<char>(scalar));
		} else if (scalar < 0x800) {
			output.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
			output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		} else if (scalar < 0x10000) {
			output.push_back(static_cast<char>(0xe0 | (scalar >> 12)));
			output.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		} else {
			output.push_back(static_cast<char>(0xf0 | (scalar >> 18)));
			output.push_back(static_cast<char>(0x80 | ((scalar >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((scalar >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		}
	}

	TextFaceNames::TextFaceNames(std::size_t max_names, std::size_t max_code_units) : m_MaxNames(max_names), m_MaxCodeUnits(max_code_units) {
		if (max_names < 2 || max_names > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) || max_code_units < 10) {
			throw std::runtime_error("text face-name limits cannot contain the native normal/ruby defaults");
		}

		(void)Intern("normal");
		(void)Intern("ruby");
	}

	std::uint32_t TextFaceNames::Intern(std::string_view utf8_name) {
		const auto name = utf8_name.substr(0, utf8_name.find('\0'));
		const auto units = DecodeTextCodeUnits(name);

		return InternCodeUnits(units);
	}

	std::uint32_t TextFaceNames::InternCodeUnits(std::span<const std::uint16_t> name) {
		const auto terminator = std::find(name.begin(), name.end(), 0);
		const auto length = static_cast<std::size_t>(terminator - name.begin());
		if (length > m_MaxCodeUnits) {
			throw std::runtime_error("text face name exceeds code-unit limit");
		}

		const std::vector<std::uint16_t> key(name.begin(), terminator);
		if (const auto found = m_NameIndices.find(key); found != m_NameIndices.end()) {
			return found->second;
		}
		if (m_Names.size() == m_MaxNames || key.size() > m_MaxCodeUnits - m_CodeUnitCount) {
			throw std::runtime_error("text face-name owner exceeds name/code-unit limit");
		}

		const auto index = static_cast<std::uint32_t>(m_Names.size());
		m_Names.push_back(key);
		try {
			m_NameIndices.emplace(key, index);
		} catch (...) {
			m_Names.pop_back();
			throw;
		}

		m_CodeUnitCount += key.size();

		return index;
	}

	std::span<const std::uint16_t> TextFaceNames::CodeUnits(std::uint32_t index) const {
		if (index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
			throw std::runtime_error("negative native text face index would access before the name vector");
		}
		if (index >= m_Names.size()) {
			return {};
		}

		return m_Names[index];
	}

	std::string TextFaceNames::Utf8Name(std::uint32_t index) const {
		const auto units = CodeUnits(index);
		std::string result;
		for (std::size_t offset = 0; offset < units.size(); ++offset) {
			const auto first = units[offset];
			std::uint32_t scalar = first;
			if (first >= 0xd800 && first <= 0xdbff) {
				if (offset + 1 == units.size() || units[offset + 1] < 0xdc00 || units[offset + 1] > 0xdfff) {
					throw std::runtime_error("text face name has an unpaired UTF-16 high surrogate");
				}

				const auto second = units[++offset];
				scalar = 0x10000 + ((std::uint32_t(first) - 0xd800) << 10) + (std::uint32_t(second) - 0xdc00);
			} else if (first >= 0xdc00 && first <= 0xdfff) {
				throw std::runtime_error("text face name has an unpaired UTF-16 low surrogate");
			}

			AppendFaceScalar(result, scalar);
		}

		return result;
	}

	std::size_t TextFaceNames::Count() const noexcept {
		return m_Names.size();
	}

	std::size_t TextFaceNames::CodeUnitCount() const noexcept {
		return m_CodeUnitCount;
	}

} // namespace sgre::text
