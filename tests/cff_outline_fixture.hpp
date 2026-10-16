#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace sgre::tests {

	inline void WriteOutlineFixtureWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
		bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
		bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
	}

	inline void WriteOutlineFixtureDoubleWord(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
		WriteOutlineFixtureWord(bytes, offset, static_cast<std::uint16_t>(value >> 16));
		WriteOutlineFixtureWord(bytes, offset + 2, static_cast<std::uint16_t>(value));
	}

	inline void AppendOutlineFixtureNumber(std::vector<std::uint8_t>& bytes, double value) {
		if (value == std::floor(value) && value >= -107 && value <= 107) {
			bytes.push_back(static_cast<std::uint8_t>(value + 139));
		} else if (value == std::floor(value) && value >= 108 && value <= 1131) {
			const auto encoded = static_cast<std::uint32_t>(value) - 108;
			bytes.push_back(static_cast<std::uint8_t>(247 + encoded / 256));
			bytes.push_back(static_cast<std::uint8_t>(encoded % 256));
		} else if (value == std::floor(value) && value <= -108 && value >= -1131) {
			const auto encoded = static_cast<std::uint32_t>(-value) - 108;
			bytes.push_back(static_cast<std::uint8_t>(251 + encoded / 256));
			bytes.push_back(static_cast<std::uint8_t>(encoded % 256));
		} else if (value == std::floor(value) && value >= -32768 && value <= 32767) {
			bytes.push_back(28);
			const auto offset = bytes.size();
			bytes.resize(offset + 2);
			WriteOutlineFixtureWord(bytes, offset, std::bit_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
		} else {
			if (!std::isfinite(value) || value < -32768 || value > 32767 || value * 65536 != std::floor(value * 65536)) {
				throw std::runtime_error("invalid synthetic Type 2 fixed operand");
			}
			bytes.push_back(255);
			const auto offset = bytes.size();
			bytes.resize(offset + 4);
			WriteOutlineFixtureDoubleWord(bytes, offset, std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(value * 65536)));
		}
	}

	inline void AppendOutlineFixtureOperation(std::vector<std::uint8_t>& program, std::initializer_list<double> operands, std::uint8_t operation) {
		for (const auto operand : operands) {
			AppendOutlineFixtureNumber(program, operand);
		}
		program.push_back(operation);
	}

	inline std::vector<std::uint8_t> MakeOutlineFixtureIndex(const std::vector<std::vector<std::uint8_t>>& items) {
		std::vector<std::uint8_t> bytes(items.empty() ? 2 : 3 + (items.size() + 1) * 4);
		WriteOutlineFixtureWord(bytes, 0, static_cast<std::uint16_t>(items.size()));
		if (items.empty()) {
			return bytes;
		}

		bytes[2] = 4;
		std::uint32_t offset = 1;
		for (std::size_t index = 0; index <= items.size(); ++index) {
			WriteOutlineFixtureDoubleWord(bytes, 3 + index * 4, offset);
			if (index != items.size()) {
				offset += static_cast<std::uint32_t>(items[index].size());
			}
		}
		for (const auto& item : items) {
			bytes.insert(bytes.end(), item.begin(), item.end());
		}

		return bytes;
	}

	inline std::vector<std::uint8_t> MakeCffOutlineFixture(const std::vector<std::uint8_t>& program, const std::vector<std::vector<std::uint8_t>>& local_subroutines = {}, const std::vector<std::vector<std::uint8_t>>& global_subroutines = {}) {
		std::vector<std::uint8_t> cff{1, 0, 4, 4};
		const auto append = [&](const auto& bytes) {
			cff.insert(cff.end(), bytes.begin(), bytes.end());
		};
		append(MakeOutlineFixtureIndex({{u'T'}}));
		std::vector<std::uint8_t> top{29, 0, 0, 0, 0, 17, 29, 0, 0, 0, 18, 29, 0, 0, 0, 0, 18};
		const auto top_offset = cff.size() + 11;
		append(MakeOutlineFixtureIndex({top}));
		append(MakeOutlineFixtureIndex({}));
		append(MakeOutlineFixtureIndex(global_subroutines));
		WriteOutlineFixtureDoubleWord(cff, top_offset + 1, static_cast<std::uint32_t>(cff.size()));
		append(MakeOutlineFixtureIndex({program, {14}, {14}}));
		WriteOutlineFixtureDoubleWord(cff, top_offset + 12, static_cast<std::uint32_t>(cff.size()));
		append(std::array<std::uint8_t, 18>{29, 0, 0, 0, 100, 20, 29, 0, 0, 0, 20, 21, 29, 0, 0, 0, 18, 19});
		append(MakeOutlineFixtureIndex(local_subroutines));

		std::map<std::string, std::vector<std::uint8_t>> tables;
		tables["CFF "] = std::move(cff);
		tables["head"].resize(54);
		WriteOutlineFixtureDoubleWord(tables["head"], 0, 0x00010000);
		WriteOutlineFixtureDoubleWord(tables["head"], 12, 0x5f0f3cf5);
		WriteOutlineFixtureWord(tables["head"], 18, 1000);
		tables["hhea"].resize(36);
		WriteOutlineFixtureDoubleWord(tables["hhea"], 0, 0x00010000);
		WriteOutlineFixtureWord(tables["hhea"], 34, 1);
		tables["hmtx"] = {0, 100, 0, 0, 0, 0, 0, 0};
		tables["maxp"] = {0, 0, 0x50, 0, 0, 3};
		tables["cmap"].resize(40);
		WriteOutlineFixtureWord(tables["cmap"], 2, 1);
		WriteOutlineFixtureWord(tables["cmap"], 6, 3);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 8, 12);
		WriteOutlineFixtureWord(tables["cmap"], 12, 12);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 16, 28);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 24, 1);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 28, 0x41);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 32, 0x42);
		WriteOutlineFixtureDoubleWord(tables["cmap"], 36, 1);
		std::vector<std::uint8_t> bytes(12 + tables.size() * 16);
		WriteOutlineFixtureDoubleWord(bytes, 0, 0x4f54544f);
		WriteOutlineFixtureWord(bytes, 4, static_cast<std::uint16_t>(tables.size()));
		std::size_t index = 0;
		for (const auto& [tag, table] : tables) {
			while (bytes.size() % 4 != 0) {
				bytes.push_back(0);
			}
			const auto record = 12 + index++ * 16;
			std::copy(tag.begin(), tag.end(), bytes.begin() + static_cast<std::ptrdiff_t>(record));
			WriteOutlineFixtureDoubleWord(bytes, record + 8, static_cast<std::uint32_t>(bytes.size()));
			WriteOutlineFixtureDoubleWord(bytes, record + 12, static_cast<std::uint32_t>(table.size()));
			bytes.insert(bytes.end(), table.begin(), table.end());
		}

		return bytes;
	}

} // namespace sgre::tests
