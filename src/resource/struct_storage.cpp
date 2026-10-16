#include "sgre/resource/struct_storage.hpp"

#include "sgre/text/encoding.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sgre::resource {
	struct StructFieldLayout {
		std::string Name;
		std::uint32_t Tag = 0;
		std::size_t Offset = 0;
		std::size_t Bytes = 0;
		std::size_t Count = 0;
		std::size_t StringUnits = 0;
		std::size_t NestedDefinition = 0;
	};

	struct StructDefinitionLayout {
		std::string Name;
		std::vector<StructFieldLayout> Fields;
		std::size_t Bytes = 0;
	};

	class StructStorageState {
	public:
		std::vector<StructDefinitionLayout> Definitions;
		std::size_t RootDefinition = 0;
		mutable std::optional<std::vector<std::uint8_t>> Backing;

		std::vector<std::uint8_t>& Materialize() const {
			if (!Backing) {
				Backing.emplace(Definitions[RootDefinition].Bytes, 0);
			}

			return *Backing;
		}

		std::span<std::uint8_t> Range(std::size_t offset, std::size_t bytes) const {
			auto& backing = Materialize();
			if (offset > backing.size() || bytes > backing.size() - offset) {
				throw std::runtime_error("Struct value range exceeds its fixed root backing");
			}

			return std::span(backing).subspan(offset, bytes);
		}
	};

	static std::size_t Product(std::size_t left, std::size_t right, std::size_t limit) {
		if (right != 0 && left > limit / right) {
			throw std::runtime_error("Struct layout byte multiplication exceeds its root budget");
		}

		return left * right;
	}

	static std::size_t Parameter(const PsbArray& declaration, std::size_t index, std::size_t limit, std::string_view context) {
		const auto* integer = index < declaration.size() ? std::get_if<std::int64_t>(&declaration[index].Data) : nullptr;
		if (integer == nullptr || *integer < 0 || static_cast<std::uint64_t>(*integer) > limit) {
			throw std::runtime_error("Struct schema requires bounded integer " + std::string(context));
		}

		return static_cast<std::size_t>(*integer);
	}

	class StructLayoutBuilder {
	public:
		StructLayoutBuilder(const PsbValue& schema, StructStorageLimits limits) : m_Schema(std::get_if<PsbObject>(&schema.Data)), m_Limits(limits) {
			if (m_Schema == nullptr || limits.MaxDefinitions == 0 || limits.MaxDefinitions > 4096 || limits.MaxFields == 0 || limits.MaxFields > 65536 || limits.MaxDepth == 0 || limits.MaxDepth > 64 || limits.MaxRootBytes == 0 || limits.MaxRootBytes > 160 * 1024 * 1024 || limits.MaxArrayElements > std::numeric_limits<std::int32_t>::max() || limits.MaxStringUnits > 1024 * 1024 || m_Schema->size() > limits.MaxDefinitions) {
				throw std::runtime_error("Struct schema requires a bounded definition table and limits");
			}

			for (const auto& definition : *m_Schema) {
				ValidateName(definition.first);
				if (std::find(m_Names.begin(), m_Names.end(), definition.first) != m_Names.end()) {
					throw std::runtime_error("Struct schema has a duplicate definition " + definition.first);
				}

				m_Names.push_back(definition.first);
			}
			m_Definitions.resize(m_Names.size());
			m_Status.resize(m_Names.size(), 0);
		}

		std::shared_ptr<StructStorageState> Build(std::string_view root_name) {
			const auto root = DefinitionIndex(root_name);
			for (std::size_t index = 0; index < m_Names.size(); ++index) {
				BuildDefinition(index, 0);
			}

			auto owner = std::make_shared<StructStorageState>();
			owner->Definitions = std::move(m_Definitions);
			owner->RootDefinition = root;

			return owner;
		}

	private:
		static void ValidateName(std::string_view name) {
			if (name.empty() || name.size() > 1024 || name.find('\0') != std::string_view::npos) {
				throw std::runtime_error("Struct schema name is absent or outside its bounded string domain");
			}
		}

		std::size_t DefinitionIndex(std::string_view name) const {
			const auto found = std::find(m_Names.begin(), m_Names.end(), name);
			if (found == m_Names.end()) {
				throw std::runtime_error("Struct schema definition does not exist: " + std::string(name));
			}

			return static_cast<std::size_t>(found - m_Names.begin());
		}

		StructFieldLayout BuildField(const std::string& name, const PsbValue& value, std::size_t depth) {
			ValidateName(name);
			const auto* declaration = std::get_if<PsbArray>(&value.Data);
			if (declaration == nullptr || declaration->empty()) {
				throw std::runtime_error("Struct field requires its actual declaration array: " + name);
			}

			StructFieldLayout field;
			field.Name = name;
			field.Tag = static_cast<std::uint32_t>(Parameter(*declaration, 0, 16, "field type"));
			if (field.Tag == 0) {
				throw std::runtime_error("Struct schema contains unsupported field type zero");
			}
			const std::size_t expected = field.Tag <= 5 ? 1 : field.Tag == 14 || field.Tag == 15 || field.Tag == 16 ? 3
			                                                                                                        : 2;
			if (declaration->size() != expected) {
				throw std::runtime_error("Struct field declaration has an incorrect parameter count: " + name);
			}
			if (field.Tag == 8 || field.Tag == 16) {
				const auto* nested = std::get_if<std::string>(&(*declaration)[1].Data);
				if (nested == nullptr) {
					throw std::runtime_error("Struct nested field requires a named definition: " + name);
				}

				field.NestedDefinition = DefinitionIndex(std::string_view(*nested).substr(0, nested->find('\0')));
				BuildDefinition(field.NestedDefinition, depth + 1);
				if (m_Definitions[field.NestedDefinition].Bytes == 0) {
					// Native4ee770 cannot resolve a nested definition whose computed size remains zero.
					throw std::runtime_error("Struct nested definition requires a positive packed byte size: " + *nested);
				}
			}
			if (field.Tag == 6 || field.Tag == 7 || field.Tag == 14 || field.Tag == 15) {
				field.StringUnits = Parameter(*declaration, 1, m_Limits.MaxStringUnits, "string capacity");
			}
			if (field.Tag >= 9) {
				field.Count = Parameter(*declaration, field.Tag >= 14 ? 2 : 1, m_Limits.MaxArrayElements, "array length");
			}

			if (field.Tag == 1 || field.Tag == 2) {
				field.Bytes = 1;
			} else if (field.Tag == 3) {
				field.Bytes = 2;
			} else if (field.Tag == 4 || field.Tag == 5) {
				field.Bytes = 4;
			} else if (field.Tag == 6 || field.Tag == 7) {
				field.Bytes = Product(field.StringUnits + 1, field.Tag == 7 ? 2 : 1, m_Limits.MaxRootBytes);
			} else if (field.Tag == 8) {
				field.Bytes = m_Definitions[field.NestedDefinition].Bytes;
			} else if (field.Tag == 9) {
				field.Bytes = (field.Count + 7) / 8;
			} else if (field.Tag <= 13) {
				field.Bytes = Product(field.Count, field.Tag == 10 ? 1 : field.Tag == 11 ? 2
				                                                                         : 4,
				    m_Limits.MaxRootBytes);
			} else if (field.Tag <= 15) {
				const auto stride = Product(field.StringUnits + 1, field.Tag == 15 ? 2 : 1, m_Limits.MaxRootBytes);
				field.Bytes = Product(stride, field.Count, m_Limits.MaxRootBytes);
			} else {
				field.Bytes = Product(m_Definitions[field.NestedDefinition].Bytes, field.Count, m_Limits.MaxRootBytes);
			}

			return field;
		}

		void BuildDefinition(std::size_t index, std::size_t depth) {
			if (m_Status[index] == 2) {
				return;
			}
			if (depth >= m_Limits.MaxDepth || m_Status[index] == 1) {
				throw std::runtime_error("Struct schema has cyclic definitions or exceeds its depth budget");
			}
			const auto* members = std::get_if<PsbObject>(&(*m_Schema)[index].second.Data);
			if (members == nullptr || members->size() > m_Limits.MaxFields - m_FieldCount) {
				throw std::runtime_error("Struct definition requires a bounded field table");
			}

			m_Status[index] = 1;
			m_FieldCount += members->size();
			auto& definition = m_Definitions[index];
			definition.Name = m_Names[index];
			for (const auto& member : *members) {
				if (std::any_of(definition.Fields.begin(), definition.Fields.end(), [&](const auto& field) {
					    return field.Name == member.first;
				    })) {
					throw std::runtime_error("Struct definition has a duplicate field " + member.first);
				}

				auto field = BuildField(member.first, member.second, depth);
				if (field.Bytes > m_Limits.MaxRootBytes - definition.Bytes) {
					throw std::runtime_error("Struct packed definition exceeds its root byte budget");
				}

				field.Offset = definition.Bytes;
				definition.Bytes += field.Bytes;
				definition.Fields.push_back(std::move(field));
			}
			m_Status[index] = 2;
		}

		const PsbObject* m_Schema;
		StructStorageLimits m_Limits;
		std::vector<std::string> m_Names;
		std::vector<StructDefinitionLayout> m_Definitions;
		std::vector<std::uint8_t> m_Status;
		std::size_t m_FieldCount = 0;
	};

	static std::uint32_t ScalarTag(const StructFieldLayout& field) {
		return field.Tag >= 9 ? field.Tag - 8 : field.Tag;
	}

	static StructValueKind ScalarKind(std::uint32_t tag) {
		if (tag == 1) {
			return StructValueKind::Boolean;
		}
		if (tag >= 2 && tag <= 4) {
			return StructValueKind::Integer;
		}
		if (tag == 5) {
			return StructValueKind::Float;
		}
		if (tag == 6 || tag == 7) {
			return StructValueKind::String;
		}

		throw std::runtime_error("Struct scalar has no recovered primitive kind");
	}

	StructValue::StructValue(std::shared_ptr<StructStorageState> owner, std::size_t definition, std::size_t field, std::size_t offset, std::size_t element, bool is_table) : m_Owner(std::move(owner)), m_Definition(definition), m_Field(field), m_Offset(offset), m_Element(element), m_IsTable(is_table) {
	}

	StructValueKind StructValue::Kind() const {
		if (m_IsTable) {
			return StructValueKind::Table;
		}
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];

		return field.Tag >= 9 && !m_IsElement ? StructValueKind::Array : ScalarKind(ScalarTag(field));
	}

	bool StructValue::Contains(std::int32_t index) const {
		return Kind() == StructValueKind::Array && index >= 0 && static_cast<std::size_t>(index) < Length();
	}

	bool StructValue::Contains(std::string_view key) const {
		if (!m_IsTable) {
			return false;
		}
		const auto& fields = m_Owner->Definitions[m_Definition].Fields;

		return std::any_of(fields.begin(), fields.end(), [key](const auto& field) {
			return field.Name == key;
		});
	}

	StructValue StructValue::At(std::int32_t index) const {
		if (!Contains(index)) {
			throw std::runtime_error("Struct array index does not exist");
		}
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];
		const auto element = static_cast<std::size_t>(index);
		if (field.Tag == 16) {
			return StructValue(m_Owner, field.NestedDefinition, 0, m_Offset + element * m_Owner->Definitions[field.NestedDefinition].Bytes, 0, true);
		}

		StructValue selected(m_Owner, m_Definition, m_Field, m_Offset, element, false);
		selected.m_IsElement = true;

		return selected;
	}

	StructValue StructValue::At(std::string_view key) const {
		if (!m_IsTable) {
			throw std::runtime_error("Struct key access requires a table view");
		}
		const auto& fields = m_Owner->Definitions[m_Definition].Fields;
		for (std::size_t index = 0; index < fields.size(); ++index) {
			const auto& field = fields[index];
			if (field.Name == key) {
				if (field.Tag == 8) {
					return StructValue(m_Owner, field.NestedDefinition, 0, m_Offset + field.Offset, 0, true);
				}

				return StructValue(m_Owner, m_Definition, index, m_Offset + field.Offset, 0, false);
			}
		}

		throw std::runtime_error("Struct table key does not exist: " + std::string(key));
	}

	std::size_t StructValue::Length() const {
		if (Kind() != StructValueKind::Array) {
			throw std::runtime_error("structvalue is not array.");
		}

		return m_Owner->Definitions[m_Definition].Fields[m_Field].Count;
	}

	std::vector<std::string> StructValue::Keys() const {
		if (!m_IsTable) {
			throw std::runtime_error("Struct iteration keys require a table view");
		}

		std::vector<std::string> keys;
		for (const auto& field : m_Owner->Definitions[m_Definition].Fields) {
			keys.push_back(field.Name);
		}

		return keys;
	}

	static std::uint32_t ReadWord(std::span<const std::uint8_t> bytes) {
		std::uint32_t value = 0;
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8);
		}

		return value;
	}

	static void WriteWord(std::span<std::uint8_t> bytes, std::uint32_t value) {
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			bytes[index] = static_cast<std::uint8_t>(value >> (index * 8));
		}
	}

	static void AppendUtf8(std::string& output, std::uint32_t scalar) {
		if (scalar <= 0x7f) {
			output.push_back(static_cast<char>(scalar));
		} else if (scalar <= 0x7ff) {
			output.push_back(static_cast<char>(0xc0 | (scalar >> 6)));
			output.push_back(static_cast<char>(0x80 | (scalar & 0x3f)));
		} else if (scalar <= 0xffff) {
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

	static std::string ReadString(std::span<const std::uint8_t> bytes, bool wide) {
		std::string result;
		for (std::size_t offset = 0; offset < bytes.size(); offset += wide ? 2 : 1) {
			std::uint32_t scalar = ReadWord(bytes.subspan(offset, wide ? 2 : 1));
			if (scalar == 0) {
				return result;
			}
			if (!wide) {
				result.push_back(static_cast<char>(scalar));

				continue;
			}
			if (scalar >= 0xd800 && scalar <= 0xdbff) {
				if (bytes.size() - offset < 4) {
					throw std::runtime_error("Struct string has an unpaired UTF-16 high surrogate");
				}
				const auto low = ReadWord(bytes.subspan(offset + 2, 2));
				if (low < 0xdc00 || low > 0xdfff) {
					throw std::runtime_error("Struct string has an unpaired UTF-16 high surrogate");
				}

				scalar = 0x10000 + ((scalar - 0xd800) << 10) + low - 0xdc00;
				offset += 2;
			} else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
				throw std::runtime_error("Struct string has an unpaired UTF-16 low surrogate");
			}

			AppendUtf8(result, scalar);
		}

		throw std::runtime_error("Struct string has no bounded terminator in its fixed slot");
	}

	StructScalar StructValue::Read() const {
		if (Kind() == StructValueKind::Table || Kind() == StructValueKind::Array) {
			throw std::runtime_error("Struct scalar read requires a primitive value");
		}
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];
		const auto tag = ScalarTag(field);
		if (tag == 1) {
			const auto byte = m_Owner->Range(m_Offset + m_Element / 8, 1)[0];

			return (byte & (1u << (m_Element & 7))) != 0;
		}
		if (tag <= 5) {
			const auto width = tag == 2 ? 1u : tag == 3 ? 2u
			                                            : 4u;
			auto bits = ReadWord(m_Owner->Range(m_Offset + m_Element * width, width));
			if (tag == 5) {
				return std::bit_cast<float>(bits);
			}
			if (tag == 2 && (bits & 0x80) != 0) {
				bits |= 0xffffff00u;
			} else if (tag == 3 && (bits & 0x8000) != 0) {
				bits |= 0xffff0000u;
			}

			return std::bit_cast<std::int32_t>(bits);
		}
		const auto stride = (field.StringUnits + 1) * (tag == 7 ? 2 : 1);

		return ReadString(m_Owner->Range(m_Offset + m_Element * stride, stride), tag == 7);
	}

	void StructValue::Write(const StructScalar& value) {
		if (Kind() == StructValueKind::Table || Kind() == StructValueKind::Array) {
			throw std::runtime_error("Struct assignment cannot replace a container");
		}
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];
		const auto tag = ScalarTag(field);
		if (tag == 1) {
			const auto* boolean = std::get_if<bool>(&value);
			if (boolean == nullptr) {
				throw std::runtime_error("Struct bool assignment requires its coerced native boolean");
			}

			auto bytes = m_Owner->Range(m_Offset + m_Element / 8, 1);
			const auto mask = static_cast<std::uint8_t>(1u << (m_Element & 7));
			bytes[0] = *boolean ? static_cast<std::uint8_t>(bytes[0] | mask) : static_cast<std::uint8_t>(bytes[0] & static_cast<std::uint8_t>(~mask));

			return;
		}
		if (tag <= 5) {
			std::uint32_t bits = 0;
			if (tag == 5) {
				const auto* number = std::get_if<float>(&value);
				if (number == nullptr) {
					throw std::runtime_error("Struct float assignment requires its coerced native float");
				}

				bits = std::bit_cast<std::uint32_t>(*number);
			} else {
				const auto* integer = std::get_if<std::int32_t>(&value);
				if (integer == nullptr) {
					throw std::runtime_error("Struct integer assignment requires its coerced native integer");
				}

				bits = std::bit_cast<std::uint32_t>(*integer);
			}
			const auto width = tag == 2 ? 1u : tag == 3 ? 2u
			                                            : 4u;
			WriteWord(m_Owner->Range(m_Offset + m_Element * width, width), bits);

			return;
		}
		const auto* text_value = std::get_if<std::string>(&value);
		if (text_value == nullptr) {
			throw std::runtime_error("Struct string assignment requires native character bytes");
		}
		const auto text_bytes = std::string_view(*text_value).substr(0, text_value->find('\0'));
		const auto stride = (field.StringUnits + 1) * (tag == 7 ? 2 : 1);
		if (tag == 6) {
			auto destination = m_Owner->Range(m_Offset + m_Element * stride, stride);
			const auto count = std::min(text_bytes.size(), field.StringUnits);
			for (std::size_t index = 0; index < count; ++index) {
				destination[index] = static_cast<std::uint8_t>(text_bytes[index]);
			}
			destination[count] = 0;

			return;
		}
		const auto units = text::DecodeTextCodeUnits(text_bytes);
		const auto count = std::min(units.size(), field.StringUnits);
		if (count != 0 && units[count - 1] >= 0xd800 && units[count - 1] <= 0xdbff) {
			throw std::runtime_error("Struct UTF-16 truncation splits a surrogate; native replacement conversion is unrecovered");
		}

		auto destination = m_Owner->Range(m_Offset + m_Element * stride, stride);
		for (std::size_t index = 0; index < count; ++index) {
			WriteWord(destination.subspan(index * 2, 2), units[index]);
		}
		WriteWord(destination.subspan(count * 2, 2), 0);
	}

	void StructValue::Clear() {
		const auto bytes = m_IsTable ? m_Owner->Definitions[m_Definition].Bytes : Kind() == StructValueKind::Array ? m_Owner->Definitions[m_Definition].Fields[m_Field].Bytes
		                                                                                                           : m_Owner->Definitions[m_Definition].Fields[m_Field].StringUnits;
		auto range = m_Owner->Range(m_Offset, bytes);
		// Native4f06c0 clears from the view base; primitive string length is measured in bytes here, including UTF-16.
		std::fill(range.begin(), range.end(), 0);
	}

	bool StructValue::IsFlags() const {
		return !m_IsTable && !m_IsElement && m_Owner->Definitions[m_Definition].Fields[m_Field].Tag == 9;
	}

	bool StructValue::IsCharacterArray() const {
		return !m_IsTable && !m_IsElement && m_Owner->Definitions[m_Definition].Fields[m_Field].Tag == 10;
	}

	void StructValue::WriteCharacterArray(std::span<const std::uint8_t> bytes) {
		if (!IsCharacterArray()) {
			throw std::runtime_error("require char array.");
		}
		if (bytes.size() != Length()) {
			throw std::runtime_error("char array size mismatch.");
		}
		auto destination = m_Owner->Range(m_Offset, bytes.size());
		std::copy(bytes.begin(), bytes.end(), destination.begin());
	}

	std::vector<std::uint8_t> StructValue::ReadCharacterArray() const {
		if (!IsCharacterArray()) {
			throw std::runtime_error("require char array.");
		}
		const auto source = m_Owner->Range(m_Offset, Length());

		return {source.begin(), source.end()};
	}

	std::uint32_t StructValue::CountFlags() const {
		if (!IsFlags()) {
			throw std::runtime_error("structvalue is not flags.");
		}

		std::uint32_t count = 0;
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];
		for (const auto byte : m_Owner->Range(m_Offset, field.Bytes)) {
			// Native all-flags count includes padding bits in the final serialized byte.
			count += static_cast<std::uint32_t>(std::popcount(byte));
		}

		return count;
	}

	std::uint32_t StructValue::CountFlags(std::span<const std::uint32_t> indices) const {
		if (!IsFlags() || std::any_of(indices.begin(), indices.end(), [&](auto index) {
			    return index >= Length();
		    })) {
			throw std::runtime_error("Struct flags indices exceed the actual bool array");
		}

		std::uint32_t count = 0;
		for (const auto index : indices) {
			count += std::get<bool>(At(static_cast<std::int32_t>(index)).Read()) ? 1u : 0u;
		}

		return count;
	}

	bool StructValue::AndFlags() const {
		if (!IsFlags()) {
			throw std::runtime_error("structvalue is not flags.");
		}
		const auto& field = m_Owner->Definitions[m_Definition].Fields[m_Field];
		const auto bytes = m_Owner->Range(m_Offset, field.Bytes);
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			const auto expected = index + 1 < bytes.size() || field.Count % 8 == 0 ? 255u : (1u << (field.Count % 8)) - 1;
			if (bytes[index] != expected) {
				return false;
			}
		}

		return true;
	}

	bool StructValue::AndFlags(std::span<const std::uint32_t> indices) const {
		return CountFlags(indices) == indices.size();
	}

	bool StructValue::OrFlags() const {
		return CountFlags() != 0;
	}

	bool StructValue::OrFlags(std::span<const std::uint32_t> indices) const {
		return CountFlags(indices) != 0;
	}

	StructStorage::StructStorage(const PsbValue& schema, std::string_view root_name, StructStorageLimits limits) : m_Owner(StructLayoutBuilder(schema, limits).Build(root_name.substr(0, root_name.find('\0')))) {
	}

	StructStorage::StructStorage(std::shared_ptr<StructStorageState> owner) : m_Owner(std::move(owner)) {
	}

	StructValue StructStorage::Root() const {
		return StructValue(m_Owner, m_Owner->RootDefinition, 0, 0, 0, true);
	}

	std::size_t StructStorage::RootSize() const {
		return m_Owner->Definitions[m_Owner->RootDefinition].Bytes;
	}

	bool StructStorage::HasBacking() const {
		return m_Owner->Backing.has_value();
	}

	std::vector<std::uint8_t> StructStorage::Serialize() const {
		return m_Owner->Materialize();
	}

	void StructStorage::Unserialize(std::span<const std::uint8_t> bytes) {
		if (bytes.size() != RootSize()) {
			throw std::runtime_error("Struct unserialize requires exact root byte size");
		}

		std::vector<std::uint8_t> replacement(bytes.begin(), bytes.end());
		m_Owner->Backing = std::move(replacement);
	}

	void StructStorage::Clear() {
		Root().Clear();
	}

	void StructStorage::Purge() {
		m_Owner->Backing.reset();
	}

	StructStorage StructStorage::Duplicate() const {
		auto duplicate = std::make_shared<StructStorageState>(*m_Owner);
		duplicate->Backing = Serialize();

		return StructStorage(std::move(duplicate));
	}

	std::int32_t StructStorage::Compare(const StructStorage& other) const {
		if (RootSize() != other.RootSize()) {
			return RootSize() < other.RootSize() ? -1 : 1;
		}
		const auto& left = m_Owner->Materialize();
		const auto& right = other.m_Owner->Materialize();
		for (std::size_t index = 0; index < left.size(); ++index) {
			if (left[index] != right[index]) {
				return static_cast<std::int32_t>(left[index]) - static_cast<std::int32_t>(right[index]);
			}
		}

		return 0;
	}
} // namespace sgre::resource
