#include "sgre/io/file_reader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class TemporaryFile {
public:
	TemporaryFile() {
		const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
		m_Path = std::filesystem::temp_directory_path() / ("sgre-reader-" + std::to_string(suffix));
		std::ofstream stream(m_Path, std::ios::binary);
		stream << "abcdef";
		if (!stream) {
			throw std::runtime_error("cannot create test input");
		}
	}

	~TemporaryFile() {
		std::error_code error;
		std::filesystem::remove(m_Path, error);
	}

	TemporaryFile(const TemporaryFile&) = delete;
	TemporaryFile& operator=(const TemporaryFile&) = delete;

	const std::filesystem::path& Path() const noexcept {
		return m_Path;
	}

private:
	std::filesystem::path m_Path;
};

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestReads() {
	const TemporaryFile input;
	sgre::io::FileReader reader(input.Path());
	Require(reader.Size() == 6, "incorrect input size");
	Require(reader.Read(2, 3) == std::vector<std::uint8_t>({'c', 'd', 'e'}), "incorrect bounded read");
	Require(reader.Read(6, 0).empty(), "empty end read must succeed");
	Require(reader.Read(0, 1) == std::vector<std::uint8_t>({'a'}), "reader must support seeking backwards");

	bool rejected = false;
	try {
		static_cast<void>(reader.Read(5, 2));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "out-of-bounds read must fail");
	rejected = false;
	try {
		static_cast<void>(reader.Read(UINT64_MAX, 0));
	} catch (const std::runtime_error&) {
		rejected = true;
	}

	Require(rejected, "invalid zero-length offset must fail");
}

int main() {
	try {
		TestReads();
		std::cout << "file reader tests passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
