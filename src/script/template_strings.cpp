#include "sgre/script/template_strings.hpp"

#include "sgre/script/source_expression.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace sgre::script {
	static constexpr std::size_t kMaximumTemplateResultBytes = 16 * 1024 * 1024;
	static constexpr std::size_t kMaximumTemplateExpressionBytes = 200;

	static bool TemplateIdentifierStart(unsigned char value) {
		return value == '_' || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
	}

	static bool TemplateIdentifierContinue(unsigned char value) {
		return TemplateIdentifierStart(value) || (value >= '0' && value <= '9');
	}

	static void AppendTemplateBytes(std::string& result, std::string_view bytes) {
		if (bytes.size() > kMaximumTemplateResultBytes - result.size()) {
			throw std::runtime_error("Squirrel template result exceeds bounded string storage");
		}
		// Native225350 copies the explicit converted string length through strncpy, padding after NUL.
		const auto prefix = bytes.substr(0, bytes.find('\0'));
		result.append(prefix);
		result.append(bytes.size() - prefix.size(), '\0');
	}

	static SquirrelValue EvaluateTemplateName(SquirrelVm& vm, std::string_view expression, const SquirrelValue& actual_receiver, const SquirrelValue& environment, const SquirrelValue& fallback) {
		if (expression.size() > kMaximumTemplateExpressionBytes) {
			return fallback;
		}
		auto program = TryCompileSquirrelNameExpression(vm, expression);
		if (!program) {
			throw std::runtime_error("Squirrel template expression syntax requires the unrecovered first-party source compiler domain");
		}

		auto closure = vm.CreateClosure(std::move(*program));
		if (!std::holds_alternative<std::monostate>(environment)) {
			closure = vm.BindEnvironment(closure, environment);
		}
		const auto closure_owner = vm.Retain(closure);
		const std::array<SquirrelValue, 1> receiver{actual_receiver};
		try {
			const auto result = vm.InvokeInContext(vm.CurrentContext(), closure, receiver);
			if (result.State != SquirrelRunState::Completed) {
				throw std::runtime_error("Squirrel template expression requires completed native evaluation");
			}

			return result.Value;
		} catch (const SquirrelVmException&) {
			// Native2227f0/225350 substitute the actual fallback after a real compile/call failure.
			return fallback;
		}
	}

	SquirrelNativeResult ExpandSquirrelTemplate(SquirrelVm& vm, std::span<const SquirrelValue> arguments) {
		if (arguments.size() < 2 || !std::holds_alternative<SquirrelString>(arguments[1])) {
			throw std::runtime_error("Squirrel template requires its actual source string");
		}

		// Retain input values before nested VM calls can grow the actual operand storage.
		const auto actual_receiver = arguments[0];
		const auto source_value = std::get<SquirrelString>(arguments[1]);
		const auto environment = arguments.size() > 2 ? arguments[2] : SquirrelValue{};
		const auto fallback = arguments.size() > 3 && !std::holds_alternative<std::monostate>(arguments[3]) ? arguments[3] : SquirrelValue(SquirrelString(""));
		const auto& source = source_value.Text();
		const auto text = std::string_view(source).substr(0, source.find('\0'));
		if (text.size() > kMaximumTemplateResultBytes) {
			throw std::runtime_error("Squirrel template source exceeds bounded string storage");
		}
		std::string result;
		result.reserve(text.size());
		for (std::size_t position = 0; position < text.size();) {
			if (text[position] != '$' || position + 1 == text.size()) {
				AppendTemplateBytes(result, text.substr(position++, 1));
				continue;
			}

			const auto next = static_cast<unsigned char>(text[position + 1]);
			if (next == '$') {
				AppendTemplateBytes(result, "$");
				position += 2;
				continue;
			}
			if (next != '{' && !TemplateIdentifierStart(next)) {
				AppendTemplateBytes(result, "$");
				++position;
				continue;
			}

			const auto begin = position + (next == '{' ? 2 : 1);
			std::size_t end = begin;
			if (next == '{') {
				end = text.find('}', begin);
				if (end == std::string_view::npos) {
					end = text.size();
				}
				position = end < text.size() ? end + 1 : end;
			} else {
				while (end < text.size() && TemplateIdentifierContinue(static_cast<unsigned char>(text[end]))) {
					++end;
				}
				position = end;
			}
			const auto expression = text.substr(begin, end - begin);
			if (expression.empty()) {
				continue;
			}
			const auto value = EvaluateTemplateName(vm, expression, actual_receiver, environment, fallback);
			AppendTemplateBytes(result, vm.ToString(value));
		}

		return SquirrelNativeResult::ReturnValue(SquirrelString(std::move(result)));
	}
} // namespace sgre::script
