#pragma once

#include "sgre/resource/psb.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sgre::resource {
	struct StructStorageLimits {
		std::size_t MaxDefinitions = 4096;
		std::size_t MaxFields = 65536;
		std::size_t MaxDepth = 64;
		std::size_t MaxRootBytes = 16 * 1024 * 1024;
		std::size_t MaxArrayElements = 1024 * 1024;
		std::size_t MaxStringUnits = 65536;
	};

	enum class StructValueKind {
		Boolean,
		Integer,
		Float,
		String,
		Array,
		Table
	};

	using StructScalar = std::variant<bool, std::int32_t, float, std::string>;
	class StructStorageState;
	class StructStorage;

	class StructValue {
	public:
		[[nodiscard]] StructValueKind Kind() const;
		[[nodiscard]] bool Contains(std::int32_t index) const;
		[[nodiscard]] bool Contains(std::string_view key) const;
		[[nodiscard]] StructValue At(std::int32_t index) const;
		[[nodiscard]] StructValue At(std::string_view key) const;
		[[nodiscard]] std::size_t Length() const;
		[[nodiscard]] std::vector<std::string> Keys() const;
		[[nodiscard]] StructScalar Read() const;
		void Write(const StructScalar& value);
		void Clear();
		[[nodiscard]] bool IsFlags() const;
		[[nodiscard]] bool IsCharacterArray() const;
		[[nodiscard]] std::vector<std::uint8_t> ReadCharacterArray() const;
		void WriteCharacterArray(std::span<const std::uint8_t> bytes);
		[[nodiscard]] std::uint32_t CountFlags() const;
		[[nodiscard]] std::uint32_t CountFlags(std::span<const std::uint32_t> indices) const;
		[[nodiscard]] bool AndFlags() const;
		[[nodiscard]] bool AndFlags(std::span<const std::uint32_t> indices) const;
		[[nodiscard]] bool OrFlags() const;
		[[nodiscard]] bool OrFlags(std::span<const std::uint32_t> indices) const;

	private:
		StructValue(std::shared_ptr<StructStorageState> owner, std::size_t definition, std::size_t field, std::size_t offset, std::size_t element, bool is_table);
		std::shared_ptr<StructStorageState> m_Owner;
		std::size_t m_Definition = 0;
		std::size_t m_Field = 0;
		std::size_t m_Offset = 0;
		std::size_t m_Element = 0;
		bool m_IsTable = true;
		bool m_IsElement = false;
		friend class StructStorage;
	};

	class StructStorage {
	public:
		// Layout is copied from immutable PSB schema; every value view retains this same mutable byte owner.
		explicit StructStorage(const PsbValue& schema, std::string_view root_name = "root", StructStorageLimits limits = {});
		StructStorage(const StructStorage&) = delete;
		StructStorage& operator=(const StructStorage&) = delete;
		StructStorage(StructStorage&&) noexcept = default;
		StructStorage& operator=(StructStorage&&) noexcept = default;
		[[nodiscard]] StructValue Root() const;
		[[nodiscard]] std::size_t RootSize() const;
		[[nodiscard]] bool HasBacking() const;
		// Serialization copies the exact packed root bytes, without a PSB or host-struct header.
		[[nodiscard]] std::vector<std::uint8_t> Serialize() const;
		void Unserialize(std::span<const std::uint8_t> bytes);
		void Clear();
		void Purge();
		[[nodiscard]] StructStorage Duplicate() const;
		[[nodiscard]] std::int32_t Compare(const StructStorage& other) const;

	private:
		explicit StructStorage(std::shared_ptr<StructStorageState> owner);
		std::shared_ptr<StructStorageState> m_Owner;
	};
} // namespace sgre::resource
