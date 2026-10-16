#include "sgre/script/native_math.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sgre::script {
	struct MathSignatureRecord {
		std::string_view Name;
		std::int32_t ParameterCheck;
		std::size_t NumericArguments;
	};

	// Registration 0x140269b50 consumes the exact catalog at 0x1409690c0.
	static constexpr MathSignatureRecord kMathSignatures[] = {
	    {"sqrt", 2, 1},
	    {"sin", 2, 1},
	    {"cos", 2, 1},
	    {"asin", 2, 1},
	    {"acos", 2, 1},
	    {"log", 2, 1},
	    {"log10", 2, 1},
	    {"tan", 2, 1},
	    {"atan", 2, 1},
	    {"atan2", 3, 2},
	    {"pow", 3, 2},
	    {"floor", 2, 1},
	    {"ceil", 2, 1},
	    {"exp", 2, 1},
	    {"srand", 2, 1},
	    {"rand", 1, 0},
	    {"fabs", 2, 1},
	    {"iabs", 2, 1},
	    {"abs", 2, 1},
	    {"min", -2, 0},
	    {"max", -2, 0},
	};

	static float MathFloat(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}

		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*integer);
		}

		throw std::runtime_error("native math parameter requires an integer or float");
	}

	static std::int32_t MathInteger(const SquirrelValue& value) {
		if (const auto* integer = std::get_if<std::int32_t>(&value)) {
			return *integer;
		}

		const auto number = MathFloat(value);
		if (!std::isfinite(number) || number < -2147483648.0f || number >= 2147483648.0f) {
			return std::numeric_limits<std::int32_t>::min();
		}

		return static_cast<std::int32_t>(number);
	}

	static std::int32_t IntegerAbs(std::int32_t number) {
		const auto bits = std::bit_cast<std::uint32_t>(number);

		return std::bit_cast<std::int32_t>(number < 0 ? 0 - bits : bits);
	}

	static float FloatAbs(float number) {
		return std::bit_cast<float>(std::bit_cast<std::uint32_t>(number) & 0x7fffffff);
	}

	static void RequireMathCount(std::span<const SquirrelValue> arguments, std::size_t count, std::string_view name) {
		if (arguments.size() != count) {
			throw std::runtime_error("native " + std::string(name) + " received an incorrect argument count");
		}
	}

	class ScriptMathBindings::Implementation {
	public:
		explicit Implementation(SquirrelVm& vm) : Vm(vm) {
		}

		SquirrelVm& Vm;
		// Exact executable construct_ptd_array 0x14059262c initializes the seed to one.
		std::uint32_t RandomSeed = 1;

		std::int32_t Random() {
			RandomSeed = RandomSeed * 0x343fd + 0x269ec3;

			return static_cast<std::int32_t>((RandomSeed >> 16) & 0x7fff);
		}

		void Install(const std::shared_ptr<Implementation>& owner) {
			const auto weak = std::weak_ptr<Implementation>(owner);
			const auto callback = [weak](auto operation) {
				return [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
					const auto retained = weak.lock();
					if (!retained) {
						throw std::runtime_error("native math bindings have expired");
					}

					return operation(*retained, arguments);
				};
			};
			Vm.RegisterNative("srand", callback([](Implementation& state, const auto arguments) {
				RequireMathCount(arguments, 2, "srand");
				state.RandomSeed = std::bit_cast<std::uint32_t>(MathInteger(arguments[1]));

				return SquirrelNativeResult{};
			}));
			Vm.RegisterNative("rand", callback([](Implementation& state, const auto arguments) {
				RequireMathCount(arguments, 1, "rand");
				SquirrelNativeResult result;
				result.Value = state.Random();

				return result;
			}));
			for (const auto name : {std::string_view("abs"), std::string_view("fabs"), std::string_view("iabs")}) {
				Vm.RegisterNative(std::string(name), callback([name](Implementation&, const auto arguments) {
					RequireMathCount(arguments, 2, name);
					SquirrelNativeResult result;
					if (name == "iabs" || (name == "abs" && std::holds_alternative<std::int32_t>(arguments[1]))) {
						result.Value = IntegerAbs(MathInteger(arguments[1]));
					} else {
						result.Value = FloatAbs(MathFloat(arguments[1]));
					}

					return result;
				}));
			}

			for (const bool minimum : {true, false}) {
				Vm.RegisterNative(minimum ? "min" : "max", callback([minimum](Implementation& state, const auto arguments) {
					if (arguments.size() < 2) {
						throw std::runtime_error("native min/max requires at least one value");
					}

					std::size_t selected = 1;
					for (std::size_t index = 2; index < arguments.size(); ++index) {
						const auto order = state.Vm.CompareValues(arguments[index], arguments[selected]);
						if ((minimum && order < 0) || (!minimum && order > 0)) {
							selected = index;
						}
					}

					SquirrelNativeResult result;
					result.Value = arguments[selected];

					return result;
				}));
			}

			const auto unary = [&](std::string_view name, double (*function)(double)) {
				Vm.RegisterNative(std::string(name), callback([name, function](Implementation&, const auto arguments) {
					RequireMathCount(arguments, 2, name);
					const auto input = MathFloat(arguments[1]);
					if (!std::isfinite(input)) {
						throw std::runtime_error("native " + std::string(name) + " nonfinite input parity is not recovered");
					}

					const auto number = static_cast<float>(function(static_cast<double>(input)));
					if (!std::isfinite(number)) {
						throw std::runtime_error("native " + std::string(name) + " domain/overflow result parity is not recovered");
					}

					SquirrelNativeResult result;
					result.Value = number;

					return result;
				}));
			};
			unary("sqrt", static_cast<double (*)(double)>(std::sqrt));
			unary("sin", static_cast<double (*)(double)>(std::sin));
			unary("cos", static_cast<double (*)(double)>(std::cos));
			unary("asin", static_cast<double (*)(double)>(std::asin));
			unary("acos", static_cast<double (*)(double)>(std::acos));
			unary("log", static_cast<double (*)(double)>(std::log));
			unary("log10", static_cast<double (*)(double)>(std::log10));
			unary("tan", static_cast<double (*)(double)>(std::tan));
			unary("atan", static_cast<double (*)(double)>(std::atan));
			unary("floor", static_cast<double (*)(double)>(std::floor));
			unary("ceil", static_cast<double (*)(double)>(std::ceil));
			unary("exp", static_cast<double (*)(double)>(std::exp));

			for (const bool power : {false, true}) {
				Vm.RegisterNative(power ? "pow" : "atan2", callback([power](Implementation&, const auto arguments) {
					RequireMathCount(arguments, 3, power ? "pow" : "atan2");
					const auto left = MathFloat(arguments[1]);
					const auto right = MathFloat(arguments[2]);
					if (!std::isfinite(left) || !std::isfinite(right)) {
						throw std::runtime_error("native binary math nonfinite input parity is not recovered");
					}

					const auto number = static_cast<float>(power ? std::pow(static_cast<double>(left), static_cast<double>(right)) : std::atan2(static_cast<double>(left), static_cast<double>(right)));
					if (!std::isfinite(number)) {
						throw std::runtime_error("native binary math domain/overflow result parity is not recovered");
					}

					SquirrelNativeResult result;
					result.Value = number;

					return result;
				}));
			}

			Vm.NewSlot(Vm.RootTable(), std::string("RAND_MAX"), std::int32_t{0x7fff});
			Vm.NewSlot(Vm.RootTable(), std::string("PI"), std::bit_cast<float>(std::uint32_t{0x40490fdb}));
			for (const auto& record : kMathSignatures) {
				SquirrelNativeSignature signature;
				signature.Name = std::string(record.Name);
				signature.ParameterCheck = record.ParameterCheck;
				if (record.NumericArguments != 0) {
					signature.TypeCheck.push_back(-1);
					signature.TypeCheck.insert(signature.TypeCheck.end(), record.NumericArguments, 6);
				}

				Vm.SetNativeSignature(Vm.GetSlot(Vm.RootTable(), std::string(record.Name)), std::move(signature));
			}
		}
	};

	ScriptMathBindings::ScriptMathBindings(SquirrelVm& vm) : m_Implementation(std::make_shared<Implementation>(vm)) {
		m_Implementation->Install(m_Implementation);
	}

	ScriptMathBindings::~ScriptMathBindings() = default;

	void ScriptMathBindings::Seed(std::uint32_t seed) {
		m_Implementation->RandomSeed = seed;
	}

	std::int32_t ScriptMathBindings::Random() {
		return m_Implementation->Random();
	}
} // namespace sgre::script
