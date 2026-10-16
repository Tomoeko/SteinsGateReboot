#include "sgre/script/regexp_bindings.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {
	struct RegexpInstanceRecord {
		SquirrelObjectIdentity Instance;
		NativeRegexp Program;
	};

	class RegexpRegistry {
	public:
		RegexpRegistry(NativeRegexpLimits limits, std::size_t maximum_instances) : Limits(limits), MaximumInstances(maximum_instances) {
			if (maximum_instances == 0 || maximum_instances > 65536) {
				throw std::runtime_error("regexp requires a bounded positive instance limit");
			}
		}

		NativeRegexpLimits Limits;
		std::size_t MaximumInstances;
		SquirrelObjectIdentity Class;
		std::map<std::uint32_t, RegexpInstanceRecord> Instances;

		NativeRegexp& Program(SquirrelVm& vm, const SquirrelValue& instance) {
			if (vm.Type(instance) == SquirrelValueType::Instance) {
				const auto identity = vm.BorrowObjectIdentity(instance);
				const auto found = Instances.find(identity.Index);
				if (found != Instances.end() && found->second.Instance == identity && vm.InstanceHostIdentity(instance, this).Value == identity.Generation) {
					return found->second.Program;
				}
			}

			throw SquirrelVmException(std::string("invalid regexp instance"), "invalid regexp instance");
		}

		void Construct(SquirrelVm& vm, const SquirrelValue& instance, std::string_view pattern, const std::shared_ptr<RegexpRegistry>& owner) {
			const auto class_value = vm.ResolveObjectIdentity(Class);
			if (!class_value || !vm.IsInstanceOf(instance, *class_value)) {
				throw SquirrelVmException(std::string("invalid regexp instance"), "invalid regexp instance");
			}

			const auto identity = vm.BorrowObjectIdentity(instance);
			if (Instances.contains(identity.Index) || Instances.size() >= MaximumInstances) {
				throw std::runtime_error("regexp receiver already initialized or instance limit exceeded");
			}

			NativeRegexp program(pattern, Limits);
			Instances.emplace(identity.Index, RegexpInstanceRecord{identity, std::move(program)});
			try {
				vm.SetInstanceHostIdentity(instance, {this, identity.Generation});
				// The registry borrows instance identities. Only this real VM hook owns the compiled payload's retirement.
				vm.SetInstanceReleaseHook(instance, [owner, identity](SquirrelVm&, const SquirrelValue&) {
					const auto found = owner->Instances.find(identity.Index);
					if (found != owner->Instances.end() && found->second.Instance == identity) {
						owner->Instances.erase(found);
					}
				});
			} catch (...) {
				Instances.erase(identity.Index);

				throw;
			}
		}
	};

	static SquirrelValue RegexpRangeTable(SquirrelVm& vm, NativeRegexpRange range) {
		const auto table = vm.CreateTable();
		vm.NewSlot(table, std::string("begin"), static_cast<std::int32_t>(range.Begin));
		vm.NewSlot(table, std::string("end"), static_cast<std::int32_t>(range.End));

		return table;
	}

	static SquirrelNativeSignature RegexpSignature(std::string_view name, std::int32_t count, std::initializer_list<std::int32_t> types) {
		SquirrelNativeSignature result;
		result.Name = std::string(name);
		result.ParameterCheck = count;
		result.TypeCheck = types;

		return result;
	}

	static std::size_t RegexpSearchStart(SquirrelVm& vm) {
		if (vm.NativeStackSize() <= 2) {
			return 0;
		}

		const auto start = TryNativeInteger(vm.NativeStackValue(3)).value_or(0);
		if (start < 0) {
			throw std::runtime_error("regexp search start precedes the input");
		}

		return static_cast<std::size_t>(start);
	}

	SquirrelValue InstallRecoveredRegexpLibrary(SquirrelVm& vm, NativeRegexpLimits limits, std::size_t maximum_instances) {
		const auto root = vm.RootTable();
		if (vm.HasSlot(root, std::string("regexp"))) {
			throw std::runtime_error("regexp root slot is already registered");
		}

		const auto owner = std::make_shared<RegexpRegistry>(limits, maximum_instances);
		const auto class_value = vm.CreateClass();
		owner->Class = vm.BorrowObjectIdentity(class_value);
		const auto constructor = vm.CreateNativeClosure("regexp.constructor", [owner](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			try {
				owner->Construct(current, arguments[0], StringText(arguments[1]), owner);
			} catch (const SquirrelVmException&) {
				throw;
			} catch (const std::exception& error) {
				throw SquirrelVmException(std::string(error.what()), error.what());
			}

			return SquirrelNativeResult::NoValues();
		}, RegexpSignature("constructor", 2, {-1, 16}));
		vm.NewSlot(class_value, std::string("constructor"), constructor);
		const auto match = vm.CreateNativeClosure("regexp.match", [owner](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			return SquirrelNativeResult::ReturnValue(owner->Program(current, arguments[0]).Match(StringText(arguments[1])));
		}, RegexpSignature("match", 2, {32768, 16}));
		vm.NewSlot(class_value, std::string("match"), match);
		const auto search = vm.CreateNativeClosure("regexp.search", [owner](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			const auto result = owner->Program(current, arguments[0]).Search(StringText(arguments[1]), RegexpSearchStart(current));
			return result ? SquirrelNativeResult::ReturnValue(RegexpRangeTable(current, *result)) : SquirrelNativeResult::NoValues();
		}, RegexpSignature("search", -2, {32768, 16, 6}));
		vm.NewSlot(class_value, std::string("search"), search);
		const auto capture = vm.CreateNativeClosure("regexp.capture", [owner](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			auto& program = owner->Program(current, arguments[0]);
			if (!program.Search(StringText(arguments[1]), RegexpSearchStart(current))) {
				return SquirrelNativeResult::NoValues();
			}

			const auto array = current.CreateArray();
			for (const auto& value : program.Captures()) {
				NativeRegexpRange range;
				if (value.Length != 0 && value.Begin) {
					range.Begin = *value.Begin;
					range.End = range.Begin + value.Length;
				}
				current.AppendArray(array, RegexpRangeTable(current, range));
			}

			return SquirrelNativeResult::ReturnValue(array);
		}, RegexpSignature("capture", -2, {32768, 16, 6}));
		vm.NewSlot(class_value, std::string("capture"), capture);
		const auto count = vm.CreateNativeClosure("regexp.subexpcount", [owner](SquirrelVm& current, std::span<const SquirrelValue> arguments) {
			return SquirrelNativeResult::ReturnValue(static_cast<std::int32_t>(owner->Program(current, arguments[0]).SubexpressionCount()));
		}, RegexpSignature("subexpcount", 1, {32768}));
		vm.NewSlot(class_value, std::string("subexpcount"), count);
		const auto type = vm.CreateNativeClosure("regexp._typeof", [](SquirrelVm&, std::span<const SquirrelValue>) {
			return SquirrelNativeResult::ReturnValue(std::string("regexp"));
		}, RegexpSignature("_typeof", 1, {32768}));
		vm.NewSlot(class_value, std::string("_typeof"), type);
		vm.NewSlot(root, std::string("regexp"), class_value);

		return class_value;
	}
} // namespace sgre::script
