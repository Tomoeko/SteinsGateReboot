#include "sgre/resource/cff_outline.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::resource {

	static constexpr std::size_t kMaximumCharstringOperands = 48;
	static constexpr std::size_t kMaximumCharstringDepth = 10;
	static constexpr std::size_t kMaximumCharstringBytes = 4 * 1024 * 1024;
	static constexpr std::size_t kMaximumOutlineCommands = 65536;
	static constexpr std::size_t kMaximumHintEvents = 65536;
	static constexpr std::uint16_t kMaximumCharstringStems = 96;

	class CffOutlineReader {
	public:
		explicit CffOutlineReader(const CffFont& font, std::uint32_t glyph_index) : m_Font(font), m_Dictionary(font.FontDictionaries()[font.GlyphFontDictionary(glyph_index)]) {
		}

		CffOutline Read(std::uint32_t glyph_index) {
			try {
				static_cast<void>(Execute(m_Font.GlyphProgram(glyph_index), 0, false));

				return std::move(m_Outline);
			} catch (const std::runtime_error& error) {
				throw std::runtime_error("at program byte " + std::to_string(m_ProgramByte) + ", subroutine depth " + std::to_string(m_ProgramDepth) + ": " + error.what());
			}
		}

	private:
		std::uint8_t ReadByte(std::span<const std::uint8_t> program, std::size_t& offset) {
			if (offset >= program.size()) {
				throw std::runtime_error("CFF charstring operand or mask is truncated");
			}
			if (m_BytesRead >= kMaximumCharstringBytes) {
				throw std::runtime_error("CFF charstring execution exceeds its byte-work limit");
			}
			++m_BytesRead;

			return program[offset++];
		}

		double ReadNumber(std::span<const std::uint8_t> program, std::size_t& offset, std::uint8_t first) {
			if (first >= 32 && first <= 246) {
				return std::int32_t(first) - 139;
			}
			if (first >= 247 && first <= 250) {
				return (std::int32_t(first) - 247) * 256 + ReadByte(program, offset) + 108;
			}
			if (first >= 251 && first <= 254) {
				return -(std::int32_t(first) - 251) * 256 - ReadByte(program, offset) - 108;
			}
			if (first == 28) {
				const auto high = ReadByte(program, offset);
				const auto low = ReadByte(program, offset);
				const auto word = static_cast<std::uint16_t>((std::uint16_t(high) << 8) | low);

				return std::bit_cast<std::int16_t>(word);
			}

			std::uint32_t word = 0;
			for (std::size_t byte = 0; byte < 4; ++byte) {
				word = (word << 8) | ReadByte(program, offset);
			}

			return std::bit_cast<std::int32_t>(word) / 65536.0;
		}

		void PushOperand(double value) {
			if (m_OperandCount >= m_Operands.size()) {
				throw std::runtime_error("CFF charstring operand stack exceeds its limit");
			}
			m_Operands[m_OperandCount++] = value;
		}

		void ReadWidth(bool supplied) {
			if (m_WidthRead) {
				return;
			}

			m_Outline.AdvanceWidth = m_Dictionary.DefaultWidth;
			if (supplied) {
				if (m_OperandCount == 0) {
					throw std::runtime_error("CFF charstring width operand is missing");
				}
				m_Outline.AdvanceWidth = m_Dictionary.NominalWidth + m_Operands[0];
				for (std::size_t index = 1; index < m_OperandCount; ++index) {
					m_Operands[index - 1] = m_Operands[index];
				}
				--m_OperandCount;
			}
			if (!std::isfinite(m_Outline.AdvanceWidth)) {
				throw std::runtime_error("CFF charstring advance width is nonfinite");
			}
			m_WidthRead = true;
		}

		void RequireOperandGroups(std::size_t minimum, std::size_t remainder, std::size_t divisor) const {
			if (m_OperandCount < minimum || m_OperandCount % divisor != remainder) {
				throw std::runtime_error("CFF drawing operator has an invalid operand count");
			}
		}

		void AddCommand(CffOutlineCommand command) {
			if (m_Outline.Commands.size() >= kMaximumOutlineCommands) {
				throw std::runtime_error("CFF outline command count exceeds its preparation limit");
			}
			for (const auto& point : command.Points) {
				if (!std::isfinite(point.X) || !std::isfinite(point.Y)) {
					throw std::runtime_error("CFF outline coordinate is nonfinite");
				}
			}
			m_Outline.Commands.push_back(command);
		}

		void CloseContour() {
			if (!m_ContourOpen) {
				return;
			}

			AddCommand({});
			m_ContourOpen = false;
		}

		void Move(std::uint8_t operation) {
			const auto expected = operation == 21 ? std::size_t(2) : std::size_t(1);
			ReadWidth(m_OperandCount == expected + 1);
			if (m_OperandCount != expected) {
				throw std::runtime_error("CFF move operator has an invalid operand count");
			}
			CloseContour();
			m_Current.X += operation == 4 ? 0 : m_Operands[0];
			m_Current.Y += operation == 22 ? 0 : m_Operands[expected - 1];
			CffOutlineCommand command;
			command.Kind = CffOutlineCommandKind::MoveTo;
			command.Points[0] = m_Current;
			AddCommand(command);
			m_ContourOpen = true;
			m_OperandCount = 0;
		}

		void Line(double dx, double dy) {
			if (!m_ContourOpen) {
				throw std::runtime_error("CFF line appears before its contour move");
			}

			m_Current.X += dx;
			m_Current.Y += dy;
			CffOutlineCommand command;
			command.Kind = CffOutlineCommandKind::LineTo;
			command.Points[0] = m_Current;
			AddCommand(command);
		}

		void Cubic(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3) {
			if (!m_ContourOpen) {
				throw std::runtime_error("CFF curve appears before its contour move");
			}

			CffOutlineCommand command;
			command.Kind = CffOutlineCommandKind::CubicTo;
			command.Points[0] = {m_Current.X + dx1, m_Current.Y + dy1};
			command.Points[1] = {command.Points[0].X + dx2, command.Points[0].Y + dy2};
			command.Points[2] = {command.Points[1].X + dx3, command.Points[1].Y + dy3};
			m_Current = command.Points[2];
			AddCommand(command);
		}

		void AddHintEvent(CffOutlineHintEvent event) {
			if (m_Outline.HintEvents.size() >= kMaximumHintEvents) {
				throw std::runtime_error("CFF hint event count exceeds its preparation limit");
			}

			event.CommandIndex = m_Outline.Commands.size();
			event.HorizontalStemCount = m_HorizontalStemCount;
			event.VerticalStemCount = m_VerticalStemCount;
			m_Outline.HintEvents.push_back(std::move(event));
		}

		void Stems(bool horizontal) {
			if (m_PrimaryMaskValid) {
				m_OperandCount = 0;
				return;
			}

			const bool supplied_width = (m_OperandCount & 1) != 0;
			if (m_WidthRead && supplied_width) {
				for (std::size_t index = 1; index < m_OperandCount; ++index) {
					m_Operands[index - 1] = m_Operands[index];
				}
				--m_OperandCount;
			} else {
				ReadWidth(supplied_width);
			}
			RequireOperandGroups(0, 0, 2);
			const auto added = m_OperandCount / 2;
			if (added > kMaximumCharstringStems - m_Outline.StemCount) {
				throw std::runtime_error("CFF stem count exceeds the Type 2 limit");
			}

			CffOutlineHintEvent event;
			event.Kind = horizontal ? CffOutlineHintEventKind::DeclareHorizontalStems : CffOutlineHintEventKind::DeclareVerticalStems;
			event.Stems.reserve(added);
			double position = 0;
			for (std::size_t index = 0; index < m_OperandCount; index += 2) {
				CffOutlineStem stem;
				stem.StartFontUnits = position + m_Operands[index];
				stem.EndFontUnits = stem.StartFontUnits + m_Operands[index + 1];
				if (!std::isfinite(stem.StartFontUnits) || !std::isfinite(stem.EndFontUnits)) {
					throw std::runtime_error("CFF declared stem coordinate is nonfinite");
				}

				position = stem.EndFontUnits;
				event.Stems.push_back(stem);
			}
			m_Outline.StemCount += static_cast<std::uint16_t>(added);
			if (horizontal) {
				m_HorizontalStemCount += static_cast<std::uint16_t>(added);
			} else {
				m_VerticalStemCount += static_cast<std::uint16_t>(added);
			}
			m_OperandCount = 0;
			if (added != 0) {
				AddHintEvent(std::move(event));
			}
		}

		void Mask(std::span<const std::uint8_t> program, std::size_t& offset, bool counter_mask) {
			if (m_PrimaryMaskValid && m_OperandCount > 1) {
				throw std::runtime_error("CFF mask operands after its primary mask remain unsupported");
			}

			Stems(false);
			CffOutlineHintEvent event;
			event.Kind = counter_mask ? CffOutlineHintEventKind::CounterMask : CffOutlineHintEventKind::HintMask;
			const auto byte_count = (std::size_t(m_Outline.StemCount) + 7) / 8;
			for (std::size_t index = 0; index < byte_count; ++index) {
				event.MaskBytes[index] = ReadByte(program, offset);
			}
			if (!counter_mask) {
				m_PrimaryMaskValid = true;
			}

			AddHintEvent(std::move(event));
		}

		void RelativeLines() {
			RequireOperandGroups(2, 0, 2);
			for (std::size_t index = 0; index < m_OperandCount; index += 2) {
				Line(m_Operands[index], m_Operands[index + 1]);
			}
			m_OperandCount = 0;
		}

		void AlternatingLines(bool horizontal) {
			if (m_OperandCount == 0) {
				throw std::runtime_error("CFF alternating line operator has no operands");
			}

			for (std::size_t index = 0; index < m_OperandCount; ++index) {
				Line(horizontal ? m_Operands[index] : 0, horizontal ? 0 : m_Operands[index]);
				horizontal = !horizontal;
			}
			m_OperandCount = 0;
		}

		void RelativeCurves() {
			RequireOperandGroups(6, 0, 6);
			for (std::size_t index = 0; index < m_OperandCount; index += 6) {
				Cubic(m_Operands[index], m_Operands[index + 1], m_Operands[index + 2], m_Operands[index + 3], m_Operands[index + 4], m_Operands[index + 5]);
			}
			m_OperandCount = 0;
		}

		void CurveThenLine() {
			RequireOperandGroups(8, 2, 6);
			const auto line = m_OperandCount - 2;
			for (std::size_t index = 0; index < line; index += 6) {
				Cubic(m_Operands[index], m_Operands[index + 1], m_Operands[index + 2], m_Operands[index + 3], m_Operands[index + 4], m_Operands[index + 5]);
			}
			Line(m_Operands[line], m_Operands[line + 1]);
			m_OperandCount = 0;
		}

		void LineThenCurve() {
			RequireOperandGroups(8, 0, 2);
			const auto curve = m_OperandCount - 6;
			for (std::size_t index = 0; index < curve; index += 2) {
				Line(m_Operands[index], m_Operands[index + 1]);
			}
			Cubic(m_Operands[curve], m_Operands[curve + 1], m_Operands[curve + 2], m_Operands[curve + 3], m_Operands[curve + 4], m_Operands[curve + 5]);
			m_OperandCount = 0;
		}

		void SameAxisCurves(bool horizontal) {
			if (m_OperandCount < 4 || (m_OperandCount % 4 != 0 && m_OperandCount % 4 != 1)) {
				throw std::runtime_error("CFF same-axis curve operand count is invalid");
			}

			std::size_t index = 0;
			double initial_other_axis = 0;
			if (m_OperandCount % 4 == 1) {
				initial_other_axis = m_Operands[index++];
			}
			for (; index < m_OperandCount; index += 4) {
				if (horizontal) {
					Cubic(m_Operands[index], initial_other_axis, m_Operands[index + 1], m_Operands[index + 2], m_Operands[index + 3], 0);
				} else {
					Cubic(initial_other_axis, m_Operands[index], m_Operands[index + 1], m_Operands[index + 2], 0, m_Operands[index + 3]);
				}
				initial_other_axis = 0;
			}
			m_OperandCount = 0;
		}

		void AlternatingAxisCurves(bool horizontal) {
			if (m_OperandCount < 4 || (m_OperandCount % 4 != 0 && m_OperandCount % 4 != 1)) {
				throw std::runtime_error("CFF alternating-axis curve operand count is invalid");
			}

			for (std::size_t index = 0; index < m_OperandCount;) {
				const auto extra = m_OperandCount - index == 5 ? m_Operands[index + 4] : 0;
				if (horizontal) {
					Cubic(m_Operands[index], 0, m_Operands[index + 1], m_Operands[index + 2], extra, m_Operands[index + 3]);
				} else {
					Cubic(0, m_Operands[index], m_Operands[index + 1], m_Operands[index + 2], m_Operands[index + 3], extra);
				}
				index += m_OperandCount - index == 5 ? 5 : 4;
				horizontal = !horizontal;
			}
			m_OperandCount = 0;
		}

		bool CallSubroutine(bool global, std::size_t depth) {
			if (m_OperandCount == 0) {
				throw std::runtime_error("CFF subroutine call has no index operand");
			}
			const auto subroutines = global ? m_Font.GlobalSubroutines() : std::span<const std::span<const std::uint8_t>>(m_Dictionary.LocalSubroutines);
			const auto operand = m_Operands[--m_OperandCount];
			const auto bias = subroutines.size() < 1240 ? 107 : subroutines.size() < 33900 ? 1131
			                                                                               : 32768;
			const auto index = operand + bias;
			if (index < 0 || index >= double(subroutines.size()) || index != std::floor(index)) {
				throw std::runtime_error("CFF subroutine index exceeds its selected dictionary");
			}

			return Execute(subroutines[static_cast<std::size_t>(index)], depth + 1, true);
		}

		void EndGlyph() {
			ReadWidth(m_OperandCount == 1 || m_OperandCount == 5);
			if (m_OperandCount == 4) {
				throw std::runtime_error("CFF composite endchar operands remain unsupported");
			}
			if (m_OperandCount != 0) {
				throw std::runtime_error("CFF endchar operand count is invalid");
			}

			CloseContour();
		}

		bool Execute(std::span<const std::uint8_t> program, std::size_t depth, bool subroutine) {
			if (depth > kMaximumCharstringDepth) {
				throw std::runtime_error("CFF subroutine nesting exceeds its depth limit");
			}

			std::size_t offset = 0;
			while (offset < program.size()) {
				m_ProgramByte = offset;
				m_ProgramDepth = depth;
				const auto operation = ReadByte(program, offset);
				if (operation >= 32 || operation == 28) {
					PushOperand(ReadNumber(program, offset, operation));
					continue;
				}

				switch (operation) {
				case 1:
				case 3:
				case 18:
				case 23:
					Stems(operation == 1 || operation == 18);
					break;
				case 4:
				case 21:
				case 22:
					Move(operation);
					break;
				case 5:
					RelativeLines();
					break;
				case 6:
				case 7:
					AlternatingLines(operation == 6);
					break;
				case 8:
					RelativeCurves();
					break;
				case 10:
				case 29:
					if (CallSubroutine(operation == 29, depth)) {
						return true;
					}
					break;
				case 11:
					if (!subroutine) {
						throw std::runtime_error("CFF root glyph contains a subroutine return");
					}

					return false;
				case 12:
					throw std::runtime_error("CFF escaped charstring operator " + std::to_string(ReadByte(program, offset)) + " remains unsupported");
				case 14:
					EndGlyph();

					return true;
				case 19:
				case 20:
					Mask(program, offset, operation == 20);
					break;
				case 24:
					CurveThenLine();
					break;
				case 25:
					LineThenCurve();
					break;
				case 26:
				case 27:
					SameAxisCurves(operation == 27);
					break;
				case 30:
				case 31:
					AlternatingAxisCurves(operation == 31);
					break;
				default:
					throw std::runtime_error("CFF charstring operator " + std::to_string(operation) + " is reserved or unsupported");
				}
			}

			throw std::runtime_error(subroutine ? "CFF subroutine lacks return or endchar" : "CFF glyph lacks endchar");
		}

		const CffFont& m_Font;
		const CffFontDictionary& m_Dictionary;
		std::array<double, kMaximumCharstringOperands> m_Operands{};
		std::size_t m_OperandCount = 0;
		std::size_t m_BytesRead = 0;
		std::size_t m_ProgramByte = 0;
		std::size_t m_ProgramDepth = 0;
		CffOutlinePoint m_Current;
		bool m_ContourOpen = false;
		bool m_WidthRead = false;
		bool m_PrimaryMaskValid = false;
		std::uint16_t m_HorizontalStemCount = 0;
		std::uint16_t m_VerticalStemCount = 0;
		CffOutline m_Outline;
	};

	CffOutline ReadCffGlyphOutline(const CffFont& font, std::uint32_t glyph_index) {
		try {
			CffOutlineReader reader(font, glyph_index);

			return reader.Read(glyph_index);
		} catch (const std::runtime_error& error) {
			throw std::runtime_error("CFF glyph " + std::to_string(glyph_index) + ": " + error.what());
		}
	}

} // namespace sgre::resource
