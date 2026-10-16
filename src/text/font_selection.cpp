#include "sgre/text/font_selection.hpp"

#include <stdexcept>

namespace sgre::text {

	TextFontSelectionCache::TextFontSelectionCache(TextFontFamily family, std::size_t max_entries) : m_Family(family), m_MaxEntries(max_entries) {
		if ((m_Family != TextFontFamily::Normal && m_Family != TextFontFamily::Ruby) || m_MaxEntries == 0 || m_MaxEntries > 65536) {
			throw std::runtime_error("text font selection requires a recovered family and bounded positive entry limit");
		}
	}

	bool TextFontSelectionCache::Matches(const TextFontSelectionEntry& entry, const TextFontSelectionKey& key) const {
		if ((!entry.IgnoreSize && !(entry.Key.RequestedSize == key.RequestedSize)) || entry.Key.FaceIndex != key.FaceIndex) {
			return false;
		}

		return m_Family == TextFontFamily::Ruby || key.Style == TextFontStyle::Regular || entry.Key.Style == key.Style;
	}

	std::optional<std::size_t> TextFontSelectionCache::Find(const TextFontSelectionKey& key) {
		if (m_Current && Matches(m_Entries[*m_Current], key)) {
			return m_Current;
		}

		for (std::size_t index = 0; index < m_Entries.size(); ++index) {
			if (Matches(m_Entries[index], key)) {
				m_Current = index;

				return m_Current;
			}
		}

		return {};
	}

	std::size_t TextFontSelectionCache::Append(const TextFontSelectionEntry& entry) {
		if (m_Entries.size() >= m_MaxEntries) {
			throw std::runtime_error("text font selection entry limit is exhausted");
		}

		auto prepared = entry;
		if (m_Family == TextFontFamily::Ruby) {
			prepared.Key.Style = TextFontStyle::Regular;
		}

		m_Entries.push_back(prepared);
		m_Current = m_Entries.size() - 1;

		return *m_Current;
	}

	void TextFontSelectionCache::ResolveFailed() {
		m_Current.reset();
	}

	void TextFontSelectionCache::Clear() {
		m_Current.reset();
		m_Entries.clear();
	}

	TextFontFamily TextFontSelectionCache::Family() const {
		return m_Family;
	}

	std::span<const TextFontSelectionEntry> TextFontSelectionCache::Entries() const {
		return m_Entries;
	}

	std::optional<std::size_t> TextFontSelectionCache::Current() const {
		return m_Current;
	}

} // namespace sgre::text
