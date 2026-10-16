#pragma once

#include "sgre/resource/sfnt.hpp"

#include "vector_font_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vector_run_test {

	inline void Word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
		bytes.push_back(static_cast<std::uint8_t>(word >> 8));
		bytes.push_back(static_cast<std::uint8_t>(word));
	}

	inline std::vector<std::uint8_t> Words(std::initializer_list<std::uint16_t> words) {
		std::vector<std::uint8_t> bytes;
		for (const auto word : words) {
			Word(bytes, word);
		}
		return bytes;
	}

	inline std::vector<std::uint8_t> Layout(bool positioning, bool malformed = false, std::uint16_t flags = 0) {
		// Independent latn/default liga/kern metadata and actual one-lookup payload.
		auto bytes = Words({1, 0, 10, 30, 44, 1});
		bytes.insert(bytes.end(), {'l', 'a', 't', 'n'});
		const auto script = Words({8, 4, 0, 0, 0xffff, 1, 0, 1});
		bytes.insert(bytes.end(), script.begin(), script.end());
		const std::string_view feature = positioning ? "kern" : "liga";
		bytes.insert(bytes.end(), feature.begin(), feature.end());
		const auto feature_table = Words({8, 0, 1, 0, 1, 4});
		bytes.insert(bytes.end(), feature_table.begin(), feature_table.end());
		Word(bytes, positioning ? 2 : 4);
		const auto lookup = Words({flags, 1, 8});
		bytes.insert(bytes.end(), lookup.begin(), lookup.end());
		if (positioning) {
			// Coverage A, pair B, XAdvance−70; no guessed pixel-size scaling.
			const auto pair = Words({1, 12, 4, 0, 1, 18, 1, 1, 1, 1, 2, 0xffba});
			bytes.insert(bytes.end(), pair.begin(), pair.end());
		} else {
			// A+B→B. Counts and replacement are independent of cmap/metric construction.
			const auto ligature = Words({1, 8, 1, 14, 1, 1, 1, 1, 4, static_cast<std::uint16_t>(malformed ? 3 : 2), 2, 2});
			bytes.insert(bytes.end(), ligature.begin(), ligature.end());
		}
		return bytes;
	}

	inline std::shared_ptr<const std::vector<std::uint8_t>> Font(bool ligature, bool pair, bool malformed = false, std::uint16_t flags = 0) {
		const auto original = vector_font_test::MakeFont();
		const sgre::resource::SfntFont reader(original);
		std::map<std::string, std::vector<std::uint8_t>> tables;
		for (const auto& item : reader.Tables()) {
			const auto bytes = *reader.FindTable(item.Tag);
			tables[item.Tag] = {bytes.begin(), bytes.end()};
		}
		if (ligature) {
			tables["GSUB"] = Layout(false, malformed, flags);
		}
		if (pair) {
			tables["GPOS"] = Layout(true);
		}
		std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
		vector_font_test::WriteDoubleWord(bytes, 0, 0x4f54544f);
		vector_font_test::WriteWord(bytes, 4, static_cast<std::uint16_t>(tables.size()));
		std::size_t index = 0;
		for (const auto& [tag, table] : tables) {
			while (bytes.size() % 4 != 0) {
				bytes.push_back(0);
			}
			const auto record = 12 + index++ * 16;
			std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
			vector_font_test::WriteDoubleWord(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
			vector_font_test::WriteDoubleWord(bytes, record + 12, static_cast<std::uint32_t>(table.size()));
			bytes.insert(bytes.end(), table.begin(), table.end());
		}
		return std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
	}

} // namespace vector_run_test
