#include "sgre/resource/resident_font.hpp"

#include "sgre/io/file_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace sgre::resource {

	static std::uint16_t ResidentU16(std::span<const std::uint8_t> bytes, std::size_t offset) {
		if (offset > bytes.size() || 2 > bytes.size() - offset) {
			throw std::runtime_error("resident font PE uint16 exceeds its bounded header");
		}

		return static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
	}

	static std::uint32_t ResidentU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
		if (offset > bytes.size() || 4 > bytes.size() - offset) {
			throw std::runtime_error("resident font PE uint32 exceeds its bounded header");
		}
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index) {
			value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
		}

		return value;
	}

	static std::uint64_t ResidentU64(std::span<const std::uint8_t> bytes, std::size_t offset) {
		if (offset > bytes.size() || 8 > bytes.size() - offset) {
			throw std::runtime_error("resident font PE uint64 exceeds its bounded header");
		}
		std::uint64_t value = 0;
		for (std::size_t index = 0; index < 8; ++index) {
			value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
		}

		return value;
	}

	ResidentMeterFontInput ReadResidentMeterFont(const std::filesystem::path& executable) {
		static constexpr std::uint64_t kResidentAddress = 0x14096c8b0;
		static constexpr std::size_t kResidentBytes = 72704;
		sgre::io::FileReader reader(executable);
		const auto dos = reader.Read(0, 64);
		if (ResidentU16(dos, 0) != 0x5a4d) {
			throw std::runtime_error("resident font input is not the original PE executable");
		}
		const auto pe_offset = static_cast<std::uint64_t>(ResidentU32(dos, 60));
		const auto coff = reader.Read(pe_offset, 24);
		const auto section_count = ResidentU16(coff, 6);
		const auto optional_bytes = ResidentU16(coff, 20);
		if (ResidentU32(coff, 0) != 0x4550 || ResidentU16(coff, 4) != 0x8664 || section_count == 0 || section_count > 96 || optional_bytes < 112 || optional_bytes > 4096) {
			throw std::runtime_error("resident font input requires bounded original PE64 x86 headers");
		}
		const auto optional_offset = pe_offset + 24;
		const auto optional = reader.Read(optional_offset, optional_bytes);
		const auto image_base = ResidentU64(optional, 24);
		if (ResidentU16(optional, 0) != 0x20b || image_base != 0x140000000 || kResidentAddress < image_base) {
			throw std::runtime_error("resident font virtual address does not belong to the original PE image");
		}
		const auto resident_rva = kResidentAddress - image_base;
		const auto section_table = reader.Read(optional_offset + optional_bytes, static_cast<std::size_t>(section_count) * 40);
		std::uint64_t file_offset = 0;
		std::size_t matches = 0;
		for (std::size_t index = 0; index < section_count; ++index) {
			const auto section = std::span<const std::uint8_t>(section_table).subspan(index * 40, 40);
			const auto virtual_bytes = ResidentU32(section, 8);
			const auto virtual_rva = ResidentU32(section, 12);
			const auto raw_bytes = ResidentU32(section, 16);
			const auto raw_offset = ResidentU32(section, 20);
			if (resident_rva < virtual_rva) {
				continue;
			}
			const auto delta = resident_rva - virtual_rva;
			if (delta > virtual_bytes || kResidentBytes > virtual_bytes - delta || delta > raw_bytes || kResidentBytes > raw_bytes - delta) {
				continue;
			}
			if (raw_offset > reader.Size() || raw_bytes > reader.Size() - raw_offset) {
				throw std::runtime_error("resident font PE section raw range exceeds the original file");
			}

			file_offset = raw_offset + delta;
			++matches;
		}
		if (matches != 1) {
			throw std::runtime_error("resident font original virtual range has no unique file-backed PE section");
		}

		ResidentMeterFontInput result;
		result.Backing = std::make_shared<const std::vector<std::uint8_t>>(reader.Read(file_offset, kResidentBytes));
		result.Document = std::make_shared<const sgre::resource::PsbDocument>(sgre::resource::ReadPsbMetadata(*result.Backing, result.Backing->size()));
		result.FileOffset = file_offset;

		return result;
	}

} // namespace sgre::resource
