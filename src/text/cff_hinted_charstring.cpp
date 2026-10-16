#include "sgre/text/cff_hinted_charstring.hpp"

#include <array>
#include <bit>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::text {

	static constexpr std::size_t kMaximumCharstringOperands = 48;
	static constexpr std::size_t kMaximumCharstringDepth = 16;
	static constexpr std::size_t kMaximumCharstringBytes = 4 * 1024 * 1024;
	static constexpr std::size_t kMaximumContourCallbacks = 65536;

	static std::int32_t AddFixed(std::int32_t first, std::int32_t second) {
		return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) + std::bit_cast<std::uint32_t>(second));
	}

	static CffFixedPoint AddPoint(CffFixedPoint point, std::int32_t dx, std::int32_t dy) {
		point.X16Dot16 = AddFixed(point.X16Dot16, dx);
		point.Y16Dot16 = AddFixed(point.Y16Dot16, dy);

		return point;
	}

	class CffHintedCharstringReader {
	public:
		CffHintedCharstringReader(const CffCharstringPrograms& programs, const CffHintedCharstringConfiguration& configuration) : m_Programs(programs), m_Configuration(configuration), m_Owner(configuration.Contour) {
			CffCharstringNumber width;
			width.Value = configuration.DefaultWidthFontUnits;
			width.Kind = CffCharstringNumberKind::Integer;
			m_Width.Width16Dot16 = ConvertCffCharstringNumberTo16Dot16(width);
		}

		CffHintedCharstring Read() {
			try {
				(void)Execute(m_Programs.Glyph, 0, false);
				m_Result.Width = m_Width;
				m_Result.InterpreterLogicalPoint = m_Current;
				m_Result.FinalState = m_Owner.State();

				return std::move(m_Result);
			} catch (const std::runtime_error& error) {
				throw std::runtime_error("CFF hinted charstring at program byte " + std::to_string(m_ProgramByte) + ", subroutine depth " + std::to_string(m_ProgramDepth) + ": " + error.what());
			}
		}

	private:
		std::uint8_t ReadByte(std::span<const std::uint8_t> program, std::size_t& offset) {
			if (offset >= program.size()) {
				throw std::runtime_error("CFF hinted charstring operand or mask is truncated");
			}
			if (m_BytesRead == kMaximumCharstringBytes) {
				throw std::runtime_error("CFF hinted charstring exceeds its byte-work limit");
			}
			++m_BytesRead;

			return program[offset++];
		}

		CffCharstringNumber ReadNumber(std::span<const std::uint8_t> program, std::size_t& offset, std::uint8_t first) {
			CffCharstringNumber number;
			number.Kind = CffCharstringNumberKind::Integer;
			if (first >= 32 && first <= 246) {
				number.Value = std::int32_t(first) - 139;
			} else if (first >= 247 && first <= 250) {
				number.Value = (std::int32_t(first) - 247) * 256 + ReadByte(program, offset) + 108;
			} else if (first >= 251 && first <= 254) {
				number.Value = -(std::int32_t(first) - 251) * 256 - ReadByte(program, offset) - 108;
			} else if (first == 28) {
				const auto high = ReadByte(program, offset);
				const auto low = ReadByte(program, offset);
				number.Value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>((std::uint16_t(high) << 8) | low));
			} else {
				std::uint32_t word = 0;
				for (std::size_t byte = 0; byte < 4; ++byte) {
					word = (word << 8) | ReadByte(program, offset);
				}
				number.Value = std::bit_cast<std::int32_t>(word);
				number.Kind = CffCharstringNumberKind::Fixed16Dot16;
			}

			return number;
		}

		void PushOperand(CffCharstringNumber number) {
			if (m_OperandCount == m_Operands.size()) {
				throw std::runtime_error("CFF hinted charstring exceeds its operand-stack limit");
			}

			m_Operands[m_OperandCount++] = number;
		}

		std::int32_t Operand(std::size_t index) const {
			if (index >= m_OperandCount) {
				throw std::runtime_error("CFF hinted drawing operand exceeds its stack");
			}

			return ConvertCffCharstringNumberTo16Dot16(m_Operands[index]);
		}

		void AddCallbacks(const CffContourOwnerStep& step) {
			if (step.CallbackCount > kMaximumContourCallbacks - m_Result.Callbacks.size()) {
				throw std::runtime_error("CFF hinted charstring exceeds its callback limit");
			}

			for (std::size_t index = 0; index < step.CallbackCount; ++index) {
				m_Result.Callbacks.push_back(step.Callbacks[index]);
			}
		}

		void ReadWidth(bool supplied) {
			if (m_Width.Read) {
				return;
			}
			if (supplied) {
				CffCharstringNumber nominal;
				nominal.Value = m_Configuration.NominalWidthFontUnits;
				nominal.Kind = CffCharstringNumberKind::Integer;
				m_Width.Width16Dot16 = AddFixed(ConvertCffCharstringNumberTo16Dot16(nominal), Operand(0));
				for (std::size_t index = 1; index < m_OperandCount; ++index) {
					m_Operands[index - 1] = m_Operands[index];
				}
				--m_OperandCount;
			}
			m_Width.Read = true;
		}

		void Move(std::uint8_t operation) {
			const auto expected = operation == 21 ? std::size_t(2) : std::size_t(1);
			ReadWidth(m_OperandCount == expected + 1);
			if (m_OperandCount != expected) {
				throw std::runtime_error("CFF hinted move operand count is invalid");
			}

			m_Current = AddPoint(m_Current, operation == 4 ? 0 : Operand(0), operation == 22 ? 0 : Operand(expected - 1));
			AddCallbacks(m_Owner.MoveTo(m_Current));
			m_OperandCount = 0;
		}

		void Line(std::int32_t dx, std::int32_t dy) {
			m_Current = AddPoint(m_Current, dx, dy);
			AddCallbacks(m_Owner.LineTo(m_Current));
		}

		void Cubic(std::int32_t dx1, std::int32_t dy1, std::int32_t dx2, std::int32_t dy2, std::int32_t dx3, std::int32_t dy3) {
			const auto first_control = AddPoint(m_Current, dx1, dy1);
			const auto second_control = AddPoint(first_control, dx2, dy2);
			m_Current = AddPoint(second_control, dx3, dy3);
			AddCallbacks(m_Owner.CubicTo(first_control, second_control, m_Current));
		}

		void Stems(bool horizontal) {
			if (m_Owner.State().Mask.Valid) {
				m_OperandCount = 0;
				return;
			}

			CffDeclaredStemBuffer added;
			CffStemDeclarationConfiguration configuration;
			configuration.NominalWidthFontUnits = m_Configuration.NominalWidthFontUnits;
			(void)AppendCffDeclaredStems(added, m_Width, std::span<const CffCharstringNumber>(m_Operands).first(m_OperandCount), configuration);
			if (horizontal) {
				m_Owner.AppendHorizontalStems(std::span<const CffStemHint>(added.Stems).first(added.Count));
			} else {
				m_Owner.AppendVerticalStemCount(added.Count);
			}
			m_OperandCount = 0;
		}

		void Mask(std::span<const std::uint8_t> program, std::size_t& offset, bool counter) {
			if (m_Owner.State().Mask.Valid && m_OperandCount > 1) {
				throw std::runtime_error("CFF hinted mask operands after its primary mask remain unsupported");
			}

			Stems(false);
			const auto& state = m_Owner.State();
			const auto byte_count = (state.HorizontalStems.Count + state.VerticalStemCount + 7) / 8;
			std::array<std::uint8_t, 12> bytes{};
			for (std::size_t index = 0; index < byte_count; ++index) {
				bytes[index] = ReadByte(program, offset);
			}
			if (counter) {
				(void)m_Owner.ApplyCounterMask(std::span<const std::uint8_t>(bytes).first(byte_count));
			} else {
				(void)m_Owner.UpdateHintMask(std::span<const std::uint8_t>(bytes).first(byte_count));
			}
		}
		void RequireOperandGroups(std::size_t minimum, std::size_t remainder, std::size_t divisor) const {
			if (m_OperandCount < minimum || m_OperandCount % divisor != remainder) {
				throw std::runtime_error("CFF drawing operator has an invalid operand count");
			}
		}

		void RelativeLines() {
			RequireOperandGroups(2, 0, 2);
			for (std::size_t index = 0; index < m_OperandCount; index += 2) {
				Line(Operand(index), Operand(index + 1));
			}
			m_OperandCount = 0;
		}

		void AlternatingLines(bool horizontal) {
			if (m_OperandCount == 0) {
				throw std::runtime_error("CFF alternating line operator has no operands");
			}

			for (std::size_t index = 0; index < m_OperandCount; ++index) {
				Line(horizontal ? Operand(index) : 0, horizontal ? 0 : Operand(index));
				horizontal = !horizontal;
			}
			m_OperandCount = 0;
		}

		void RelativeCurves() {
			RequireOperandGroups(6, 0, 6);
			for (std::size_t index = 0; index < m_OperandCount; index += 6) {
				Cubic(Operand(index), Operand(index + 1), Operand(index + 2), Operand(index + 3), Operand(index + 4), Operand(index + 5));
			}
			m_OperandCount = 0;
		}

		void CurveThenLine() {
			RequireOperandGroups(8, 2, 6);
			const auto line = m_OperandCount - 2;
			for (std::size_t index = 0; index < line; index += 6) {
				Cubic(Operand(index), Operand(index + 1), Operand(index + 2), Operand(index + 3), Operand(index + 4), Operand(index + 5));
			}
			Line(Operand(line), Operand(line + 1));
			m_OperandCount = 0;
		}

		void LineThenCurve() {
			RequireOperandGroups(8, 0, 2);
			const auto curve = m_OperandCount - 6;
			for (std::size_t index = 0; index < curve; index += 2) {
				Line(Operand(index), Operand(index + 1));
			}
			Cubic(Operand(curve), Operand(curve + 1), Operand(curve + 2), Operand(curve + 3), Operand(curve + 4), Operand(curve + 5));
			m_OperandCount = 0;
		}

		void SameAxisCurves(bool horizontal) {
			if (m_OperandCount < 4 || (m_OperandCount % 4 != 0 && m_OperandCount % 4 != 1)) {
				throw std::runtime_error("CFF same-axis curve operand count is invalid");
			}

			std::size_t index = 0;
			std::int32_t initial_other_axis = 0;
			if (m_OperandCount % 4 == 1) {
				initial_other_axis = Operand(index++);
			}
			for (; index < m_OperandCount; index += 4) {
				if (horizontal) {
					Cubic(Operand(index), initial_other_axis, Operand(index + 1), Operand(index + 2), Operand(index + 3), 0);
				} else {
					Cubic(initial_other_axis, Operand(index), Operand(index + 1), Operand(index + 2), 0, Operand(index + 3));
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
				const auto extra = m_OperandCount - index == 5 ? Operand(index + 4) : 0;
				if (horizontal) {
					Cubic(Operand(index), 0, Operand(index + 1), Operand(index + 2), extra, Operand(index + 3));
				} else {
					Cubic(0, Operand(index), Operand(index + 1), Operand(index + 2), Operand(index + 3), extra);
				}
				index += m_OperandCount - index == 5 ? 5 : 4;
				horizontal = !horizontal;
			}
			m_OperandCount = 0;
		}

		bool CallSubroutine(bool global, std::size_t depth) {
			if (m_OperandCount == 0 || m_Operands[m_OperandCount - 1].Kind != CffCharstringNumberKind::Integer) {
				throw std::runtime_error("CFF hinted subroutine index must be an integer operand");
			}

			const auto subroutines = global ? m_Programs.GlobalSubroutines : m_Programs.LocalSubroutines;
			const auto bias = subroutines.size() < 1240 ? 107 : subroutines.size() < 33900 ? 1131
			                                                                               : 32768;
			const auto index = std::int64_t(m_Operands[--m_OperandCount].Value) + bias;
			if (index < 0 || std::uint64_t(index) >= subroutines.size()) {
				throw std::runtime_error("CFF hinted subroutine index exceeds its selected dictionary");
			}

			return Execute(subroutines[static_cast<std::size_t>(index)], depth + 1, true);
		}

		void EndGlyph() {
			if (m_OperandCount == 1 && !m_Width.Read) {
				CffCharstringNumber nominal;
				nominal.Value = m_Configuration.NominalWidthFontUnits;
				nominal.Kind = CffCharstringNumberKind::Integer;
				m_Width.Width16Dot16 = AddFixed(ConvertCffCharstringNumberTo16Dot16(nominal), Operand(0));
			} else if (m_OperandCount != 0) {
				throw std::runtime_error("CFF hinted endchar composite or unused operands remain unsupported");
			}

			AddCallbacks(m_Owner.CloseContour());
		}

		bool Execute(std::span<const std::uint8_t> program, std::size_t depth, bool subroutine) {
			if (depth > kMaximumCharstringDepth) {
				throw std::runtime_error("CFF hinted subroutine nesting exceeds its depth limit");
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
						throw std::runtime_error("CFF hinted root glyph contains a subroutine return");
					}

					return false;
				case 12:
					throw std::runtime_error("CFF hinted escaped operator " + std::to_string(ReadByte(program, offset)) + " remains unsupported");
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
					throw std::runtime_error("CFF hinted operator " + std::to_string(operation) + " is reserved or unsupported");
				}
			}

			throw std::runtime_error(subroutine ? "CFF hinted subroutine lacks return or endchar" : "CFF hinted glyph lacks endchar");
		}

		const CffCharstringPrograms& m_Programs;
		const CffHintedCharstringConfiguration& m_Configuration;
		CffContourOwner m_Owner;
		std::array<CffCharstringNumber, kMaximumCharstringOperands> m_Operands{};
		std::size_t m_OperandCount = 0;
		std::size_t m_BytesRead = 0;
		std::size_t m_ProgramByte = 0;
		std::size_t m_ProgramDepth = 0;
		CffCharstringWidth m_Width;
		CffFixedPoint m_Current;
		CffHintedCharstring m_Result;
	};

	CffHintedCharstring ExecuteCffHintedCharstring(const CffCharstringPrograms& programs, const CffHintedCharstringConfiguration& configuration) {
		CffHintedCharstringReader reader(programs, configuration);

		return reader.Read();
	}

} // namespace sgre::text
