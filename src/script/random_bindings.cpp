#include "sgre/script/random_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	struct RandomBindingRecord {
		SquirrelObjectIdentity Instance;
		std::unique_ptr<scene::MotionSharedRandom> Generator;
	};

	enum class RandomMethod : std::uint32_t {
		Unit = 1,
		Integer
	};

	static std::array<std::uint8_t, 8> RandomMethodMetadata(RandomMethod method) {
		const auto word = static_cast<std::uint32_t>(method);

		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0};
	}

	static void RequireRandomMethod(SquirrelVm& vm, RandomMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("Random callback requires its captured native method identity");
		}
		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = RandomMethodMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("Random captured native method identity is invalid");
		}
	}

	class ScriptRandomBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, std::span<const std::uint32_t, scene::kMotionSharedSeedWords> actual_seed, std::size_t maximum)
		    : Vm(vm), Objects(objects), Seed(actual_seed), Maximum(maximum) {
			if (maximum == 0 || maximum > 65536) {
				throw std::runtime_error("Random binding requires a bounded positive native instance capacity");
			}
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		std::span<const std::uint32_t, scene::kMotionSharedSeedWords> Seed;
		std::size_t Maximum;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		std::map<std::uint32_t, RandomBindingRecord> Instances;

		RandomBindingRecord& Record(const SquirrelValue& instance) {
			if (Vm.Type(instance) != SquirrelValueType::Instance) {
				throw std::runtime_error("Random callback requires its genuine native instance");
			}
			const auto found = Instances.find(std::get<SquirrelObjectHandle>(instance).Index);
			if (found == Instances.end() || found->second.Instance != Vm.BorrowObjectIdentity(instance)) {
				throw std::runtime_error("Random receiver has no live native identity");
			}

			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.empty() || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("Random constructor requires its genuine instance receiver");
			}
			const auto instance = arguments[0];
			const auto index = std::get<SquirrelObjectHandle>(instance).Index;
			if (Instances.contains(index) || Instances.size() >= Maximum) {
				throw std::runtime_error("Random native instance capacity is exhausted or receiver is already initialized");
			}

			RandomBindingRecord record;
			record.Instance = Vm.BorrowObjectIdentity(instance);
			if (arguments.size() == 1) {
				record.Generator = std::make_unique<scene::MotionSharedRandom>(Seed);
			} else {
				const auto seed = std::bit_cast<std::uint32_t>(TryNativeInteger(arguments[1]).value_or(0));
				record.Generator = std::make_unique<scene::MotionSharedRandom>(seed);
			}
			// Native2286B0 attaches the Object base with an empty delegate; arguments are not delegates.
			Objects.AttachInstance(instance);
			bool inserted = false;
			try {
				Instances.emplace(index, std::move(record));
				inserted = true;
				Objects.ConfigureNativeRelease(instance, [weak](const SquirrelValue& receiver) {
					if (const auto owner = weak.lock()) {
						owner->Release(receiver);
					}
				});
			} catch (...) {
				if (inserted) {
					Instances.erase(index);
				}
				Objects.ReleaseInstance(instance);

				throw;
			}
		}

		void Release(const SquirrelValue& instance) {
			static_cast<void>(Record(instance));
			Objects.RequireCanRelease(instance);
			// The native generator retires before the Object base notifies completion subscribers.
			Instances.erase(std::get<SquirrelObjectHandle>(instance).Index);
			Objects.ReleaseInstance(instance);
		}
	};

	ScriptRandomBindings::ScriptRandomBindings(SquirrelVm& vm, ScriptObjectBindings& objects, std::span<const std::uint32_t, scene::kMotionSharedSeedWords> actual_seed, std::size_t max_instances)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, actual_seed, max_instances)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(objects.ObjectClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("Random.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Random constructor requires its live binding services");
			}
			owner->Construct(arguments, weak);

			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("Random.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("Random weakref requires its live services and original final argument");
			}

			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](std::string name, RandomMethod identity) {
			const auto metadata = RandomMethodMetadata(identity);
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(metadata)};
			const auto closure = vm.CreateNativeClosureWithCaptures(
			    "Random." + name, [weak, identity](SquirrelVm&, auto arguments) {
				    const auto owner = weak.lock();
				    if (!owner || arguments.empty()) {
					    throw std::runtime_error("Random method requires its live services and native receiver");
				    }
				    RequireRandomMethod(owner->Vm, identity);
				    auto& generator = *owner->Record(arguments[0]).Generator;
				    if (identity == RandomMethod::Unit) {
					    return SquirrelNativeResult::ReturnValue(generator.NextUnitFloat());
				    }

				    const auto maximum = arguments.size() > 1 ? TryNativeInteger(arguments[1]).value_or(0) : 0;
				    const auto denominator = std::bit_cast<std::uint32_t>(maximum) + 1u;
				    if (denominator == 0) {
					    throw std::runtime_error("Random.intrandom(-1) reaches the native zero-divisor fault and is outside the recovered returning domain");
				    }
				    const auto result = (generator.NextWord() & 0x7fffffffu) % denominator;

				    return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(result));
			    },
			    captures);
			vm.NewSlot(state.Class, name, closure);
		};
		method("random", RandomMethod::Unit);
		method("intrandom", RandomMethod::Integer);
		vm.NewSlot(vm.RootTable(), std::string("Random"), state.Class);
	}

	ScriptRandomBindings::~ScriptRandomBindings() = default;

	SquirrelValue ScriptRandomBindings::RandomClass() const {
		return m_Implementation->Class;
	}

} // namespace sgre::script
