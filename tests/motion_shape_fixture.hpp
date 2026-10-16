#pragma once

#include "sgre/resource/psb.hpp"

#include <bit>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace motion_shape_fixture {
	using Bytes = std::vector<std::uint8_t>;

	template <typename Value>
	static sgre::resource::PsbValue Atom(Value value) {
		sgre::resource::PsbValue result;
		result.Data = std::move(value);
		return result;
	}

	static void AppendWord(Bytes& bytes, std::uint64_t word, std::size_t width = 4) {
		for (std::size_t index = 0; index != width; ++index) {
			bytes.push_back(static_cast<std::uint8_t>(word >> (index * 8)));
		}
	}

	static void WriteWord(Bytes& bytes, std::size_t offset, std::uint32_t word) {
		for (std::size_t index = 0; index != 4; ++index) {
			bytes.at(offset + index) = static_cast<std::uint8_t>(word >> (index * 8));
		}
	}

	static void AppendArray(Bytes& bytes, const std::vector<std::uint32_t>& words) {
		bytes.push_back(16);
		AppendWord(bytes, words.size());
		bytes.push_back(16);
		for (const auto word : words) {
			AppendWord(bytes, word);
		}
	}

	// Independent test-only PSB3 encoder: actual bytes cross the public reader; no populated Document shortcuts.
	class Writer {
	public:
		Bytes Serialize(const sgre::resource::PsbValue& root) {
			Collect(root);
			std::uint32_t index = 0;
			for (auto& [name, id] : m_Names) {
				id = index++;
			}
			index = 0;
			for (auto& [text, id] : m_Strings) {
				id = index++;
			}
			Bytes result(44, 0);
			result[0] = 'P';
			result[1] = 'S';
			result[2] = 'B';
			result[4] = 3;
			WriteWord(result, 8, 44);
			WriteWord(result, 12, static_cast<std::uint32_t>(result.size()));
			Names(result);
			WriteWord(result, 36, static_cast<std::uint32_t>(result.size()));
			const auto values = Encode(root);
			result.insert(result.end(), values.begin(), values.end());
			WriteWord(result, 16, static_cast<std::uint32_t>(result.size()));
			std::vector<std::uint32_t> offsets;
			Bytes strings;
			for (const auto& [text, id] : m_Strings) {
				static_cast<void>(id);
				offsets.push_back(static_cast<std::uint32_t>(strings.size()));
				strings.insert(strings.end(), text.begin(), text.end());
				strings.push_back(0);
			}
			AppendArray(result, offsets);
			WriteWord(result, 20, static_cast<std::uint32_t>(result.size()));
			result.insert(result.end(), strings.begin(), strings.end());
			WriteWord(result, 24, static_cast<std::uint32_t>(result.size()));
			AppendArray(result, {});
			WriteWord(result, 28, static_cast<std::uint32_t>(result.size()));
			AppendArray(result, {});
			WriteWord(result, 32, static_cast<std::uint32_t>(result.size()));
			std::uint32_t first = 1;
			std::uint32_t second = 0;
			for (std::size_t byte = 8; byte != 40; ++byte) {
				first = (first + result[byte]) % 65521;
				second = (second + first) % 65521;
			}
			WriteWord(result, 40, (second << 16) | first);
			if (result.size() > 2 * 1024 * 1024) {
				throw std::runtime_error("shape fixture PSB exceeds bounded authored metadata");
			}
			return result;
		}

	private:
		void Collect(const sgre::resource::PsbValue& value) {
			if (const auto* text = std::get_if<std::string>(&value.Data)) {
				m_Strings.emplace(*text, 0);
			} else if (const auto* array = std::get_if<sgre::resource::PsbArray>(&value.Data)) {
				for (const auto& child : *array) {
					Collect(child);
				}
			} else if (const auto* object = std::get_if<sgre::resource::PsbObject>(&value.Data)) {
				for (const auto& [name, child] : *object) {
					m_Names.emplace(name, 0);
					Collect(child);
				}
			}
		}

		void Names(Bytes& bytes) const {
			std::vector<std::uint32_t> bases(1);
			std::vector<std::uint32_t> parents(1);
			std::vector<std::uint32_t> leaves;
			std::uint32_t block = 0;
			const auto allocate_base = [&](std::uint32_t node) {
				if (bases[node] == 0) {
					const auto base = ++block * 256;
					bases.resize(base + 256);
					parents.resize(base + 256);
					bases[node] = base;
				}
				return bases[node];
			};
			for (const auto& [name, id] : m_Names) {
				static_cast<void>(id);
				std::uint32_t node = 0;
				for (const auto character : name) {
					const auto child = allocate_base(node) + static_cast<std::uint8_t>(character);
					parents[child] = node;
					node = child;
				}
				const auto leaf = allocate_base(node);
				parents[leaf] = node;
				leaves.push_back(leaf);
			}
			AppendArray(bytes, bases);
			AppendArray(bytes, parents);
			AppendArray(bytes, leaves);
		}

		Bytes Encode(const sgre::resource::PsbValue& value) const {
			Bytes result;
			if (std::holds_alternative<std::monostate>(value.Data)) {
				return {1};
			}
			if (const auto* boolean = std::get_if<bool>(&value.Data)) {
				return {static_cast<std::uint8_t>(*boolean ? 2 : 3)};
			}
			if (const auto* integer = std::get_if<std::int64_t>(&value.Data)) {
				result.push_back(12);
				AppendWord(result, static_cast<std::uint64_t>(*integer), 8);
			} else if (const auto* real = std::get_if<double>(&value.Data)) {
				result.push_back(31);
				AppendWord(result, std::bit_cast<std::uint64_t>(*real), 8);
			} else if (const auto* text = std::get_if<std::string>(&value.Data)) {
				result.push_back(24);
				AppendWord(result, m_Strings.at(*text));
			} else {
				std::vector<std::uint32_t> offsets;
				std::vector<std::uint32_t> names;
				Bytes payload;
				const auto append = [&](const auto& child) {
					offsets.push_back(static_cast<std::uint32_t>(payload.size()));
					const auto encoded = Encode(child);
					payload.insert(payload.end(), encoded.begin(), encoded.end());
				};
				if (const auto* object = std::get_if<sgre::resource::PsbObject>(&value.Data)) {
					result.push_back(33);
					for (const auto& [name, child] : *object) {
						names.push_back(m_Names.at(name));
						append(child);
					}
					AppendArray(result, names);
				} else if (const auto* array = std::get_if<sgre::resource::PsbArray>(&value.Data)) {
					result.push_back(32);
					for (const auto& child : *array) {
						append(child);
					}
				} else {
					throw std::runtime_error("shape fixture does not fabricate resource chunks");
				}
				AppendArray(result, offsets);
				result.insert(result.end(), payload.begin(), payload.end());
			}
			return result;
		}

		std::map<std::string, std::uint32_t> m_Names;
		std::map<std::string, std::uint32_t> m_Strings;
	};
} // namespace motion_shape_fixture
