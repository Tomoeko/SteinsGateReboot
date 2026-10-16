#include "sgre/text/recursive_decomposition.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::uint32_t kNativeMaximumBufferRecords = 0x0ccccccc;
	static constexpr std::uint32_t kSupportedBufferRequest = 65536;
	static constexpr std::uint32_t kSupportedBufferAllocation = 131072;
	static constexpr std::size_t kMaximumUtf16InputUnits = 65536;

	static bool ReallocateBufferRecords(std::vector<SubstitutionGlyph>& storage, std::uint32_t slots) {
		try {
			std::vector<SubstitutionGlyph> candidate(slots);
			std::copy_n(storage.begin(), std::min(storage.size(), candidate.size()), candidate.begin());
			storage = std::move(candidate);
		} catch (const std::bad_alloc&) {
			return false;
		}

		return true;
	}

	UnicodeNormalizationBuffer::UnicodeNormalizationBuffer()
	    : m_AllocatorGrowth(true) {
		m_State.MaximumLength = 0x3fffffff;
	}

	bool UnicodeNormalizationBuffer::EnsureInputCapacity(std::uint32_t requested) {
		// 3a5be0 admits an already resident request before consulting sticky failure or the maximum.
		if (requested == 0 || requested < m_State.ResidentCapacity) {
			return true;
		}
		if (m_State.Failed) {
			return false;
		}
		if (!m_AllocatorGrowth || requested > m_State.MaximumLength || requested >= kNativeMaximumBufferRecords) {
			m_State.Failed = true;

			return false;
		}

		auto slots = m_State.ResidentCapacity;
		do {
			slots += 32 + (slots >> 1);
		} while (requested >= slots);
		if (slots >= kNativeMaximumBufferRecords) {
			m_State.Failed = true;

			return false;
		}
		if (requested > kSupportedBufferRequest) {
			throw std::runtime_error("Unicode buffer growth request exceeds its supported record limit");
		}

		if (slots > kSupportedBufferAllocation) {
			throw std::runtime_error("Unicode buffer paired allocation exceeds its supported record limit");
		}

		// Both native reallocations run, auxiliary first. Each successful array remains adopted on partial failure.
		const auto auxiliary = ReallocateBufferRecords(m_Storage[1 - m_InputStorage], slots);
		const auto input = ReallocateBufferRecords(m_Storage[m_InputStorage], slots);
		if (!auxiliary || !input) {
			m_State.Failed = true;

			return false;
		}
		m_State.ResidentCapacity = slots;

		return true;
	}

	static std::uint32_t DecodeForwardUtf16(std::u16string_view text, std::size_t& position, std::size_t end, std::uint32_t replacement) {
		const auto first = std::uint32_t(text[position++]);
		if (first < 0xd800 || first > 0xdfff) {
			return first;
		}
		if (first > 0xdbff || position >= end) {
			return replacement;
		}
		const auto second = std::uint32_t(text[position]);
		if (second < 0xdc00 || second > 0xdfff) {
			return replacement;
		}
		++position;

		return 0x10000 + ((first - 0xd800) << 10) + second - 0xdc00;
	}

	static std::uint32_t DecodePrecedingUtf16(std::u16string_view text, std::size_t& position, std::uint32_t replacement) {
		const auto last = std::uint32_t(text[--position]);
		if (last < 0xd800 || last > 0xdfff) {
			return last;
		}
		if (last < 0xdc00 || position == 0) {
			return replacement;
		}
		const auto first = std::uint32_t(text[position - 1]);
		if (first < 0xd800 || first > 0xdbff) {
			return replacement;
		}
		--position;

		return 0x10000 + ((first - 0xd800) << 10) + last - 0xdc00;
	}

	void UnicodeNormalizationBuffer::AddUtf16(std::u16string_view text, std::uint32_t item_offset, std::uint32_t item_length) {
		if (!m_AllocatorGrowth || m_State.OutputActive || m_State.InputCursor != 0 || m_InputMetadata.Content == UnicodeBufferContent::GlyphIndices) {
			throw std::runtime_error("Unicode buffer UTF16 ingestion requires the general Unicode-input phase");
		}
		if (text.size() > kMaximumUtf16InputUnits || item_offset > text.size() || item_length > text.size() - item_offset) {
			throw std::runtime_error("Unicode buffer UTF16 input or subrange exceeds its supported bounds");
		}
		if (item_length / 2 > kSupportedBufferRequest - m_State.InputCount) {
			throw std::runtime_error("Unicode buffer UTF16 append exceeds its supported record limit");
		}

		static_cast<void>(EnsureInputCapacity(m_State.InputCount + item_length / 2));
		if (m_State.InputCount == 0 && item_offset != 0) {
			m_InputMetadata.PrecedingContextCount = 0;
			auto position = std::size_t(item_offset);
			while (position != 0 && m_InputMetadata.PrecedingContextCount < m_InputMetadata.PrecedingContext.size()) {
				m_InputMetadata.PrecedingContext[m_InputMetadata.PrecedingContextCount++] = DecodePrecedingUtf16(text, position, m_InputMetadata.ReplacementCodepoint);
			}
		}

		auto position = std::size_t(item_offset);
		const auto end = position + item_length;
		while (position < end) {
			const auto cluster = static_cast<std::uint32_t>(position);
			const auto codepoint = DecodeForwardUtf16(text, position, end, m_InputMetadata.ReplacementCodepoint);
			if (m_State.InputCount == kSupportedBufferRequest) {
				throw std::runtime_error("Unicode buffer UTF16 append exceeds its supported record limit");
			}
			if (EnsureInputCapacity(m_State.InputCount + 1)) {
				SubstitutionGlyph glyph;
				glyph.GlyphIndex = codepoint;
				glyph.Cluster = cluster;
				m_Storage[m_InputStorage][m_State.InputCount++] = glyph;
			}
		}

		m_InputMetadata.FollowingContextCount = 0;
		while (position < text.size() && m_InputMetadata.FollowingContextCount < m_InputMetadata.FollowingContext.size()) {
			m_InputMetadata.FollowingContext[m_InputMetadata.FollowingContextCount++] = DecodeForwardUtf16(text, position, text.size(), m_InputMetadata.ReplacementCodepoint);
		}
		m_InputMetadata.Content = UnicodeBufferContent::Unicode;
	}

	void UnicodeNormalizationBuffer::ClearInput() {
		const auto capacity = m_State.ResidentCapacity;
		const auto maximum = m_State.MaximumLength;
		m_State = {};
		m_State.ResidentCapacity = capacity;
		m_State.MaximumLength = maximum;
		m_InputMetadata.Content = UnicodeBufferContent::Unset;
		m_InputMetadata.DirectionWord = 0;
		m_InputMetadata.ScriptTag = 0;
		m_InputMetadata.PrecedingContext = {};
		m_InputMetadata.FollowingContext = {};
		m_InputMetadata.PrecedingContextCount = 0;
		m_InputMetadata.FollowingContextCount = 0;
	}

	void UnicodeNormalizationBuffer::ResetInput() {
		m_InputMetadata.TextFlags = 0;
		m_InputMetadata.ReplacementCodepoint = 0xfffd;
		ClearInput();
	}

	void UnicodeNormalizationBuffer::SetInputDirection(GlyphShapingDirection direction) {
		if (direction != GlyphShapingDirection::LeftToRight && direction != GlyphShapingDirection::RightToLeft && direction != GlyphShapingDirection::TopToBottom && direction != GlyphShapingDirection::BottomToTop) {
			throw std::runtime_error("Unicode buffer direction is outside its supported range");
		}
		m_InputMetadata.DirectionWord = static_cast<std::uint32_t>(direction);
	}

	void UnicodeNormalizationBuffer::SetInputScript(std::uint32_t script_tag) {
		m_InputMetadata.ScriptTag = script_tag;
	}

	void UnicodeNormalizationBuffer::SetInputGrowthLimits(std::uint32_t maximum_length, std::uint32_t maximum_operations) {
		if (!m_AllocatorGrowth) {
			throw std::runtime_error("Unicode buffer growth limits require the general allocation owner");
		}
		m_State.MaximumLength = maximum_length;
		m_InputMetadata.MaximumOperations = maximum_operations;
	}

	void UnicodeNormalizationBuffer::InitializeInputUnicodeProperties() {
		m_State.ScratchFlags = InitializeUnicodeGlyphProperties(std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount), m_State.ScratchFlags);
	}

	void UnicodeNormalizationBuffer::InitializeInputClusters(GlyphClusterLevel level) {
		if (m_State.OutputActive || m_State.InputCursor != 0 || m_State.OutputCount != 0) {
			throw std::runtime_error("Unicode buffer initial clustering requires the original zero-cursor input phase");
		}
		m_State.ScratchFlags = InitializeUnicodeGlyphClusters(std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount), level, m_State.ScratchFlags);
		m_InputMetadata.ClusterLevel = level;
	}

	GlyphShapingDirection UnicodeNormalizationBuffer::AdjustInputDirection() {
		if (m_State.OutputActive || m_State.InputCursor != 0 || m_State.OutputCount != 0) {
			throw std::runtime_error("Unicode buffer direction adjustment requires the original zero-cursor input phase");
		}
		const auto direction = AdjustUnicodeGlyphDirection(std::span<SubstitutionGlyph>(m_Storage[m_InputStorage]).first(m_State.InputCount), m_InputMetadata.ClusterLevel, static_cast<GlyphShapingDirection>(m_InputMetadata.DirectionWord), m_InputMetadata.ScriptTag);
		m_InputMetadata.DirectionWord = static_cast<std::uint32_t>(direction);

		return direction;
	}

	bool UnicodeNormalizationBuffer::UsesAllocatorGrowth() const {
		return m_AllocatorGrowth;
	}

	UnicodeBufferInputMetadata UnicodeNormalizationBuffer::InputMetadata() const {
		auto metadata = m_InputMetadata;
		metadata.InputAllocationSlots = static_cast<std::uint32_t>(m_Storage[m_InputStorage].size());
		metadata.AuxiliaryAllocationSlots = static_cast<std::uint32_t>(m_Storage[1 - m_InputStorage].size());

		return metadata;
	}

} // namespace sgre::text
