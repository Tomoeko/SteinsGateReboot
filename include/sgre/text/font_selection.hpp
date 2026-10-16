#pragma once

#include "sgre/text/element_draw.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sgre::text {

	enum class TextFontFamily {
		Normal,
		Ruby
	};

	struct TextFontSelectionKey {
		float RequestedSize = 0;
		std::uint32_t FaceIndex = 0;
		TextFontStyle Style = TextFontStyle::Regular;
	};

	struct TextFontSelectionEntry {
		TextFontSelectionKey Key;
		// The native constructor clears this flag; its later producer remains unresolved.
		bool IgnoreSize = false;
	};

	class TextFontSelectionCache {
	public:
		explicit TextFontSelectionCache(TextFontFamily family, std::size_t max_entries = 65536);
		// Finds the current entry first, then the earliest compatible retained entry.
		// A miss leaves the current entry unchanged until the real findFont callback completes.
		[[nodiscard]] std::optional<std::size_t> Find(const TextFontSelectionKey& key);
		// Commit only after a successful real findFont callback and retained FontInfo construction.
		[[nodiscard]] std::size_t Append(const TextFontSelectionEntry& entry);
		void ResolveFailed();
		void Clear();
		[[nodiscard]] TextFontFamily Family() const;
		[[nodiscard]] std::span<const TextFontSelectionEntry> Entries() const;
		[[nodiscard]] std::optional<std::size_t> Current() const;

	private:
		[[nodiscard]] bool Matches(const TextFontSelectionEntry& entry, const TextFontSelectionKey& key) const;
		TextFontFamily m_Family = TextFontFamily::Normal;
		std::size_t m_MaxEntries = 0;
		std::vector<TextFontSelectionEntry> m_Entries;
		std::optional<std::size_t> m_Current;
	};

} // namespace sgre::text
