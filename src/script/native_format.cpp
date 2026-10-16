#include "sgre/script/native_format.hpp"

#include "sgre/script/native_float.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::script {
	struct NativeFormatField {
		bool Left = false;
		bool Plus = false;
		bool Space = false;
		bool Alternate = false;
		bool Zero = false;
		std::size_t Width = 0;
		std::optional<std::size_t> Precision;
		char Conversion = 0;
	};

	static void AppendBounded(std::string& output, std::string_view text, std::size_t limit) {
		if (output.size() > limit || text.size() > limit - output.size()) {
			throw std::runtime_error("native formatted output exceeds limit");
		}

		output.append(text);
	}

	static std::size_t ReadFormatNumber(std::string_view format, std::size_t& position, std::string_view name) {
		std::size_t value = 0;
		std::size_t digits = 0;
		while (position < format.size() && format[position] >= '0' && format[position] <= '9') {
			if (++digits >= 3) {
				throw std::runtime_error(std::string(name) + " format too long");
			}

			value = value * 10 + static_cast<std::size_t>(format[position] - '0');
			++position;
		}

		return value;
	}

	static NativeFormatField ReadField(std::string_view format, std::size_t& position) {
		const auto start = position;
		NativeFormatField field;
		while (position < format.size()) {
			const auto flag = format[position];
			if (flag == '-') {
				field.Left = true;
			} else if (flag == '+') {
				field.Plus = true;
			} else if (flag == ' ') {
				field.Space = true;
			} else if (flag == '#') {
				field.Alternate = true;
			} else if (flag == '0') {
				field.Zero = true;
			} else {
				break;
			}

			++position;
		}

		field.Width = ReadFormatNumber(format, position, "width");
		if (position < format.size() && format[position] == '.') {
			++position;
			field.Precision = ReadFormatNumber(format, position, "precision");
		}

		if (position == format.size() || position - start > 20) {
			throw std::runtime_error("invalid format");
		}

		field.Conversion = format[position++];
		if (std::string_view("EGXcdefgiosux").find(field.Conversion) == std::string_view::npos) {
			throw std::runtime_error("invalid format");
		}

		return field;
	}

	static std::int32_t FormatInteger(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		if (const auto* boolean = std::get_if<bool>(&value)) {
			return static_cast<std::int32_t>(*boolean);
		}

		if (const auto* number = std::get_if<float>(&value)) {
			const auto expanded = static_cast<double>(*number);
			if (!std::isfinite(expanded) || expanded < -2147483648.0 || expanded >= 2147483648.0) {
				return std::numeric_limits<std::int32_t>::min();
			}

			return static_cast<std::int32_t>(*number);
		}

		throw std::runtime_error("native numeric format requires an integer, float, or boolean");
	}

	static std::string PadField(std::string text, const NativeFormatField& field, std::size_t numeric_prefix = 0, bool integer = false) {
		if (text.size() >= field.Width) {
			return text;
		}

		const auto padding = field.Width - text.size();
		if (field.Left) {
			text.append(padding, ' ');
		} else if (integer && field.Zero && !field.Precision) {
			text.insert(numeric_prefix, padding, '0');
		} else {
			text.insert(0, padding, ' ');
		}

		return text;
	}

	static std::string IntegerField(const SquirrelValue& value, const NativeFormatField& field) {
		const auto number = FormatInteger(value);
		if (field.Conversion == 'c') {
			const auto byte = static_cast<std::uint8_t>(std::bit_cast<std::uint32_t>(number));

			return PadField(std::string(1, static_cast<char>(byte)), field);
		}

		const bool signed_number = field.Conversion == 'd' || field.Conversion == 'i';
		const auto bits = std::bit_cast<std::uint32_t>(number);
		const auto magnitude = signed_number && number < 0 ? 0 - bits : bits;
		const auto base = field.Conversion == 'o' ? 8 : ((field.Conversion == 'x' || field.Conversion == 'X') ? 16 : 10);
		std::array<char, 32> buffer{};
		const auto conversion = std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude, base);
		if (conversion.ec != std::errc{}) {
			throw std::runtime_error("native integer format conversion failed");
		}

		std::string digits(buffer.data(), conversion.ptr);
		if (field.Precision && *field.Precision == 0 && magnitude == 0) {
			digits.clear();
		}

		if (field.Precision && digits.size() < *field.Precision) {
			digits.insert(0, *field.Precision - digits.size(), '0');
		}

		if (field.Conversion == 'X') {
			for (auto& character : digits) {
				if (character >= 'a' && character <= 'f') {
					character = static_cast<char>(character - 'a' + 'A');
				}
			}
		}

		std::string prefix;
		if (signed_number) {
			if (number < 0) {
				prefix = "-";
			} else if (field.Plus) {
				prefix = "+";
			} else if (field.Space) {
				prefix = " ";
			}
		} else if (field.Alternate && field.Conversion == 'o' && (digits.empty() || digits.front() != '0')) {
			prefix = "0";
		} else if (field.Alternate && magnitude != 0 && (field.Conversion == 'x' || field.Conversion == 'X')) {
			prefix = field.Conversion == 'X' ? "0X" : "0x";
		}

		return PadField(prefix + digits, field, prefix.size(), true);
	}

	static std::string FloatingField(const SquirrelValue& value, const NativeFormatField& field) {
		float number = 0;
		if (const auto* scalar = std::get_if<float>(&value)) {
			number = *scalar;
		} else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			number = static_cast<float>(*integer);
		} else if (const auto* boolean = std::get_if<bool>(&value)) {
			number = static_cast<float>(*boolean);
		} else {
			throw std::runtime_error("native numeric format requires an integer, float, or boolean");
		}

		auto text = FormatNativeFloat(number, field.Conversion, field.Precision, field.Alternate);
		if (text.empty() || text.front() != '-') {
			if (field.Plus) {
				text.insert(0, 1, '+');
			} else if (field.Space) {
				text.insert(0, 1, ' ');
			}
		}

		auto padding = field;
		padding.Precision.reset();
		if (!std::isfinite(number)) {
			padding.Zero = false;
		}

		const auto prefix = !text.empty() && (text.front() == '-' || text.front() == '+' || text.front() == ' ') ? 1 : 0;

		return PadField(std::move(text), padding, static_cast<std::size_t>(prefix), true);
	}

	struct NativeFormatPart {
		std::string_view Literal;
		std::optional<NativeFormatField> Field;
		std::size_t Argument = 0;
		std::optional<std::string> String;
	};

	std::string FormatNativeString(SquirrelVm& vm, std::string_view format, std::span<const SquirrelValue> values, std::size_t max_output_bytes) {
		format = format.substr(0, format.find('\0'));
		if (format.size() > max_output_bytes) {
			throw std::runtime_error("native format string exceeds limit");
		}

		std::vector<NativeFormatPart> parts;
		std::size_t argument = 0;
		std::size_t position = 0;
		while (position < format.size()) {
			const auto marker = format.find('%', position);
			NativeFormatPart literal;
			literal.Literal = marker == std::string_view::npos ? format.substr(position) : format.substr(position, marker - position);
			parts.push_back(literal);
			if (marker == std::string_view::npos) {
				break;
			}

			position = marker + 1;
			if (position < format.size() && format[position] == '%') {
				NativeFormatPart percent;
				percent.Literal = "%";
				parts.push_back(percent);
				++position;
				continue;
			}

			if (argument == values.size()) {
				throw std::runtime_error("not enough paramters for the given format string");
			}

			NativeFormatPart part;
			part.Field = ReadField(format, position);
			part.Argument = argument++;
			if (part.Field->Conversion == 's') {
				part.String = vm.ToString(values[part.Argument]);
			}

			parts.push_back(std::move(part));
		}

		// Native 0x140265f30 converts all %s operands before validating numeric conversions.
		std::string output;
		for (const auto& part : parts) {
			if (!part.Field) {
				AppendBounded(output, part.Literal, max_output_bytes);
				continue;
			}

			const auto& field = *part.Field;
			std::string formatted;
			if (part.String) {
				const std::string_view text(*part.String);
				formatted = text.substr(0, text.find('\0'));
				if (field.Precision && formatted.size() > *field.Precision) {
					formatted.resize(*field.Precision);
				}

				formatted = PadField(std::move(formatted), field);
			} else if (std::string_view("eEfFgG").find(field.Conversion) != std::string_view::npos) {
				formatted = FloatingField(values[part.Argument], field);
			} else {
				formatted = IntegerField(values[part.Argument], field);
			}

			AppendBounded(output, formatted, max_output_bytes);
		}

		return output;
	}

	void InstallNativePrinting(SquirrelVm& vm, NativePrintCallback output, std::size_t max_output_bytes) {
		if (!output || max_output_bytes == 0) {
			throw std::runtime_error("native printing requires a host callback and nonzero output limit");
		}

		vm.RegisterNative("format", [max_output_bytes](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			if (arguments.size() < 2 || !std::holds_alternative<SquirrelString>(arguments[1])) {
				throw std::runtime_error("native format requires a format string");
			}

			SquirrelNativeResult result;
			result.Value = FormatNativeString(current, StringText(arguments[1]), arguments.subspan(2), max_output_bytes);

			return result;
		});

		for (const bool error : {false, true}) {
			vm.RegisterNative(error ? "errprintf" : "printf", [output, error, max_output_bytes](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
				if (arguments.size() < 2 || !std::holds_alternative<SquirrelString>(arguments[1])) {
					throw std::runtime_error("native printf requires a format string");
				}

				const auto formatted = FormatNativeString(current, StringText(arguments[1]), arguments.subspan(2), max_output_bytes);
				output(std::string_view(formatted).substr(0, formatted.find('\0')), error);

				return SquirrelNativeResult{};
			});
		}

		for (const bool error : {false, true}) {
			vm.RegisterNative(error ? "errprint" : "print", [output, error, max_output_bytes](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
				if (arguments.size() != 2) {
					throw std::runtime_error("native print requires one value");
				}

				const auto text = current.ToString(arguments[1]);
				if (text.size() > max_output_bytes) {
					throw std::runtime_error("native printed output exceeds limit");
				}

				output(std::string_view(text).substr(0, text.find('\0')), error);

				return SquirrelNativeResult{};
			});
		}

		for (const auto* name : {"format", "printf", "errprintf", "print", "errprint"}) {
			SquirrelNativeSignature signature;
			signature.Name = name;
			const bool formatted = std::string_view(name) != "print" && std::string_view(name) != "errprint";
			signature.ParameterCheck = formatted ? -2 : 2;
			if (formatted) {
				signature.TypeCheck = {-1, 16};
			}

			vm.SetNativeSignature(vm.GetSlot(vm.RootTable(), std::string(name)), std::move(signature));
		}
	}
} // namespace sgre::script
