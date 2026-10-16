#include "sgre/resource/archive_index.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace sgre::resource {

	static std::runtime_error ArchiveError(std::string_view message) {
		return std::runtime_error("archive index: " + std::string(message));
	}

	static const PsbValue& RequireMember(const PsbObject& object, std::string_view name) {
		const PsbValue* value = nullptr;
		for (const auto& member : object) {
			if (member.first != name) {
				continue;
			}

			if (value != nullptr) {
				throw ArchiveError("duplicate root field '" + std::string(name) + "'");
			}

			value = &member.second;
		}

		if (value == nullptr) {
			throw ArchiveError("missing root field '" + std::string(name) + "'");
		}

		return *value;
	}

	static const PsbObject& ReadArchiveRoot(const PsbDocument& document) {
		if (document.Header.Version != 3 || document.Header.Flags != 0) {
			throw ArchiveError("requires unfiltered PSB version 3");
		}

		const auto* object = std::get_if<PsbObject>(&document.Root.Data);
		if (object == nullptr) {
			throw ArchiveError("root must be an object");
		}

		const auto* identifier = std::get_if<std::string>(&RequireMember(*object, "id").Data);
		if (identifier == nullptr || *identifier != "archive") {
			throw ArchiveError("root field 'id' must be 'archive'");
		}

		const PsbValue& version = RequireMember(*object, "version");
		const auto* integer = std::get_if<std::int64_t>(&version.Data);
		const auto* floating = std::get_if<double>(&version.Data);
		if ((integer == nullptr || *integer != 1) && (floating == nullptr || *floating != 1.0)) {
			throw ArchiveError("root field 'version' must be numeric 1");
		}

		return *object;
	}

	static std::vector<std::string> ReadExpireSuffixes(const PsbObject& root) {
		const auto* array = std::get_if<PsbArray>(&RequireMember(root, "expire_suffix_list").Data);
		if (array == nullptr) {
			throw ArchiveError("root field 'expire_suffix_list' must be an array of strings");
		}

		std::vector<std::string> suffixes;
		suffixes.reserve(array->size());
		for (std::size_t index = 0; index < array->size(); ++index) {
			const auto* suffix = std::get_if<std::string>(&(*array)[index].Data);
			if (suffix == nullptr) {
				throw ArchiveError("expire_suffix_list element " + std::to_string(index) + " must be a string");
			}

			suffixes.push_back(*suffix);
		}

		return suffixes;
	}

	static ArchiveEntry ReadEntry(const std::pair<std::string, PsbValue>& member, std::uint64_t body_size) {
		const auto* pair = std::get_if<PsbArray>(&member.second.Data);
		if (pair == nullptr || pair->size() != 2) {
			throw ArchiveError("file_info entry '" + member.first + "' must be an offset/size pair");
		}

		const auto* offset = std::get_if<std::int64_t>(&(*pair)[0].Data);
		const auto* size = std::get_if<std::int64_t>(&(*pair)[1].Data);
		if (offset == nullptr || size == nullptr || *offset < 0 || *size < 0) {
			throw ArchiveError("file_info entry '" + member.first + "' requires nonnegative integer offset and size");
		}

		ArchiveEntry entry;
		entry.Name = member.first;
		entry.Offset = static_cast<std::uint64_t>(*offset);
		entry.Size = static_cast<std::uint64_t>(*size);
		if (entry.Offset > body_size || entry.Size > body_size - entry.Offset) {
			throw ArchiveError("file_info entry '" + entry.Name + "' exceeds body size " + std::to_string(body_size));
		}

		if (entry.Size > std::numeric_limits<std::uint64_t>::max() - entry.Offset) {
			throw ArchiveError("file_info entry '" + entry.Name + "' end exceeds 64-bit extent");
		}

		return entry;
	}

	static void ValidateOverlaps(const std::vector<ArchiveEntry>& entries) {
		std::vector<const ArchiveEntry*> intervals;
		intervals.reserve(entries.size());
		for (const auto& entry : entries) {
			if (entry.Size != 0) {
				intervals.push_back(&entry);
			}
		}

		std::sort(intervals.begin(), intervals.end(), [](const ArchiveEntry* left, const ArchiveEntry* right) {
			return left->Offset < right->Offset;
		});

		for (std::size_t index = 1; index < intervals.size(); ++index) {
			const ArchiveEntry& previous = *intervals[index - 1];
			const ArchiveEntry& current = *intervals[index];
			if (current.Offset < previous.Offset + previous.Size) {
				throw ArchiveError("file_info entries '" + previous.Name + "' and '" + current.Name + "' overlap");
			}
		}
	}

	ArchiveIndex ReadArchiveIndex(const PsbDocument& document, std::uint64_t body_size) {
		const PsbObject& root = ReadArchiveRoot(document);
		ArchiveIndex index;
		index.ExpireSuffixes = ReadExpireSuffixes(root);
		const PsbValue& file_info = RequireMember(root, "file_info");
		if (std::holds_alternative<std::monostate>(file_info.Data)) {
			// The supplied map metadata uses PSB null for its empty file map.
			if (body_size != 0) {
				throw ArchiveError("null file_info requires an empty body");
			}

			return index;
		}

		const auto* object = std::get_if<PsbObject>(&file_info.Data);
		if (object == nullptr) {
			throw ArchiveError("root field 'file_info' must be an object or the empty-map null value");
		}

		index.Entries.reserve(object->size());
		std::unordered_set<std::string_view> names;
		names.reserve(object->size());
		for (const auto& member : *object) {
			if (!names.insert(member.first).second) {
				throw ArchiveError("duplicate file_info entry '" + member.first + "'");
			}

			ArchiveEntry entry = ReadEntry(member, body_size);
			index.SerializedExtent = std::max(index.SerializedExtent, entry.Offset + entry.Size);
			index.Entries.push_back(std::move(entry));
		}

		ValidateOverlaps(index.Entries);

		return index;
	}

} // namespace sgre::resource
