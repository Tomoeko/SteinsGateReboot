#include "sgre/resource/resource_cipher.hpp"

#include "sgre/core/md5.hpp"

#include <array>
#include <bit>
#include <stdexcept>
#include <string>

namespace sgre::resource {

	static std::uint32_t ReadLittleEndianWord(std::span<const std::uint8_t> bytes, std::size_t offset) {
		return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
	}

	class ResourceMt19937 {
	public:
		explicit ResourceMt19937(const std::array<std::uint32_t, 4>& seed) {
			m_State[0] = 19650218;

			for (std::size_t index = 1; index < m_State.size(); ++index) {
				m_State[index] = 1812433253U * (m_State[index - 1] ^ (m_State[index - 1] >> 30)) + static_cast<std::uint32_t>(index);
			}

			std::size_t state_index = 1;
			std::size_t seed_index = 0;

			for (std::size_t count = 0; count < m_State.size(); ++count) {
				m_State[state_index] = (m_State[state_index] ^ ((m_State[state_index - 1] ^ (m_State[state_index - 1] >> 30)) * 1664525U)) + seed[seed_index] + static_cast<std::uint32_t>(seed_index);
				AdvanceSeedIndex(state_index);
				seed_index = (seed_index + 1) % seed.size();
			}

			for (std::size_t count = 0; count < m_State.size() - 1; ++count) {
				m_State[state_index] = (m_State[state_index] ^ ((m_State[state_index - 1] ^ (m_State[state_index - 1] >> 30)) * 1566083941U)) - static_cast<std::uint32_t>(state_index);
				AdvanceSeedIndex(state_index);
			}

			m_State[0] = 0x80000000;
		}

		std::uint32_t NextWord() {
			if (m_Index == m_State.size()) {
				for (std::size_t index = 0; index < m_State.size(); ++index) {
					const std::uint32_t joined = (m_State[index] & 0x80000000U) | (m_State[(index + 1) % m_State.size()] & 0x7fffffffU);
					m_State[index] = m_State[(index + 397) % m_State.size()] ^ (joined >> 1) ^ ((joined & 1U) == 0 ? 0U : 0x9908b0dfU);
				}

				m_Index = 0;
			}

			std::uint32_t word = m_State[m_Index++];
			word ^= word >> 11;
			word ^= (word << 7) & 0x9d2c5680U;
			word ^= (word << 15) & 0xefc60000U;
			word ^= word >> 18;

			return word;
		}

	private:
		void AdvanceSeedIndex(std::size_t& index) {
			++index;

			if (index == m_State.size()) {
				m_State[0] = m_State.back();
				index = 1;
			}
		}

		std::array<std::uint32_t, 624> m_State{};
		std::size_t m_Index = m_State.size();
	};

	static std::string NormalizeBasename(std::string_view filename) {
		const std::size_t separator = filename.find_last_of("/\\");

		if (separator != std::string_view::npos) {
			filename.remove_prefix(separator + 1);
		}

		std::string basename(filename);

		for (char& character : basename) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character + ('a' - 'A'));
			}
		}

		return basename;
	}

	ResourceCipher::ResourceCipher(std::string_view filename, std::string_view seed, std::size_t period) {
		if (period == 0 || period > 1024 * 1024) {
			throw std::runtime_error("resource cipher period must be between one byte and 1 MiB");
		}

		core::Md5 digest;
		digest.Update(seed);
		digest.Update(NormalizeBasename(filename));
		const auto bytes = digest.Finalize();
		std::array<std::uint32_t, 4> words;
		for (std::size_t index = 0; index < words.size(); ++index) {
			words[index] = ReadLittleEndianWord(bytes, index * 4);
		}
		ResourceMt19937 random(words);
		m_KeyStream.resize(period);
		std::uint32_t word = 0;
		for (std::size_t index = 0; index < period; ++index) {
			if (index % 4 == 0) {
				word = random.NextWord();
			}

			m_KeyStream[index] = static_cast<std::uint8_t>(word >> ((index % 4) * 8));
		}
	}

	void ResourceCipher::Transform(std::span<std::uint8_t> bytes, std::uint64_t stream_offset) const {
		if (m_KeyStream.empty()) {
			throw std::logic_error("resource cipher has no key stream");
		}

		std::size_t key_index = static_cast<std::size_t>(stream_offset % m_KeyStream.size());
		for (auto& byte : bytes) {
			byte ^= m_KeyStream[key_index];
			++key_index;
			if (key_index == m_KeyStream.size()) {
				key_index = 0;
			}
		}
	}

} // namespace sgre::resource
