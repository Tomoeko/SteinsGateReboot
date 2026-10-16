#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace sgre::io {

	class FileReader {
	public:
		explicit FileReader(const std::filesystem::path& path);
		[[nodiscard]] std::uint64_t Size() const noexcept;
		[[nodiscard]] std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t byte_count);

	private:
		std::ifstream m_Stream;
		std::uint64_t m_Size = 0;
	};

} // namespace sgre::io
