#include "sgre/script/native_regexp.hpp"

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sgre::script {
	static constexpr std::int32_t kRepeat = 0x100;
	static constexpr std::int32_t kAlternative = 0x101;
	static constexpr std::int32_t kGroup = 0x102;
	static constexpr std::int32_t kNoncapturingGroup = 0x103;
	static constexpr std::int32_t kAny = 0x104;
	static constexpr std::int32_t kPositiveClass = 0x105;
	static constexpr std::int32_t kClassEscape = 0x106;
	static constexpr std::int32_t kNegativeClass = 0x107;
	static constexpr std::int32_t kRange = 0x108;
	static constexpr std::int32_t kEndAnchor = 0x10a;
	static constexpr std::int32_t kBeginAnchor = 0x10b;
	static constexpr std::int32_t kBoundary = 0x10c;

	struct RegexpNode {
		std::int32_t Type = 0;
		std::int32_t Left = -1;
		std::int32_t Right = -1;
		std::int32_t Next = -1;
	};

	static std::string_view CStringPrefix(std::string_view value) {
		return value.substr(0, value.find('\0'));
	}

	static bool IsDigit(std::uint8_t value) {
		return value >= '0' && value <= '9';
	}

	static bool IsSpace(std::uint8_t value) {
		return value == 32 || (value >= 9 && value <= 13);
	}

	static std::int32_t SignedByte(std::uint8_t value) {
		return value < 128 ? value : std::int32_t(value) - 256;
	}

	static bool MatchesEscape(std::int32_t escape, std::uint8_t value) {
		const auto lower = value >= 'a' && value <= 'z';
		const auto upper = value >= 'A' && value <= 'Z';
		const auto alpha = lower || upper;
		const auto digit = IsDigit(value);
		const auto word = alpha || digit || value == '_';
		const auto hex = digit || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
		const auto control = value < 32 || value == 127;
		const auto punctuation = value >= 33 && value <= 126 && !alpha && !digit;
		switch (escape) {
		case 'a':
			return alpha;
		case 'A':
			return !alpha;
		case 'w':
			return word;
		case 'W':
			return !word;
		case 's':
			return IsSpace(value);
		case 'S':
			return !IsSpace(value);
		case 'd':
			return digit;
		case 'D':
			return !digit;
		case 'x':
			return hex;
		case 'X':
			return !hex;
		case 'c':
			return control;
		case 'C':
			return !control;
		case 'p':
			return punctuation;
		case 'P':
			return !punctuation;
		case 'l':
			return lower;
		case 'u':
			return upper;
		default:
			throw std::runtime_error("regexp has an invalid character-class instruction");
		}
	}

	static std::uint8_t EscapeLiteral(std::uint8_t value) {
		switch (value) {
		case 'f':
			return 12;
		case 'n':
			return 10;
		case 'r':
			return 13;
		case 't':
			return 9;
		case 'v':
			return 11;
		default:
			return value;
		}
	}

	class RegexpParser {
	public:
		RegexpParser(std::string_view pattern, const NativeRegexpLimits& limits) : Pattern(pattern), Limits(limits) {
		}

		std::string_view Pattern;
		const NativeRegexpLimits& Limits;
		std::vector<RegexpNode> Nodes;
		std::size_t Groups = 0;
		std::size_t Position = 0;

		std::uint8_t Current() const {
			return Position < Pattern.size() ? static_cast<std::uint8_t>(Pattern[Position]) : 0;
		}

		std::int32_t Add(std::int32_t type) {
			if (Nodes.size() >= Limits.MaxNodes) {
				throw std::runtime_error("regexp node limit exceeded");
			}

			const auto index = static_cast<std::int32_t>(Nodes.size());
			RegexpNode node;
			node.Type = type;
			if (type == kGroup) {
				node.Right = static_cast<std::int32_t>(Groups++);
			}
			Nodes.push_back(node);

			return index;
		}

		void RequireDepth(std::size_t depth) const {
			if (depth > Limits.MaxDepth) {
				throw std::runtime_error("regexp parser depth limit exceeded");
			}
		}

		std::int32_t Character(bool in_class) {
			const auto value = Current();
			++Position;
			if (value != '\\') {
				if (value < 32 || value > 126) {
					throw std::runtime_error("letter expected");
				}

				return Add(value);
			}

			const auto escaped = Current();
			++Position;
			if (std::string_view("ACDPSWXacdlpsuwx").find(static_cast<char>(escaped)) != std::string_view::npos) {
				const auto node = Add(kClassEscape);
				Nodes[node].Left = escaped;

				return node;
			}
			if (!in_class && (escaped == 'b' || escaped == 'B')) {
				const auto node = Add(kBoundary);
				Nodes[node].Left = escaped;

				return node;
			}

			return Add(SignedByte(EscapeLiteral(escaped)));
		}

		std::int32_t CharacterClass() {
			const auto negative = Current() == '^';
			const auto head = Add(negative ? kNegativeClass : kPositiveClass);
			if (negative) {
				++Position;
			}
			if (Current() == ']') {
				throw std::runtime_error("empty class");
			}

			auto last = head;
			std::int32_t pending = -1;
			// The original enters once even when '[' is the final pattern byte.
			do {
				if (Current() == '-' && pending != -1) {
					++Position;
					const auto range = Add(kRange);
					// 2aadd0 compares the pending node index, rather than its literal value.
					if (pending > Current()) {
						throw std::runtime_error("invalid range");
					}
					if (Nodes[pending].Type == kClassEscape) {
						throw std::runtime_error("cannot use character classes in ranges");
					}

					Nodes[range].Left = Nodes[pending].Type;
					auto end = Current();
					++Position;
					if (end == '\\') {
						end = EscapeLiteral(Current());
						++Position;
					} else if (end < 32 || end > 126) {
						throw std::runtime_error("letter expected");
					}
					Nodes[range].Right = SignedByte(end);
					Nodes[last].Next = range;
					last = range;
					pending = -1;
				} else {
					if (pending != -1) {
						Nodes[last].Next = pending;
						last = pending;
					}
					pending = Character(true);
				}
			} while (Current() != ']' && Position < Pattern.size());

			if (pending != -1) {
				Nodes[last].Next = pending;
			}
			Nodes[head].Left = Nodes[head].Next;
			Nodes[head].Next = -1;

			return head;
		}

		std::uint32_t Number() {
			auto value = std::uint32_t(Current() - '0');
			++Position;
			std::uint32_t multiplier = 10;
			while (IsDigit(Current())) {
				value = value * 10 + Current() - '0';
				++Position;
				if (multiplier == 1000000000) {
					throw std::runtime_error("overflow in numeric constant");
				}
				multiplier *= 10;
			}

			return value;
		}

		std::int32_t Sequence(std::size_t depth) {
			RequireDepth(depth);
			std::int32_t node = -1;
			const auto value = Current();
			if (value == '(') {
				++Position;
				auto type = kGroup;
				if (Current() == '?') {
					++Position;
					if (Current() != ':') {
						throw std::runtime_error("expected paren");
					}
					++Position;
					type = kNoncapturingGroup;
				}
				node = Add(type);
				const auto child = Expression(depth + 1);
				Nodes[node].Left = child;
				if (Current() != ')') {
					throw std::runtime_error("expected paren");
				}
				++Position;
			} else if (value == '[') {
				++Position;
				node = CharacterClass();
				if (Current() != ']') {
					throw std::runtime_error("expected paren");
				}
				++Position;
			} else if (value == '.' || value == '$') {
				++Position;
				node = Add(value == '.' ? kAny : kEndAnchor);
			} else {
				node = Character(false);
			}

			std::uint32_t minimum = 0;
			std::uint32_t maximum = 0;
			const auto quantifier = Current();
			if (quantifier == '*' || quantifier == '+' || quantifier == '?' || quantifier == '{') {
				++Position;
				maximum = quantifier == '?' ? 1 : 65535;
				minimum = quantifier == '+' ? 1 : 0;
				if (quantifier == '{') {
					if (!IsDigit(Current())) {
						throw std::runtime_error("number expected");
					}
					minimum = Number();
					if (Current() == ',') {
						++Position;
						maximum = IsDigit(Current()) ? Number() : 65535;
					} else if (Current() == '}') {
						maximum = minimum;
					} else {
						throw std::runtime_error(", or } expected");
					}
					if (Current() != '}') {
						throw std::runtime_error("expected paren");
					}
					++Position;
				}
				const auto repeat = Add(kRepeat);
				Nodes[repeat].Left = node;
				const auto packed = ((minimum & 65535) << 16) | (maximum & 65535);
				Nodes[repeat].Right = std::bit_cast<std::int32_t>(packed);
				node = repeat;
			}

			const auto next = Current();
			if (next != 0 && next != '|' && next != ')' && next != '*' && next != '+') {
				const auto following = Sequence(depth + 1);
				Nodes[node].Next = following;
			}

			return node;
		}

		std::int32_t Expression(std::size_t depth) {
			RequireDepth(depth);
			std::int32_t node = -1;
			if (Current() == '^') {
				++Position;
				node = Add(kBeginAnchor);
				const auto following = Sequence(depth + 1);
				Nodes[node].Next = following;
			} else {
				node = Sequence(depth + 1);
			}
			if (Current() == '|') {
				++Position;
				const auto alternative = Add(kAlternative);
				Nodes[alternative].Left = node;
				const auto right = Expression(depth + 1);
				Nodes[alternative].Right = right;
				node = alternative;
			}

			return node;
		}
	};

	class NativeRegexp::Implementation {
	public:
		explicit Implementation(std::string_view pattern, NativeRegexpLimits limits) : Limits(limits) {
			if (limits.MaxPatternBytes == 0 || limits.MaxPatternBytes > 1024 * 1024 || limits.MaxTextBytes == 0 || limits.MaxTextBytes > 1024 * 1024 * 1024 || limits.MaxNodes == 0 || limits.MaxNodes > 1024 * 1024 || limits.MaxDepth == 0 || limits.MaxDepth > 1024 || limits.MaxMatchSteps == 0) {
				throw std::runtime_error("regexp requires bounded positive limits");
			}
			if (pattern.size() > Limits.MaxPatternBytes) {
				throw std::runtime_error("regexp pattern byte limit exceeded");
			}

			RegexpParser parser(CStringPrefix(pattern), Limits);
			const auto root = parser.Add(kGroup);
			const auto child = parser.Expression(0);
			parser.Nodes[root].Left = child;
			if (parser.Current() != 0) {
				throw std::runtime_error("unexpected character");
			}
			Nodes = std::move(parser.Nodes);
			Captures.resize(parser.Groups);
		}

		NativeRegexpLimits Limits;
		std::vector<RegexpNode> Nodes;
		std::vector<NativeRegexpCapture> Captures;

		struct MatchState {
			Implementation& Program;
			std::string_view Text;
			std::size_t Start = 0;
			std::size_t CaptureCursor = 0;
			std::size_t Steps = 0;

			void Step(std::size_t depth) {
				if (++Steps > Program.Limits.MaxMatchSteps || depth > Program.Limits.MaxDepth) {
					throw std::runtime_error("regexp matching work or depth limit exceeded");
				}
			}

			std::uint8_t Byte(std::size_t position) const {
				if (position > Text.size()) {
					throw std::runtime_error("regexp attempted to read beyond the input terminator");
				}

				return position == Text.size() ? 0 : static_cast<std::uint8_t>(Text[position]);
			}

			bool ClassMatches(std::int32_t index, std::size_t position, std::size_t depth) {
				const auto value = Byte(position);
				while (index != -1) {
					Step(depth);
					const auto& node = Program.Nodes.at(static_cast<std::size_t>(index));
					if ((node.Type == kRange && SignedByte(value) >= node.Left && SignedByte(value) <= node.Right) || (node.Type == kClassEscape && MatchesEscape(node.Left, value)) || (node.Type != kRange && node.Type != kClassEscape && node.Type == SignedByte(value))) {
						return true;
					}
					index = node.Next;
				}

				return false;
			}

			std::optional<std::size_t> Chain(std::int32_t index, std::size_t position, std::int32_t following, bool pass_next, std::size_t depth) {
				while (index != -1) {
					const auto& node = Program.Nodes.at(static_cast<std::size_t>(index));
					const auto result = Node(index, position, pass_next ? (node.Next == -1 ? following : node.Next) : -1, depth + 1);
					if (!result) {
						return std::nullopt;
					}
					position = *result;
					index = node.Next;
				}

				return position;
			}

			std::optional<std::size_t> Repeat(const RegexpNode& node, std::size_t position, std::int32_t inherited, std::size_t depth) {
				const auto packed = static_cast<std::uint32_t>(node.Right);
				const auto minimum = packed >> 16;
				const auto maximum = packed & 65535;
				const auto following = node.Next == -1 ? inherited : node.Next;
				std::uint32_t count = 0;
				while (count == 65535 || count < maximum) {
					const auto result = Node(node.Left, position, following, depth + 1);
					if (!result) {
						break;
					}
					position = *result;
					++count;
					// 2ab926 probes the following instruction without consuming its result or undoing captures.
					if (following != -1 && !(Program.Nodes[following].Type == kRepeat && (static_cast<std::uint32_t>(Program.Nodes[following].Right) >> 16) == 0)) {
						auto after = Program.Nodes[following].Next;
						if (after == -1 && inherited != -1) {
							after = Program.Nodes[inherited].Next;
						}
						if (Node(following, position, after, depth + 1) && count >= minimum && (maximum == 65535 || count <= maximum)) {
							break;
						}
					}
					if (position >= Text.size()) {
						break;
					}
				}
				if (count < minimum || (maximum != 65535 && count > maximum)) {
					return std::nullopt;
				}

				return position;
			}

			std::optional<std::size_t> Group(const RegexpNode& node, std::size_t position, std::int32_t following, std::size_t depth) {
				std::optional<std::size_t> capture;
				if (node.Type == kGroup && node.Right == static_cast<std::int32_t>(CaptureCursor)) {
					capture = CaptureCursor++;
					Program.Captures.at(*capture).Begin = position;
				}
				const auto result = Chain(node.Left, position, following, true, depth);
				if (capture) {
					auto& entry = Program.Captures[*capture];
					if (result) {
						entry.Length = *result - position;
					} else {
						entry = {};
					}
				}

				return result;
			}

			std::optional<std::size_t> Node(std::int32_t index, std::size_t position, std::int32_t following, std::size_t depth) {
				Step(depth);
				if (position > Text.size()) {
					return std::nullopt;
				}

				const auto& node = Program.Nodes.at(static_cast<std::size_t>(index));
				switch (node.Type) {
				case kRepeat:
					return Repeat(node, position, following, depth);
				case kAlternative:
					if (const auto left = Chain(node.Left, position, -1, false, depth)) {
						return left;
					}

					return Chain(node.Right, position, -1, false, depth);
				case kGroup:
				case kNoncapturingGroup:
					return Group(node, position, following, depth);
				case kAny:
					return position + 1;
				case kPositiveClass:
				case kNegativeClass:
					return ClassMatches(node.Left, position, depth) == (node.Type == kPositiveClass) ? std::optional(position + 1) : std::nullopt;
				case kClassEscape:
					return MatchesEscape(node.Left, Byte(position)) ? std::optional(position + 1) : std::nullopt;
				case kBeginAnchor:
					return position == Start ? std::optional(position) : std::nullopt;
				case kEndAnchor:
					return position == Text.size() ? std::optional(position) : std::nullopt;
				case kBoundary:
					return Boundary(node.Left, position);
				default:
					return node.Type == SignedByte(Byte(position)) ? std::optional(position + 1) : std::nullopt;
				}
			}

			std::optional<std::size_t> Boundary(std::int32_t kind, std::size_t position) const {
				bool boundary = false;
				if (position == Start && !IsSpace(Byte(position))) {
					boundary = true;
				} else if (position == Text.size() && position > 0 && !IsSpace(Byte(position - 1))) {
					boundary = true;
				} else if (position < Text.size()) {
					boundary = IsSpace(Byte(position)) != IsSpace(Byte(position + 1));
				}

				return boundary == (kind == 'b') ? std::optional(position) : std::nullopt;
			}
		};

		std::string_view PrepareText(std::string_view text) const {
			if (text.size() > Limits.MaxTextBytes) {
				throw std::runtime_error("regexp input byte limit exceeded");
			}

			return CStringPrefix(text);
		}
	};

	NativeRegexp::NativeRegexp(std::string_view pattern, NativeRegexpLimits limits) : m_Implementation(std::make_unique<Implementation>(pattern, limits)) {
	}

	NativeRegexp::~NativeRegexp() = default;
	NativeRegexp::NativeRegexp(NativeRegexp&&) noexcept = default;
	NativeRegexp& NativeRegexp::operator=(NativeRegexp&&) noexcept = default;

	bool NativeRegexp::Match(std::string_view text) {
		auto& program = *m_Implementation;
		Implementation::MatchState state{program, program.PrepareText(text)};
		const auto result = state.Node(0, 0, -1, 0);

		return result && *result == state.Text.size();
	}

	std::optional<NativeRegexpRange> NativeRegexp::Search(std::string_view text, std::size_t start) {
		auto& program = *m_Implementation;
		Implementation::MatchState state{program, program.PrepareText(text)};
		if (start > state.Text.size()) {
			throw std::runtime_error("regexp search start exceeds the input terminator");
		}
		state.Start = start;
		for (auto position = start; position < state.Text.size(); ++position) {
			state.CaptureCursor = 0;
			if (const auto result = state.Node(0, position, -1, 0)) {
				return NativeRegexpRange{position, *result};
			}
		}

		return std::nullopt;
	}

	std::size_t NativeRegexp::SubexpressionCount() const {
		return m_Implementation->Captures.size();
	}

	std::span<const NativeRegexpCapture> NativeRegexp::Captures() const {
		return m_Implementation->Captures;
	}
} // namespace sgre::script
