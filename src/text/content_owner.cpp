#include "sgre/text/content_owner.hpp"

#include "sgre/text/line_geometry.hpp"
#include "sgre/text/markup.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumContentElements = 1'000'000;
	static constexpr std::size_t kMaximumContentWork = 16'777'216;

	static void ChargeContentWork(std::size_t& work, std::size_t amount) {
		if (amount > kMaximumContentWork - work) {
			throw std::runtime_error("text content preparation exceeds its bounded work");
		}
		work += amount;
	}

	static std::u16string ContentPunctuation(std::span<const std::uint16_t> units) {
		std::u16string result;
		result.reserve(units.size());
		for (const auto unit : units) {
			result.push_back(static_cast<char16_t>(unit));
		}
		return result;
	}

	static bool IsContentEffectDirective(const TextMarkupToken& token) {
		return token.Kind == TextMarkupKind::PercentDirective && (token.DirectiveCode == u'e' || token.DirectiveCode == u's');
	}

	static void RequireContentColorOperand(std::span<const std::uint16_t> payload) {
		for (const auto unit : payload) {
			if (unit > 127) {
				throw std::runtime_error("Text.render color operand requires unrecovered native Unicode numeric classification");
			}
		}
	}

	static void RequireContentDomain(std::span<const std::uint16_t> input, const TextFormattingState& formatting, const TextProcessingOptions& options, const TextContentCallbacks& callbacks) {
		if (formatting.Vertical) {
			throw std::runtime_error("Text.render vertical run/ruby producer remains unrecovered");
		}
		if (!callbacks.Fonts.SelectNormal || !callbacks.Fonts.MeasureNormal || !callbacks.Fonts.SelectRuby || !callbacks.PrepareNormal) {
			throw std::runtime_error("Text.render requires its actual normal/ruby font owner and shaped-run producer");
		}
		TextMarkupReader reader(input);
		while (const auto token = reader.Next()) {
			if (token->Kind == TextMarkupKind::ColorDirective) {
				if (!options.IgnoreColor) {
					RequireContentColorOperand(token->Payload);
				}
			} else if (token->Kind == TextMarkupKind::StyleDirective) {
				if (!options.IgnoreRuby && !token->Payload.empty()) {
					const auto comma = std::find(token->Payload.begin(), token->Payload.end(), std::uint16_t(u','));
					if (comma != token->Payload.end()) {
						RequireContentColorOperand(token->Payload.subspan(static_cast<std::size_t>(comma - token->Payload.begin()) + 1));
					}
					if (comma != token->Payload.begin() && (!callbacks.PrepareRuby || !callbacks.MeasureRuby)) {
						throw std::runtime_error("Text.render ruby requires its actual retained ruby provider");
					}
				}
			} else if (IsContentEffectDirective(*token)) {
				if (!options.IgnoreColor && !token->Payload.empty() && token->Payload.front() == u'#') {
					RequireContentColorOperand(token->Payload.subspan(1));
				}
			} else if (token->Kind != TextMarkupKind::CodeUnit && token->Kind != TextMarkupKind::EscapedCodeUnit && token->Kind != TextMarkupKind::LineBreak && !(token->Kind == TextMarkupKind::PercentDirective && (token->DirectiveCode == u'C' || token->DirectiveCode == u'p' || token->DirectiveCode == u'r'))) {
				throw std::runtime_error("Text.render markup effect remains unrecovered at UTF-16 unit " + std::to_string(token->SourceOffset));
			}
		}
	}

	static bool IsContentNumberSpace(std::uint16_t unit) {
		return unit == u' ' || (unit >= u'\t' && unit <= u'\r');
	}

	static std::uint32_t ContentHexDigit(std::uint16_t unit) {
		if (unit >= u'0' && unit <= u'9') {
			return static_cast<std::uint32_t>(unit - u'0');
		}
		if (unit >= u'a' && unit <= u'f') {
			return static_cast<std::uint32_t>(unit - u'a' + 10);
		}
		if (unit >= u'A' && unit <= u'F') {
			return static_cast<std::uint32_t>(unit - u'A' + 10);
		}

		return 16;
	}

	static std::uint32_t ReadContentInteger(std::span<const std::uint16_t> payload, std::uint32_t base) {
		std::size_t position = 0;
		while (position < payload.size() && IsContentNumberSpace(payload[position])) {
			++position;
		}
		bool negative = false;
		if (position < payload.size() && (payload[position] == u'+' || payload[position] == u'-')) {
			negative = payload[position] == u'-';
			++position;
		}
		if (base == 16 && payload.size() - position >= 2 && payload[position] == u'0' && (payload[position + 1] == u'x' || payload[position + 1] == u'X')) {
			position += 2;
		}
		// Original575BE0 saturates signed32 before each directive converts the retained word.
		const auto limit = negative ? std::uint32_t(0x80000000) : std::uint32_t(0x7fffffff);
		auto value = std::uint32_t(0);
		while (position < payload.size()) {
			const auto digit = ContentHexDigit(payload[position++]);
			if (digit >= base) {
				break;
			}
			value = value > (limit - digit) / base ? limit : value * base + digit;
		}
		if (negative) {
			value = std::uint32_t(0) - value;
		}

		return value;
	}

	static std::uint32_t ReadContentColor(std::span<const std::uint16_t> payload, std::uint32_t default_color) {
		if (payload.empty()) {
			return default_color;
		}
		RequireContentColorOperand(payload);

		return ReadContentInteger(payload, 16) | 0xff000000;
	}

	static void ApplyContentEffect(const TextMarkupToken& token, TextFormattingState& formatting, const TextProcessingOptions& options) {
		if (token.Payload.empty()) {
			return;
		}
		const auto edge = token.DirectiveCode == u'e';
		if (token.Payload.front() == u'#') {
			if (!options.IgnoreColor) {
				auto& color = edge ? formatting.Current.EdgeColor : formatting.Current.ShadowColor;
				color = ReadContentColor(token.Payload.subspan(1), edge ? formatting.Defaults.EdgeColor : formatting.Defaults.ShadowColor);
			}
		} else if (!options.IgnoreType) {
			auto& enabled = edge ? formatting.Current.Edge : formatting.Current.Shadow;
			if (token.Payload.front() == u'1') {
				enabled = true;
			} else if (token.Payload.front() == u'0') {
				enabled = false;
			} else {
				enabled = edge ? formatting.Defaults.Edge : formatting.Defaults.Shadow;
			}
		}
	}

	static constexpr std::size_t kNoPunctuationIndex = std::numeric_limits<std::size_t>::max();

	static std::size_t FindContentPunctuation(std::span<const std::uint16_t> punctuation, std::uint16_t unit, std::size_t& work) {
		ChargeContentWork(work, punctuation.size());
		const auto found = std::find(punctuation.begin(), punctuation.end(), unit);

		return found == punctuation.end() ? kNoPunctuationIndex : static_cast<std::size_t>(found - punctuation.begin());
	}

	struct TextIndentationMatch {
		std::size_t Begin = kNoPunctuationIndex;
		std::size_t End = kNoPunctuationIndex;
	};

	struct TextIndentationState {
		bool FirstEligible = true;
		bool FirstOrdinary = true;
		std::int32_t Depth = 0;
		std::u16string SavedOpener;

		void Complete(TextIndentationMatch match, bool accepted, const TextProcessingOptions& options, float cursor, float& line_start, std::size_t& work) {
			if (accepted && match.Begin != kNoPunctuationIndex) {
				if (FirstEligible && Depth == 0) {
					// Font callbacks may have changed the live Begin table after its original index lookup.
					if (match.Begin >= options.Begin.size()) {
						throw std::runtime_error("text opening punctuation index no longer belongs to its current Begin table");
					}
					SavedOpener.assign(1, static_cast<char16_t>(options.Begin[match.Begin]));
					line_start = cursor;
				}
				++Depth;
			} else if (accepted && match.End != kNoPunctuationIndex) {
				if (Depth == 1) {
					// Native substring search of the initially empty saved opener returns index zero.
					const auto index = SavedOpener.empty() ? 0 : FindContentPunctuation(options.Begin, static_cast<std::uint16_t>(SavedOpener.front()), work);
					if (index == match.End) {
						line_start = 0;
					}
				}
				--Depth;
			}
			// The bounded source-element count also bounds the signed depth, including unmatched closers.
			FirstEligible = false;
			FirstOrdinary = false;
		}
	};

	static TextElementDrawStyle CaptureContentStyle(const TextFormattingCurrent& current) {
		TextElementDrawStyle style;
		style.MainColor = current.Color;
		style.ShadowColor = current.ShadowColor;
		style.EdgeColor = current.EdgeColor;
		style.Bold = current.Bold;
		style.Italic = current.Italic;
		style.ShadowEnabled = current.Shadow;
		style.EdgeEnabled = current.Edge;
		style.ShadowOffset = current.ShadowOffset;
		style.EdgeWidth = current.EdgeSize;
		return style;
	}

	static TextContentElement PrepareContentElement(std::uint16_t unit, const TextFormattingState& formatting, const TextContentCallbacks& callbacks) {
		TextContentElement element;
		element.Text.push_back(static_cast<char16_t>(unit));
		element.Run = callbacks.PrepareNormal(unit);
		element.Geometry.Metrics = {element.Run.Advance.X, formatting.FontScale * formatting.Current.FontSize};
		element.Style = CaptureContentStyle(formatting.Current);
		element.Font = PrepareTextFontSelectionRequest(formatting, TextFontFamily::Normal, false).Key;
		element.RubyFont = PrepareTextFontSelectionRequest(formatting, TextFontFamily::Ruby, false).Key;

		return element;
	}

	static void RequireContentRubyGeometry(const TextContentRuby& ruby) {
		const auto& geometry = ruby.Geometry;
		if (!std::isfinite(geometry.Position.X) || !std::isfinite(geometry.Position.Y) || !std::isfinite(geometry.Metrics.Width) || !std::isfinite(geometry.Metrics.Height)) {
			throw std::runtime_error("text ruby geometry exceeds the recovered finite domain");
		}
	}

	static TextContentRuby PrepareContentRuby(std::u16string_view text, const TextContentCallbacks& callbacks, float height, std::size_t& work) {
		ChargeContentWork(work, text.size());
		TextContentRuby ruby;
		ruby.Text = text;
		ruby.Run = callbacks.PrepareRuby(ruby.Text);
		ChargeContentWork(work, ruby.Run.Glyphs.size());
		ruby.Geometry.Metrics = {ruby.Run.Advance.X, height};

		return ruby;
	}

	static void AttachContentRuby(std::span<TextContentElement> elements, std::u16string_view text, const TextFormattingState& formatting, const TextContentCallbacks& callbacks, std::size_t& work) {
		if (text.empty()) {
			return;
		}
		const auto height = formatting.Current.RubySize * formatting.FontScale;
		const auto y = -height - formatting.Current.RubyOffset;
		if (elements.size() == 1) {
			auto ruby = PrepareContentRuby(text, callbacks, height, work);
			ruby.Geometry.Position = {elements.front().Geometry.Metrics.Width * 0.5f - ruby.Geometry.Metrics.Width * 0.5f, y};
			RequireContentRubyGeometry(ruby);
			elements.front().Ruby.push_back(std::move(ruby));
			return;
		}

		float base_distance = 0;
		for (const auto& element : elements) {
			base_distance += element.Geometry.Metrics.Width;
		}
		base_distance += static_cast<float>(elements.size() - 1) * formatting.Current.Pitch;
		auto center = elements.front().Geometry.Metrics.Width * 0.5f;
		base_distance = base_distance - center - elements.back().Geometry.Metrics.Width * 0.5f;
		std::vector<float> widths;
		widths.reserve(text.size());
		float ruby_distance = 0;
		for (const auto unit : text) {
			const auto width = callbacks.MeasureRuby(static_cast<std::uint16_t>(unit));
			if (!std::isfinite(width)) {
				throw std::runtime_error("text ruby measurement is nonfinite");
			}
			widths.push_back(width);
			ruby_distance += width;
		}
		ruby_distance = ruby_distance - widths.front() * 0.5f - widths.back() * 0.5f;
		if (!(base_distance > ruby_distance)) {
			center -= (ruby_distance - base_distance) * 0.5f;
			base_distance = ruby_distance;
		}
		// Native divides by zero for a sole annotation unit; no following position consumes that result.
		const auto spacing = text.size() == 1 ? 0.0f : base_distance / static_cast<float>(text.size() - 1);
		std::size_t parent = 0;
		for (std::size_t index = 0; index < text.size(); ++index) {
			auto ruby = PrepareContentRuby(text.substr(index, 1), callbacks, height, work);
			ruby.Geometry.Position = {center - widths[index] * 0.5f, y};
			RequireContentRubyGeometry(ruby);
			elements[parent].Ruby.push_back(std::move(ruby));
			center += spacing;
			const auto advance = elements[parent].Geometry.Metrics.Width + formatting.Current.Pitch;
			if (parent < elements.size() - 1 && !(center <= advance)) {
				++parent;
				center -= advance;
			}
		}
	}

	void TextContentOwner::ClearPendingRuby() noexcept {
		m_RubyText.clear();
		m_PendingText.clear();
		m_RubyCountdown = 0;
	}

	void TextContentOwner::SetRubyOperand(std::span<const std::uint16_t> payload) {
		if (payload.empty()) {
			return;
		}
		const auto comma = std::find(payload.begin(), payload.end(), std::uint16_t(u','));
		const auto count = static_cast<std::size_t>(comma - payload.begin());
		m_RubyText = ContentPunctuation(payload.first(count));
		m_RubyCountdown = comma == payload.end() ? 0 : std::bit_cast<std::int32_t>(ReadContentInteger(payload.subspan(count + 1), 10));
	}

	bool TextContentOwner::AppendCodeUnit(std::uint16_t unit, const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, const TextContentCallbacks& callbacks, float delay, std::size_t& work) {
		ChargeContentWork(work, 1);
		m_PendingText.push_back(static_cast<char16_t>(unit));
		if (m_RubyCountdown > 0) {
			--m_RubyCountdown;
			return true;
		}
		std::vector<TextContentElement> elements;
		elements.reserve(m_PendingText.size());
		for (const auto pending : m_PendingText) {
			auto element = PrepareContentElement(static_cast<std::uint16_t>(pending), formatting, callbacks);
			ChargeContentWork(work, element.Run.Glyphs.size());
			elements.push_back(std::move(element));
		}
		AttachContentRuby(elements, m_RubyText, formatting, callbacks, work);
		m_RubyText.clear();
		m_PendingText.clear();
		for (auto& element : elements) {
			element.RevealTime = m_Reveal.NextRelativeTime;
			m_Reveal.LastRelativeTime = m_Reveal.NextRelativeTime;
			const auto actual_delay = options.WidthTimeScale ? (element.Geometry.Metrics.Width / (formatting.FontScale * formatting.Current.FontSize)) * delay : delay;
			m_Reveal.NextRelativeTime += actual_delay;
			if (m_RenderedElementCount >= kMaximumContentElements) {
				throw std::runtime_error("text rendered-element count exceeds its limit");
			}
			++m_RenderedElementCount;
			if (!Append(std::move(element), formatting, options, render_size, work)) {
				return false;
			}
		}

		return true;
	}

	void TextContentOwner::Place(TextContentElement element, const TextFormattingState& formatting) {
		if (m_ElementCount >= kMaximumContentElements) {
			throw std::runtime_error("text content element count exceeds its limit");
		}
		const auto placement = PlaceMeasuredTextElement(TextDirection::Horizontal, m_Cursor, element.Geometry.Metrics, formatting.Current.Pitch);
		element.Geometry.Position = placement.Position;
		m_Current.push_back(std::move(element));
		m_Cursor = placement.NextCursor;
		++m_ElementCount;
		UpdateTextWordBoundary(m_Wrap, !formatting.Current.WordBreak, m_Current.back().Text, m_Current.size());
	}

	bool TextContentOwner::Append(TextContentElement element, const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, std::size_t& work, std::size_t depth) {
		if (depth >= 64) {
			throw std::runtime_error("text replay nesting exceeds its bounded preparation depth");
		}
		ChargeContentWork(work, 1);
		if (TextElementReachesLineLimit(TextDirection::Horizontal, m_Cursor, element.Geometry.Metrics, render_size.X, options.IgnoreHorizontalOverflow)) {
			std::vector<std::u16string_view> texts;
			texts.reserve(m_Current.size());
			for (const auto& previous : m_Current) {
				texts.push_back(previous.Text);
			}
			ChargeContentWork(work, texts.size());
			const auto start = ContentPunctuation(options.Following);
			const auto end = ContentPunctuation(options.Leading);
			TextWrapSettings settings;
			settings.KeepWordsTogether = !formatting.Current.WordBreak;
			settings.MaximumLeadingProhibitionOverhang = static_cast<std::uint32_t>(std::max(options.MaximumProhibitionCount, 0));
			settings.ProhibitedLineStart = start;
			settings.ProhibitedLineEnd = end;
			const auto plan = PlanTextLineOverflow(settings, m_Wrap, texts, element.Text);
			m_Wrap.LeadingProhibitionOverhang = plan.LeadingProhibitionOverhang;
			if (plan.FinalizeLine) {
				std::deque<TextContentElement> replay;
				for (std::size_t index = plan.RetainedElementCount; index < m_Current.size(); ++index) {
					replay.push_back(m_Current[index]);
				}
				m_Current.erase(m_Current.begin() + static_cast<std::ptrdiff_t>(plan.RetainedElementCount), m_Current.end());
				m_ElementCount -= replay.size();
				if (!FinishLine(formatting, options, render_size)) {
					return false;
				}
				for (auto& previous : replay) {
					if (!Append(std::move(previous), formatting, options, render_size, work, depth + 1)) {
						return false;
					}
				}
			}
		}
		Place(std::move(element), formatting);
		return true;
	}

	bool TextContentOwner::FinishLine(const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size) {
		if (m_History.size() >= kMaximumContentElements) {
			throw std::runtime_error("text content line count exceeds its limit");
		}
		std::vector<resource::GlyphSize> metrics;
		std::vector<TextCursor> positions;
		metrics.reserve(m_Current.size());
		positions.reserve(m_Current.size());
		for (const auto& element : m_Current) {
			metrics.push_back(element.Geometry.Metrics);
			positions.push_back(element.Geometry.Position);
		}
		TextLineGeometrySettings settings;
		settings.Alignment = DecodeTextAlignment(formatting.Current.Alignment);
		settings.RenderWidth = render_size.X;
		settings.RenderHeight = render_size.Y;
		settings.MinimumCrossExtent = formatting.Current.LineSize;
		settings.LineSpacing = formatting.Current.LineSpacing;
		settings.LineStart = m_LineStart;
		settings.AllowCrossOverflow = options.IgnoreVerticalOverflow;
		const auto old_cursor = m_Cursor;
		const auto line = FinalizeTextLineGeometry(settings, old_cursor, metrics, positions);
		m_Overflowed = m_Overflowed || line.CrossLimitExceeded;
		if (!line.Accepted) {
			m_ElementCount -= m_Current.size();
			m_Current.clear();
			m_Wrap = {};
			return false;
		}
		if (!m_Current.empty()) {
			const auto prefix = (TextLineAlignmentOffset(settings.Alignment, render_size.X, old_cursor.X) + m_Current.front().Geometry.Position.X) / formatting.Current.FontBaseWidth;
			if (!std::isfinite(prefix) || prefix < -2147483648.0f || prefix >= 2147483648.0f) {
				throw std::runtime_error("text processed-line indentation exceeds the native integer domain");
			}
			const auto count = static_cast<std::int32_t>(prefix);
			if (count > 0) {
				if (static_cast<std::size_t>(count) > kMaximumContentWork - m_Processed.size()) {
					throw std::runtime_error("text processed-line space prefix exceeds its limit");
				}
				m_Processed.append(static_cast<std::size_t>(count), u'\u3000');
			}
		}
		std::size_t index = 0;
		for (auto& element : m_Current) {
			element.Geometry.Position = positions[index++];
			if (element.Text.size() > kMaximumContentWork - m_Processed.size()) {
				throw std::runtime_error("text processed content exceeds its limit");
			}
			m_Processed.append(element.Text);
		}
		TextContentLine retained;
		retained.Elements = m_Current;
		m_History.push_back(std::move(retained));
		m_Current.clear();
		if (m_Processed.size() >= kMaximumContentWork) {
			throw std::runtime_error("text processed line terminator exceeds its limit");
		}
		m_Processed.push_back(u'\n');
		m_Cursor = line.NextCursor;
		m_Wrap = {};
		ClearPendingRuby();
		return true;
	}

	void TextContentOwner::Retime(const TextContentRequest& request, float delay) {
		std::vector<TextContentElement*> elements;
		std::vector<float> times;
		elements.reserve(m_ElementCount);
		times.reserve(m_ElementCount);
		for (auto& line : m_History) {
			for (auto& element : line.Elements) {
				elements.push_back(&element);
				times.push_back(element.RevealTime);
			}
		}
		for (auto& element : m_Current) {
			elements.push_back(&element);
			times.push_back(element.RevealTime);
		}
		static_cast<void>(RetimeTextRevealSegment(m_Reveal, times, m_Reveal.SegmentFirstElement, request.TargetTime > 0, delay, request.TargetTime));
		if (m_Reveal.SegmentBaseTime > m_RenderDelay) {
			m_RenderDelay = m_Reveal.SegmentBaseTime;
		}
		for (std::size_t index = 0; index < elements.size(); ++index) {
			elements[index]->RevealTime = times[index];
		}
		m_Boundaries.push_back({static_cast<std::uint32_t>(elements.size()), 0});
		++m_SegmentCount;
	}

	bool TextContentOwner::Render(std::span<const std::uint16_t> input, TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size, const TextContentRequest& request, const TextContentCallbacks& callbacks) {
		if (m_Operating || m_Clearing) {
			throw std::runtime_error("text content operation cannot reenter its retained owner");
		}
		RequireContentDomain(input, formatting, options, callbacks);
		if (m_SegmentCount != 0) {
			throw std::runtime_error("Text.render appended-segment state requires native multi-call timing recovery");
		}
		m_Operating = true;
		try {
			callbacks.Fonts.SelectNormal();
			formatting.Current.FontBaseWidth = callbacks.Fonts.MeasureNormal(0x3000).Width;
			if (formatting.Current.FontBaseWidth == 0) {
				formatting.Current.FontBaseWidth = formatting.Current.FontSize;
			}
			callbacks.Fonts.SelectRuby();
			if (!request.KeepPreviousTimes) {
				for (auto& line : m_History) {
					for (auto& element : line.Elements) {
						element.RevealTime = 0;
					}
				}
				m_Boundaries.clear();
				m_SegmentCount = 0;
				m_RenderDelay = 0;
			}
			m_Reveal = {};
			const auto delay = request.TargetTime > 0 && request.ElementDelay == 0 ? 0.001f : static_cast<float>(request.ElementDelay);
			TextMarkupReader reader(input);
			std::size_t work = 0;
			bool accepted = true;
			// Depth/opener eligibility are parser locals; m_LineStart remains actual content state.
			TextIndentationState indentation;
			while (const auto token = reader.Next()) {
				if (token->Kind == TextMarkupKind::ColorDirective) {
					if (!options.IgnoreColor) {
						formatting.Current.Color = ReadContentColor(token->Payload, formatting.Defaults.Color);
					}
				} else if (token->Kind == TextMarkupKind::StyleDirective) {
					if (!options.IgnoreRuby) {
						SetRubyOperand(token->Payload);
					}
				} else if (IsContentEffectDirective(*token)) {
					ApplyContentEffect(*token, formatting, options);
				} else if (token->Kind == TextMarkupKind::PercentDirective && token->DirectiveCode == u'r') {
					ResetTextFontDefaults(formatting, callbacks.Fonts);
				} else if (token->Kind == TextMarkupKind::PercentDirective && token->DirectiveCode == u'p') {
					if (!options.IgnoreStyle) {
						formatting.Current.Pitch = token->Payload.empty() ? formatting.Defaults.Pitch : static_cast<float>(std::bit_cast<std::int32_t>(ReadContentInteger(token->Payload, 10)));
					}
				} else if (token->Kind == TextMarkupKind::PercentDirective) {
					if (!options.IgnoreStyle) {
						formatting.Current.Alignment = 0;
					}
				} else if (token->Kind == TextMarkupKind::LineBreak) {
					accepted = FinishLine(formatting, options, render_size);
					indentation.FirstEligible = true;
				} else {
					TextIndentationMatch match;
					// Escaped units append literally and clear eligibility without entering native034793.
					if (request.Indent != 0 && token->Kind == TextMarkupKind::CodeUnit) {
						match.Begin = FindContentPunctuation(options.Begin, token->CodeUnit, work);
						match.End = FindContentPunctuation(options.End, token->CodeUnit, work);
					}
					if (request.Indent < 0 && indentation.FirstOrdinary && match.Begin != kNoPunctuationIndex) {
						// Native03489b performs a distinct provider measurement before030fa0 shapes the unit.
						const auto measured = callbacks.Fonts.MeasureNormal(token->CodeUnit);
						const auto shifted = m_Cursor.X - measured.Width;
						if (!std::isfinite(measured.Width) || !std::isfinite(shifted)) {
							throw std::runtime_error("text opening punctuation measurement exceeds its finite cursor domain");
						}
						m_Cursor.X = shifted;
					}

					accepted = AppendCodeUnit(token->CodeUnit, formatting, options, render_size, callbacks, delay, work);
					indentation.Complete(match, accepted, options, m_Cursor.X, m_LineStart, work);
				}
				if (!accepted) {
					break;
				}
			}
			Retime(request, delay);
			m_Plan = {};
			// The native query table is already available when Render returns; done later rebuilds its order.
			m_Plan.RevealTimes.reserve(m_ElementCount);
			for (const auto& line : m_History) {
				for (const auto& element : line.Elements) {
					m_Plan.RevealTimes.push_back(element.RevealTime);
				}
			}
			for (const auto& element : m_Current) {
				m_Plan.RevealTimes.push_back(element.RevealTime);
			}
			m_Operating = false;
			return accepted;
		} catch (...) {
			m_Operating = false;
			throw;
		}
	}

	void TextContentOwner::RebuildPlan() {
		std::vector<std::vector<TextRenderElement>> storage(m_History.size());
		std::vector<TextRenderLine> lines;
		lines.reserve(m_History.size());
		std::uint32_t id = 0;
		for (std::size_t index = 0; index < m_History.size(); ++index) {
			for (const auto& element : m_History[index].Elements) {
				TextRenderElement row;
				row.ElementId = id++;
				row.Geometry = element.Geometry;
				row.Text = element.Text;
				row.RevealTime = element.RevealTime;
				row.LinkIndex = element.LinkIndex;
				storage[index].push_back(row);
			}
			lines.push_back({storage[index]});
		}
		m_Plan = PrepareTextRenderPlan(lines, {}, m_Boundaries, m_SegmentCount);
		m_Boundaries = m_Plan.RevealBoundaries;
	}

	void TextContentOwner::Finalize(const TextFormattingState& formatting, const TextProcessingOptions& options, TextCursor render_size) {
		if (m_Operating || m_Clearing) {
			throw std::runtime_error("text content finalization cannot reenter its retained owner");
		}
		CaptureRenderCursor();
		if (!m_Current.empty()) {
			static_cast<void>(FinishLine(formatting, options, render_size));
		}
		std::vector<std::vector<TextBlockElement>> storage(m_History.size());
		std::vector<std::vector<TextGlyphGeometry>> ruby_storage;
		ruby_storage.reserve(m_ElementCount);
		std::vector<TextBlockLine> lines;
		lines.reserve(m_History.size());
		for (std::size_t index = 0; index < m_History.size(); ++index) {
			for (const auto& element : m_History[index].Elements) {
				auto& ruby = ruby_storage.emplace_back();
				ruby.reserve(element.Ruby.size());
				for (const auto& annotation : element.Ruby) {
					ruby.push_back(annotation.Geometry);
				}
				storage[index].push_back({element.Geometry, ruby});
			}
			lines.push_back({m_History[index].Bounds, storage[index]});
		}
		TextBlockGeometrySettings settings;
		settings.Alignment = DecodeTextAlignment(formatting.Current.VerticalAlignment);
		settings.RenderWidth = render_size.X;
		settings.RenderHeight = render_size.Y;
		const auto block = FinalizeTextBlockGeometry(settings, lines, m_Bounds);
		m_Bounds = block.Bounds;
		for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
			m_History[line_index].Bounds = lines[line_index].Bounds;
			std::size_t element_index = 0;
			for (auto& element : m_History[line_index].Elements) {
				element.Geometry = storage[line_index][element_index++].Geometry;
			}
		}
		RebuildPlan();
	}

	void TextContentOwner::BeginClear(TextCursor initial_cursor) {
		if (m_Operating || m_Clearing) {
			throw std::runtime_error("text content cannot clear during its font/shaping callback");
		}
		m_Clearing = true;
		m_ElementCount -= m_Current.size();
		m_Current.clear();
		ClearPendingRuby();
		m_Cursor = initial_cursor;
		m_RenderCursor = initial_cursor;
		m_Bounds = {};
		m_Wrap = {};
		m_LineStart = 0;
	}

	void TextContentOwner::CancelClear() noexcept {
		m_Clearing = false;
	}

	void TextContentOwner::Clear() {
		BeginClear();
		FinishClear();
	}

	void TextContentOwner::FinishClear() {
		m_Current.clear();
		m_History.clear();
		m_Processed.clear();
		m_Plan = {};
		m_Bounds = {};
		m_Wrap = {};
		m_Reveal = {};
		m_Boundaries.clear();
		m_SegmentCount = 0;
		m_ElementCount = 0;
		m_RenderedElementCount = 0;
		m_RenderDelay = 0;
		m_LineStart = 0;
		m_Overflowed = false;
		m_Clearing = false;
	}

	std::span<const TextContentLine> TextContentOwner::Lines() const noexcept {
		return m_History;
	}
	const std::deque<TextContentElement>& TextContentOwner::CurrentLine() const noexcept {
		return m_Current;
	}
	const std::u16string& TextContentOwner::ProcessedText() const noexcept {
		return m_Processed;
	}
	const TextRenderPlan& TextContentOwner::Plan() const noexcept {
		return m_Plan;
	}
	std::span<const TextRevealBoundary> TextContentOwner::RevealBoundaries() const noexcept {
		return m_Boundaries;
	}

	TextBounds TextContentOwner::Bounds() const noexcept {
		return m_Bounds;
	}
	TextCursor TextContentOwner::Cursor() const noexcept {
		return m_Cursor;
	}

	TextCursor TextContentOwner::RenderCursor() const noexcept {
		return m_RenderCursor;
	}

	void TextContentOwner::CaptureRenderCursor() noexcept {
		m_RenderCursor = m_Cursor;
	}

	bool TextContentOwner::Overflowed() const noexcept {
		return m_Overflowed;
	}
	std::size_t TextContentOwner::ElementCount() const noexcept {
		return m_ElementCount;
	}

	std::uint32_t TextContentOwner::RenderedElementCount() const noexcept {
		return m_RenderedElementCount;
	}

	float TextContentOwner::RenderDelay(float time_scale) const noexcept {
		return m_RenderDelay * time_scale;
	}

	std::uint32_t TextContentOwner::CalculateShownElementCount(std::int32_t elapsed_time, float time_scale) const {
		return CalculateTextShownElementCount(m_Plan.RevealTimes, elapsed_time, time_scale);
	}

} // namespace sgre::text
