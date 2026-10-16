#include "sgre/app/launch_configuration.hpp"

#include "sgre/core/md5.hpp"
#include "sgre/io/file_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sgre::app {

	static std::uint32_t PeWord(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
		if (count > 4 || offset > bytes.size() || count > bytes.size() - offset) {
			throw std::runtime_error("archive seed PE field exceeds its actual input");
		}
		std::uint32_t result = 0;
		for (std::size_t index = 0; index < count; ++index) {
			result |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
		}

		return result;
	}

	static std::uint64_t MappedOffset(std::span<const std::uint8_t> sections, std::uint32_t rva, std::uint32_t length, std::uint64_t file_bytes) {
		std::optional<std::uint64_t> result;
		for (std::size_t offset = 0; offset < sections.size(); offset += 40) {
			const auto address = PeWord(sections, offset + 12, 4);
			const auto raw_bytes = PeWord(sections, offset + 16, 4);
			const auto raw_offset = PeWord(sections, offset + 20, 4);
			if (rva < address || rva - address > raw_bytes || length > raw_bytes - (rva - address)) {
				continue;
			}
			const auto mapped = static_cast<std::uint64_t>(raw_offset) + rva - address;
			if (result || mapped > file_bytes || length > file_bytes - mapped) {
				throw std::runtime_error("archive seed PE mapping is ambiguous or exceeds the executable");
			}
			result = mapped;
		}
		if (!result) {
			throw std::runtime_error("archive seed source has no actual PE raw section");
		}

		return *result;
	}

	std::string ReadOriginalArchiveSeed(const std::filesystem::path& executable) {
		io::FileReader input(executable);
		const auto dos = input.Read(0, 64);
		const auto pe_offset = PeWord(dos, 60, 4);
		if (PeWord(dos, 0, 2) != 0x5a4d || pe_offset < 64 || pe_offset > 1024 * 1024) {
			throw std::runtime_error("archive seed discovery requires the supplied bounded PE executable");
		}
		const auto pe = input.Read(pe_offset, 24);
		const auto count = PeWord(pe, 6, 2);
		const auto optional_bytes = PeWord(pe, 20, 2);
		if (PeWord(pe, 0, 4) != 0x4550 || PeWord(pe, 4, 2) != 0x8664 || count == 0 || count > 32 || optional_bytes < 112 || optional_bytes > 4096) {
			throw std::runtime_error("archive seed executable is not the supported AMD64 PE32+ layout");
		}
		const auto optional = input.Read(static_cast<std::uint64_t>(pe_offset) + 24, optional_bytes);
		if (PeWord(optional, 0, 2) != 0x20b || PeWord(optional, 24, 4) != 0x40000000 || PeWord(optional, 28, 4) != 1) {
			throw std::runtime_error("archive seed executable image base/layout differs from the recovered initializer");
		}
		const auto sections = input.Read(static_cast<std::uint64_t>(pe_offset) + 24 + optional_bytes, static_cast<std::size_t>(count) * 40);
		// Original0483DA constructs the narrow literal;0483EA stores it with period0x83.
		// These exact recovered bytes and literal digest scope discovery without shipping the key itself.
		static constexpr std::array<std::uint8_t, 32> kInitializer = {0x15, 0xdb, 0x6d, 0x57, 0x00, 0x48, 0x8d, 0x4c, 0x24, 0x70, 0xe8, 0x11, 0x14, 0xff, 0xff, 0x90, 0xba, 0x83, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x4c, 0x24, 0x70, 0xe8, 0xb1, 0xf3, 0x48, 0x00, 0x90};
		static constexpr std::array<std::uint8_t, 16> kSeedMd5 = {0xc2, 0xe1, 0x8b, 0xf6, 0x3a, 0x17, 0x14, 0x06, 0xb9, 0xf5, 0x77, 0xd4, 0x04, 0x30, 0x42, 0x13};
		const auto initializer = input.Read(MappedOffset(sections, 0x483d0, 32, input.Size()), 32);
		if (!std::equal(initializer.begin(), initializer.end(), kInitializer.begin())) {
			throw std::runtime_error("archive seed initializer differs; supply an explicit verified seed_file for this executable");
		}
		const auto literal = input.Read(MappedOffset(sections, 0x5bf1b0, 14, input.Size()), 14);
		if (literal.back() != 0 || std::any_of(literal.begin(), literal.end() - 1, [](std::uint8_t byte) {
			    return byte < 0x20 || byte > 0x7e;
		    })) {
			throw std::runtime_error("archive seed initializer literal is not its recovered13-byte ASCII string");
		}
		core::Md5 digest;
		digest.Update(std::span<const std::uint8_t>(literal).first(13));
		if (digest.Finalize() != kSeedMd5) {
			throw std::runtime_error("archive seed literal differs from the supplied executable's verified content");
		}

		return std::string(literal.begin(), literal.end() - 1);
	}

} // namespace sgre::app
