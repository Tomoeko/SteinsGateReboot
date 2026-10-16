#include "sgre/script/original_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sgre::script {

	static constexpr std::size_t kMaximumCheckpointBytes = 16 * 1024 * 1024;
	static constexpr std::size_t kMaximumCheckpointNodes = 262144;
	static constexpr std::size_t kMaximumCheckpointString = 1024 * 1024;
	static constexpr std::size_t kMaximumCheckpointDepth = 64;

	static void RequireLocation(const OriginalCheckpointLocation& location) {
		if (location.Storage.empty() || location.Storage.size() > 4096 || location.Storage.find('\0') != std::string::npos || location.Label.empty() || location.Label.front() != '*' || location.Label.size() > 4096 || location.Label.find('\0') != std::string::npos || location.SourceLine < 0 || location.Point < 0 || location.Record < 0) {
			throw std::runtime_error("original checkpoint requires exact bounded snapshot coordinates");
		}
	}

	static void RequireSaveData(SquirrelVm& vm, const SquirrelValue& data, const OriginalCheckpointLocation& location) {
		if (vm.Type(data) != SquirrelValueType::Table) {
			throw std::runtime_error("original checkpoint requires the whole getSaveData table");
		}
		const auto storage = vm.FindRawSlot(data, SquirrelString("storage"));
		const auto target = vm.FindRawSlot(data, SquirrelString("target"));
		const auto point = vm.FindRawSlot(data, SquirrelString("point"));
		const auto flags = vm.FindRawSlot(data, SquirrelString("flags"));
		if (!storage || !TryStringText(*storage) || StringText(*storage) != location.Storage || !target || !TryStringText(*target) || StringText(*target) != location.Label || !point || !std::holds_alternative<std::int32_t>(*point) || std::get<std::int32_t>(*point) != location.Point || !flags || vm.Type(*flags) != SquirrelValueType::Table) {
			throw std::runtime_error("original checkpoint save coordinates or complete flags do not match");
		}
	}

	class CheckpointWriter {
	public:
		explicit CheckpointWriter(SquirrelVm& vm) : Vm(vm) {
			const auto table = Vm.CreateTable();
			const auto pin = Vm.Retain(table);
			Keys = Vm.Retain(Vm.ReadSlot(table, std::string("keys")));
			if (Vm.Type(Keys.Value()) != SquirrelValueType::NativeClosure) {
				throw std::runtime_error("original checkpoint requires the native table keys delegate");
			}
		}

		void Byte(std::uint8_t value) {
			if (Bytes.size() == kMaximumCheckpointBytes) {
				throw std::runtime_error("original checkpoint exceeds its byte limit");
			}
			Bytes.push_back(value);
		}

		void Word(std::uint32_t value) {
			for (std::uint32_t index = 0; index < 4; ++index) {
				Byte(static_cast<std::uint8_t>((value >> (index * 8)) & 255));
			}
		}

		void String(std::string_view text) {
			if (text.size() > kMaximumCheckpointString || text.size() > kMaximumCheckpointBytes - Bytes.size()) {
				throw std::runtime_error("original checkpoint string exceeds its byte limit");
			}
			Word(static_cast<std::uint32_t>(text.size()));
			for (const auto value : text) {
				Byte(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
			}
		}

		void Value(const SquirrelValue& value, std::size_t depth = 0) {
			if (depth > kMaximumCheckpointDepth || Nodes == kMaximumCheckpointNodes) {
				throw std::runtime_error("original checkpoint exceeds its depth/node limit");
			}
			++Nodes;
			switch (Vm.Type(value)) {
			case SquirrelValueType::Null:
				Byte(0);
				break;
			case SquirrelValueType::Integer:
				Byte(1);
				Word(std::bit_cast<std::uint32_t>(std::get<std::int32_t>(value)));
				break;
			case SquirrelValueType::Float:
				Byte(2);
				Word(std::bit_cast<std::uint32_t>(std::get<float>(value)));
				break;
			case SquirrelValueType::Boolean:
				Byte(3);
				Word(std::get<bool>(value) ? 1u : 0u);
				break;
			case SquirrelValueType::String:
				Byte(4);
				String(StringText(value));
				break;
			case SquirrelValueType::Table:
			case SquirrelValueType::Array:
				Container(value, depth);
				break;
			default:
				throw std::runtime_error("original checkpoint cannot encode a VM/native object");
			}
		}

		void Container(const SquirrelValue& value, std::size_t depth) {
			const auto count = Vm.Length(value);
			if (count > kMaximumCheckpointNodes - Nodes) {
				throw std::runtime_error("original checkpoint container exceeds its node limit");
			}
			const auto identity = Vm.BorrowObjectIdentity(value);
			if (std::find(Ancestors.begin(), Ancestors.end(), identity) != Ancestors.end()) {
				throw std::runtime_error("original checkpoint primitive tree contains a cycle");
			}
			Ancestors.push_back(identity);
			const auto table = Vm.Type(value) == SquirrelValueType::Table;
			Byte(table ? 5 : 6);
			Word(static_cast<std::uint32_t>(count));
			if (table) {
				// Use our captured native delegate, never a data key or metamethod.
				const auto keys = Vm.InvokeInContext(Vm.CurrentContext(), Keys.Value(), std::array<SquirrelValue, 1>{value}, 1024);
				if (keys.State != SquirrelRunState::Completed || Vm.Type(keys.Value) != SquirrelValueType::Array || Vm.Length(keys.Value) != count) {
					throw std::runtime_error("original checkpoint raw key enumeration did not complete");
				}
				const auto keys_pin = Vm.Retain(keys.Value);
				std::map<std::string, SquirrelValue> ordered;
				for (std::size_t index = 0; index < count; ++index) {
					const auto key = Vm.GetSlot(keys.Value, static_cast<std::int32_t>(index));
					const auto* text = TryStringText(key);
					if (!text || text->size() > kMaximumCheckpointString) {
						throw std::runtime_error("original checkpoint table requires bounded string keys");
					}
					const auto item = Vm.FindRawSlot(value, key);
					if (!item || !ordered.emplace(*text, *item).second) {
						throw std::runtime_error("original checkpoint raw key changed during capture");
					}
				}
				for (const auto& [key, item] : ordered) {
					String(key);
					Value(item, depth + 1);
				}
			} else {
				for (std::size_t index = 0; index < count; ++index) {
					Value(Vm.GetSlot(value, static_cast<std::int32_t>(index)), depth + 1);
				}
			}
			Ancestors.pop_back();
		}

		SquirrelVm& Vm;
		SquirrelHostRoot Keys;
		std::vector<std::uint8_t> Bytes;
		std::vector<SquirrelObjectIdentity> Ancestors;
		std::size_t Nodes = 0;
	};

	class CheckpointReader {
	public:
		CheckpointReader(SquirrelVm& vm, std::span<const std::uint8_t> bytes) : Vm(vm), Bytes(bytes) {
			if (bytes.size() > kMaximumCheckpointBytes) {
				Fail("file byte limit");
			}
		}

		[[noreturn]] void Fail(std::string_view reason) const {
			throw std::runtime_error("original checkpoint at byte " + std::to_string(Offset) + ": " + std::string(reason));
		}

		std::uint8_t Byte() {
			if (Offset == Bytes.size()) {
				Fail("truncated byte");
			}

			return Bytes[Offset++];
		}

		std::uint32_t Word() {
			std::uint32_t result = 0;
			for (std::uint32_t index = 0; index < 4; ++index) {
				result |= std::uint32_t(Byte()) << (index * 8);
			}

			return result;
		}

		std::string String() {
			const auto count = Word();
			if (count > kMaximumCheckpointString || count > Bytes.size() - Offset) {
				Fail("string extent");
			}
			std::string result;
			result.reserve(count);
			for (std::uint32_t index = 0; index < count; ++index) {
				result.push_back(std::bit_cast<char>(Byte()));
			}

			return result;
		}

		SquirrelValue Container(bool table, std::size_t depth) {
			const auto count = Word();
			if (count > kMaximumCheckpointNodes - Nodes || count > (Bytes.size() - Offset) / (table ? 5 : 1)) {
				Fail("container extent/work limit");
			}
			const auto result = table ? Vm.CreateTable() : Vm.CreateArray();
			const auto pin = Vm.Retain(result);
			for (std::uint32_t index = 0; index < count; ++index) {
				if (table) {
					const auto key = Vm.CreateString(String());
					if (Vm.FindRawSlot(result, key)) {
						Fail("duplicate table key");
					}
					const auto item = Value(depth + 1);
					Vm.NewSlot(result, key, item);
				} else {
					Vm.AppendArray(result, Value(depth + 1));
				}
			}

			return result;
		}

		SquirrelValue Boolean() {
			const auto word = Word();
			if (word > 1) {
				Fail("boolean word");
			}

			return word != 0;
		}

		SquirrelValue Value(std::size_t depth = 0) {
			if (depth > kMaximumCheckpointDepth || Nodes == kMaximumCheckpointNodes) {
				Fail("node/depth limit");
			}
			++Nodes;
			switch (Byte()) {
			case 0:
				return {};
			case 1:
				return std::bit_cast<std::int32_t>(Word());
			case 2:
				return std::bit_cast<float>(Word());
			case 3:
				return Boolean();
			case 4:
				return Vm.CreateString(String());
			case 5:
				return Container(true, depth);
			case 6:
				return Container(false, depth);
			default:
				Fail("unsupported data tag");
			}
		}

		SquirrelVm& Vm;
		std::span<const std::uint8_t> Bytes;
		std::size_t Offset = 0;
		std::size_t Nodes = 0;
	};

	std::vector<std::uint8_t> EncodeOriginalCheckpoint(SquirrelVm& vm, const SquirrelValue& data, const OriginalCheckpointLocation& location, std::string_view identity, std::string_view parent) {
		RequireLocation(location);
		RequireSaveData(vm, data, location);
		if (identity.empty() || identity.size() > 4096 || parent.empty() || parent.size() > 4096) {
			throw std::runtime_error("original checkpoint requires resource and predecessor provenance");
		}
		const auto pin = vm.Retain(data);
		CheckpointWriter writer(vm);
		for (const auto byte : std::array<std::uint8_t, 8>{'S', 'G', 'C', 'P', 2, 0, 0, 0}) {
			writer.Byte(byte);
		}
		writer.String(identity);
		writer.String(parent);
		writer.String(location.Storage);
		writer.String(location.Label);
		writer.Word(static_cast<std::uint32_t>(location.SourceLine));
		writer.Word(static_cast<std::uint32_t>(location.Point));
		writer.Word(static_cast<std::uint32_t>(location.Record));
		writer.Value(data);

		return std::move(writer.Bytes);
	}

	SquirrelValue RestoreOriginalCheckpoint(SquirrelVm& vm, std::span<const std::uint8_t> bytes, const OriginalCheckpointLocation& expected, std::string_view identity) {
		RequireLocation(expected);
		CheckpointReader reader(vm, bytes);
		if (reader.Byte() != 'S' || reader.Byte() != 'G' || reader.Byte() != 'C' || reader.Byte() != 'P' || reader.Word() != 2 || reader.String() != identity) {
			reader.Fail("magic/version/resource identity");
		}
		const auto parent = reader.String();
		OriginalCheckpointLocation actual;
		actual.Storage = reader.String();
		actual.Label = reader.String();
		actual.SourceLine = std::bit_cast<std::int32_t>(reader.Word());
		actual.Point = std::bit_cast<std::int32_t>(reader.Word());
		actual.Record = std::bit_cast<std::int32_t>(reader.Word());
		if (parent.empty() || parent.size() > 4096 || actual != expected) {
			reader.Fail("predecessor provenance or snapshot coordinates");
		}
		const auto result = reader.Value();
		const auto pin = vm.Retain(result);
		if (reader.Offset != bytes.size()) {
			reader.Fail("trailing data");
		}
		RequireSaveData(vm, result, expected);

		return result;
	}

	std::string OriginalCheckpointKey(const OriginalCheckpointLocation& location) {
		RequireLocation(location);

		return std::to_string(location.Storage.size()) + ":" + location.Storage + std::to_string(location.Label.size()) + ":" + location.Label + ":" + std::to_string(location.SourceLine) + ":" + std::to_string(location.Point) + ":" + std::to_string(location.Record);
	}

	static SquirrelInstruction CheckpointInstruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t operand = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
		return SquirrelInstruction{operand, opcode, destination, argument2, argument3};
	}

	static BytecodeProgram RecorderFactory() {
		// Original checkSaveMode remains outside the authored capture trap. A failed
		// optional cache operation is reported without becoming a gameplay exception.
		BytecodeProgram program;
		auto& factory = program.MainFunction;
		factory.Name = std::string("checkpoint recorder factory");
		factory.SourceName = std::string("sgre/original-checkpoint");
		factory.Parameters = {std::string("this"), std::string("original"), std::string("enabled"), std::string("capture"), std::string("failure")};
		factory.StackSize = 6;
		factory.Instructions = {CheckpointInstruction(48, 5, 0), CheckpointInstruction(19, 1, 5)};
		SquirrelFunction hook;
		hook.Name = std::string("checkpoint.ScenePlayer.checkSaveMode");
		hook.SourceName = factory.SourceName;
		hook.Parameters = {std::string("this")};
		hook.StackSize = 7;
		hook.Literals = {std::string("getSaveData")};
		for (std::int32_t index = 0; index < 4; ++index) {
			SquirrelOuterBinding binding;
			binding.Type = 0;
			binding.Source = index + 1;
			binding.Name = factory.Parameters[static_cast<std::size_t>(index + 1)];
			hook.OuterBindings.push_back(std::move(binding));
		}
		// All original calls, including getSaveData and asynchronous save release,
		// use ordinary CALLs and retain their original coroutine continuation.
		hook.Instructions = {CheckpointInstruction(27, 2, 0), CheckpointInstruction(10, 3, 0), CheckpointInstruction(6, 1, 2, 3, 1), CheckpointInstruction(56, 6, 12), CheckpointInstruction(27, 2, 1), CheckpointInstruction(10, 3, 0), CheckpointInstruction(6, 4, 2, 3, 1), CheckpointInstruction(26, 4, 6), CheckpointInstruction(8, 2, 0, 0, 3), CheckpointInstruction(6, 5, 2, 3, 1), CheckpointInstruction(27, 2, 2), CheckpointInstruction(10, 3, 0), CheckpointInstruction(10, 4, 5), CheckpointInstruction(6, 255, 2, 3, 2), CheckpointInstruction(57, 1), CheckpointInstruction(19, 1, 1), CheckpointInstruction(27, 2, 3), CheckpointInstruction(10, 3, 0), CheckpointInstruction(10, 4, 6), CheckpointInstruction(6, 255, 2, 3, 2), CheckpointInstruction(19, 1, 1)};
		factory.Functions.push_back(std::move(hook));

		return program;
	}

	static std::int32_t PlayerInteger(SquirrelVm& vm, const SquirrelValue& player, std::string_view key) {
		const auto value = vm.GetSlot(player, std::string(key));
		const auto* integer = std::get_if<std::int32_t>(&value);
		if (!integer) {
			throw std::runtime_error("original checkpoint requires player integer " + std::string(key));
		}

		return *integer;
	}

	class OriginalCheckpointRecorder::Implementation {
	public:
		Implementation(SquirrelVm& vm, std::string identity, std::string parent, Sink sink) : Vm(vm), Identity(std::move(identity)), Parent(std::move(parent)), Output(std::move(sink)) {
			if (Vm.IsClosed() || Identity.empty() || Parent.empty() || !Output) {
				throw std::runtime_error("original checkpoint recorder requires live VM, provenance and owned output");
			}
		}

		void Capture(const SquirrelValue& player, const SquirrelValue& data) {
			const auto name = StringText(Vm.GetSlot(player, std::string("curSceneName")));
			const auto separator = name.find('*');
			const auto cursor = PlayerInteger(Vm, player, "cur");
			if (separator == std::string::npos || cursor <= 0) {
				throw std::runtime_error("original checkpoint requires its containing scene dispatcher snapshot");
			}
			OriginalCheckpointLocation location;
			location.Storage = name.substr(0, separator);
			location.Label = name.substr(separator);
			location.SourceLine = PlayerInteger(Vm, player, "curLine");
			location.Point = PlayerInteger(Vm, player, "curPoint");
			// sceneMain fetches data[cur++] before checkSaveMode; restore uses raw cur.
			location.Record = cursor - 1;
			auto bytes = EncodeOriginalCheckpoint(Vm, data, location, Identity, Parent);
			Parent = Output(location, std::move(bytes));
			++Count;
		}

		SquirrelVm& Vm;
		std::string Identity;
		std::string Parent;
		Sink Output;
		std::string Failure;
		bool Installed = false;
		bool Cancelled = false;
		std::size_t Count = 0;
	};

	OriginalCheckpointRecorder::OriginalCheckpointRecorder(SquirrelVm& vm, std::string identity, std::string parent, Sink sink) : m_Implementation(std::make_shared<Implementation>(vm, std::move(identity), std::move(parent), std::move(sink))) {
	}

	OriginalCheckpointRecorder::~OriginalCheckpointRecorder() {
		Cancel();
	}

	void OriginalCheckpointRecorder::InstallAvailableHook() {
		auto& state = *m_Implementation;
		if (state.Cancelled || state.Installed) {
			return;
		}
		const auto root = state.Vm.RootTable();
		if (!state.Vm.HasSlot(root, std::string("ScenePlayer"))) {
			return;
		}
		if (state.Vm.HasSlot(root, std::string("MyPlayer"))) {
			throw std::runtime_error("original checkpoint recorder must precede player derivation");
		}
		const auto player = state.Vm.GetSlot(root, std::string("ScenePlayer"));
		const auto original = state.Vm.GetSlot(player, std::string("checkSaveMode"));
		if (state.Vm.Type(original) != SquirrelValueType::Closure) {
			throw std::runtime_error("original checkpoint recorder requires original script checkSaveMode");
		}
		const std::weak_ptr<Implementation> weak = m_Implementation;
		const auto enabled = state.Vm.CreateNativeClosure("checkpoint.enabled", [weak](SquirrelVm&, std::span<const SquirrelValue>) {
			const auto owner = weak.lock();

			return SquirrelNativeResult::ReturnValue(owner && !owner->Cancelled);
		});
		const auto capture = state.Vm.CreateNativeClosure("checkpoint.capture", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			if (const auto owner = weak.lock(); owner && !owner->Cancelled) {
				if (arguments.size() != 2) {
					throw std::runtime_error("original checkpoint capture requires player and whole save data");
				}
				owner->Capture(arguments[0], arguments[1]);
			}

			return SquirrelNativeResult::NoValues();
		});
		const auto failure = state.Vm.CreateNativeClosure("checkpoint.failure", [weak](SquirrelVm&, std::span<const SquirrelValue> arguments) {
			if (const auto owner = weak.lock()) {
				owner->Cancelled = true;
				const auto* detail = arguments.size() == 2 ? TryStringText(arguments[1]) : nullptr;
				owner->Failure = "checkpoint recording stopped: " + (detail ? detail->substr(0, 1024) : std::string("non-string capture exception"));
			}

			return SquirrelNativeResult::NoValues();
		});
		const auto context = state.Vm.CreateContext();
		try {
			const auto factory = state.Vm.CreateClosureInContext(context, RecorderFactory());
			const auto result = state.Vm.InvokeInContext(context, factory, std::array<SquirrelValue, 5>{root, original, enabled, capture, failure}, 256, false);
			if (result.State != SquirrelRunState::Completed || state.Vm.Type(result.Value) != SquirrelValueType::Closure) {
				throw std::runtime_error("original checkpoint recorder factory did not complete");
			}
			const auto pin = state.Vm.Retain(result.Value);
			state.Vm.NewSlot(player, std::string("checkSaveMode"), result.Value);
			state.Installed = true;
			state.Vm.ReleaseContext(context);
		} catch (...) {
			try {
				state.Vm.ReleaseContext(context);
			} catch (...) {
				// Retain the original installation failure.
			}
			throw;
		}
	}

	void OriginalCheckpointRecorder::Cancel() noexcept {
		m_Implementation->Cancelled = true;
		m_Implementation->Output = {};
	}

	void OriginalCheckpointRecorder::SetEntryPredecessor(std::string digest) {
		if (m_Implementation->Count != 0 || digest.empty() || digest.size() > 4096) {
			throw std::runtime_error("checkpoint predecessor must be installed before its first actual capture");
		}
		m_Implementation->Parent = std::move(digest);
	}

	std::string_view OriginalCheckpointRecorder::Error() const noexcept {
		return m_Implementation->Failure;
	}

	std::size_t OriginalCheckpointRecorder::Captures() const noexcept {
		return m_Implementation->Count;
	}

} // namespace sgre::script
