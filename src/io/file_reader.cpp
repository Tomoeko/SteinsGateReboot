#include "sgre/io/file_reader.hpp"

#include <limits>
#include <stdexcept>

namespace sgre::io {

	FileReader::FileReader(const std::filesystem::path& path) : m_Stream(path, std::ios::binary) {
		if (!m_Stream) {
			throw std::runtime_error("cannot open input: " + path.filename().string());
		}

		m_Stream.seekg(0, std::ios::end);
		const std::streamoff end = m_Stream.tellg();
		if (end < 0) {
			throw std::runtime_error("cannot determine input size");
		}

		m_Size = static_cast<std::uint64_t>(end);
	}

	std::uint64_t FileReader::Size() const noexcept {
		return m_Size;
	}

	std::vector<std::uint8_t> FileReader::Read(std::uint64_t offset, std::size_t byte_count) {
		if (offset > m_Size || byte_count > m_Size - offset) {
			throw std::runtime_error("file range exceeds input");
		}

		if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) || byte_count > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
			throw std::runtime_error("file range exceeds stream addressing limits");
		}

		std::vector<std::uint8_t> bytes(byte_count);
		if (byte_count == 0) {
			return bytes;
		}

		m_Stream.clear();
		m_Stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		m_Stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byte_count));
		if (!m_Stream || m_Stream.gcount() != static_cast<std::streamsize>(byte_count)) {
			throw std::runtime_error("input read failed or input changed during inspection");
		}

		return bytes;
	}

} // namespace sgre::io
