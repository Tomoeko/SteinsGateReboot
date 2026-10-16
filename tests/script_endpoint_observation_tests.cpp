#include "script_buffer_fixture.hpp"

#include "sgre/script/script_scheduler.hpp"

#include <bit>
#include <iostream>
#include <optional>
#include <span>
#include <utility>

using sgre::script::BytecodeProgram;
using sgre::script::ScriptScheduler;
using sgre::script::ScriptThreadHandle;
using sgre::script::ScriptThreadStatus;
using sgre::script::SquirrelValue;
using sgre::script::SquirrelVm;

static sgre::script::SquirrelInstruction Instruction(std::uint8_t opcode, std::uint8_t destination, std::int32_t argument1 = 0, std::uint8_t argument2 = 0, std::uint8_t argument3 = 0) {
	sgre::script::SquirrelInstruction instruction;
	instruction.Opcode = opcode;
	instruction.Argument0 = destination;
	instruction.Argument1 = argument1;
	instruction.Argument2 = argument2;
	instruction.Argument3 = argument3;

	return instruction;
}

static BytecodeProgram CallingProgram(std::string native_name) {
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string("independent-endpoint.nut");
	program.MainFunction.Name = std::string("endpoint_fixture");
	program.MainFunction.Parameters = {std::string("this")};
	program.MainFunction.StackSize = 4;
	program.MainFunction.Literals = {std::move(native_name)};
	program.MainFunction.Instructions = {Instruction(8, 1, 0, 0, 2), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)};

	return program;
}

static sgre::script::SquirrelVmLimits ReclaimingLimits() {
	sgre::script::SquirrelVmLimits limits;
	limits.ReclaimObjects = true;

	return limits;
}

static void TestSuspendedScalarsAndNoRetainedValues() {
	SquirrelVm vm(ReclaimingLimits());
	const auto root_context = vm.CurrentContext();
	const auto child = vm.CreateContext();
	std::size_t releases = 0;
	vm.RegisterNative("endpoint_suspend", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		RequireFailure([&]() {
			static_cast<void>(current.ObserveContext(child));
		});
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	BytecodeProgram program;
	program.MainFunction.SourceName = std::string(160, 's');
	program.MainFunction.Name = std::string(160, 'f');
	program.MainFunction.Parameters = {std::string("this"), std::string("integer"), std::string("float"), std::string("boolean"), std::string("text"), std::string("userdata"), std::string("null")};
	program.MainFunction.StackSize = 12;
	program.MainFunction.Literals = {std::string("endpoint_suspend")};
	program.MainFunction.Instructions = {Instruction(8, 8, 0, 0, 9), Instruction(6, 10, 8, 9, 1), Instruction(19, 1, 1)};
	program.MainFunction.LineInformation = {{71, 0}, {82, 1}, {93, 2}};
	const auto closure = vm.CreateClosure(std::move(program));
	auto userdata = vm.CreateUserData(std::array<std::uint8_t, 1>{47});
	vm.SetUserDataReleaseHook(userdata, [&](SquirrelVm&, const SquirrelValue&, std::span<std::uint8_t> payload) {
		Require(payload[0] == 47, "observed owner payload changed before release");
		++releases;
	});
	const auto userdata_identity = vm.BorrowObjectIdentity(userdata);
	std::array<SquirrelValue, 7> arguments{vm.RootTableInContext(child), std::int32_t{-256}, std::bit_cast<float>(std::uint32_t{0x80000001}), true, std::string(160, 't'), userdata, SquirrelValue{}};
	Require(vm.InvokeInContext(child, closure, arguments).State == sgre::script::SquirrelRunState::Suspended, "independent scalar fixture did not actually suspend");
	const auto before = vm.RetainedReferenceCount(userdata);
	const auto observed = vm.ObserveContext(child, 1, 8);
	const auto again = vm.ObserveContext(child, 1, 8);
	Require(observed.State == sgre::script::SquirrelContextState::Suspended && observed.FrameCount == 1 && observed.Frames.size() == 1 && observed.NativeFrameCount == 0 && observed.SuspendedRegister == 10, "snapshot did not copy the real suspended CALL endpoint");
	const auto& frame = observed.Frames[0];
	Require(frame.Source == std::string(128, 's') && frame.Function == std::string(128, 'f') && frame.SourceTruncated && frame.FunctionTruncated && frame.NextInstruction == 2 && frame.PreviousOperation && frame.PreviousOperation->Opcode == 6 && frame.SourceLine == 82, "source, function, line or previous CALL metadata changed");
	Require(frame.RegisterCount == 12 && frame.Registers.size() == 8 && frame.RegistersTruncated && frame.Registers[1].Value.Integer == -256 && frame.Registers[2].Value.FloatBits == 0x80000001 && frame.Registers[3].Value.Boolean == true && frame.Registers[4].Value.Text == std::string(128, 't') && frame.Registers[4].Value.TextTruncated, "snapshot changed primitive values or their bounded bit/text representation");
	Require(frame.Registers[5].Value.Type == sgre::script::SquirrelValueType::UserData && frame.Registers[5].Value.ObjectIndex == userdata_identity.Index && frame.Registers[5].Value.ObjectGeneration == userdata_identity.Generation && frame.Registers[6].Value.Type == sgre::script::SquirrelValueType::Null, "snapshot did not preserve non-owning object identity and null type");
	Require(again.Frames[0].NextInstruction == frame.NextInstruction && vm.RetainedReferenceCount(userdata) == before && vm.CurrentContext() == root_context && vm.ContextState(child) == sgre::script::SquirrelContextState::Suspended, "observation selected, resumed, or retained the observed context/value");
	arguments = {};
	userdata = {};
	Require(vm.ResumeInContext(child, std::int32_t{59}).Value == SquirrelValue(std::int32_t{-256}), "snapshot changed the actual resume destination or continuation");
	vm.ResetContext(child);
	Require(releases == 1 && observed.Frames[0].Registers[5].Value.ObjectIndex == userdata_identity.Index, "scalar snapshot retained the retired userdata owner");
	vm.ReleaseContext(child);
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(child));
	});
	vm.Close();
	vm.RethrowReleaseFailure();
	Require(observed.Frames[0].Registers[4].Value.Text == std::string(128, 't'), "copied diagnostic text did not remain independent after real VM close");
}

static void TestBoundsContextsAndNestedFrames() {
	SquirrelVm vm;
	const auto child = vm.CreateContext();
	SquirrelVm foreign;
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(foreign.RootContext()));
	});
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(child, 0));
	});
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(child, 33));
	});
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(child, 1, 17));
	});
	Require(vm.ObserveContext(child).State == sgre::script::SquirrelContextState::Idle && vm.ObserveContext(child).Frames.empty(), "idle context acquired an authored execution frame");
	vm.RegisterNative("nested_suspend", [](SquirrelVm&, std::span<const SquirrelValue>) {
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto inner = vm.CreateClosure(CallingProgram("nested_suspend"));
	BytecodeProgram outer;
	outer.MainFunction.SourceName = std::string("independent-parent.nut");
	outer.MainFunction.Name = std::string("outer");
	outer.MainFunction.Parameters = {std::string("this"), std::string("callable")};
	outer.MainFunction.StackSize = 4;
	outer.MainFunction.Instructions = {Instruction(10, 2, 0), Instruction(6, 3, 1, 2, 1), Instruction(19, 1, 3)};
	const auto closure = vm.CreateClosure(std::move(outer));
	const std::array<SquirrelValue, 2> arguments{vm.RootTableInContext(child), inner};
	Require(vm.InvokeInContext(child, closure, arguments).State == sgre::script::SquirrelRunState::Suspended, "nested fixture did not retain two actual frames");
	const auto all = vm.ObserveContext(child, 2, 4);
	const auto tail = vm.ObserveContext(child, 1, 0);
	Require(all.FrameCount == 2 && all.Frames.size() == 2 && all.Frames[0].Function == "outer" && all.Frames[1].Function == "endpoint_fixture" && tail.FrameCount == 2 && tail.FramesTruncated && tail.Frames.size() == 1 && tail.Frames[0].Function == "endpoint_fixture" && tail.Frames[0].Registers.empty() && tail.Frames[0].RegistersTruncated, "bounded snapshot did not retain the actual most recent frame tail");
	Require(vm.ResumeInContext(child, std::int32_t{83}).Value == SquirrelValue(std::int32_t{83}), "bounded zero-register snapshot changed nested continuation");
	vm.ReleaseContext(child);
	vm.Close();
	RequireFailure([&]() {
		static_cast<void>(vm.ObserveContext(vm.RootContext()));
	});
	vm.RethrowReleaseFailure();
}

static void TestActualChildPredicateAndGuards() {
	SquirrelVm vm;
	ScriptScheduler scheduler(vm);
	std::optional<ScriptThreadHandle> child;
	auto child_program = CallingProgram("unused");
	child_program.MainFunction.Instructions = {Instruction(2, 1, 73), Instruction(19, 1, 1)};
	const auto child_closure = vm.CreateClosure(std::move(child_program));
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	vm.RegisterNative("await_actual_child", [&](SquirrelVm&, std::span<const SquirrelValue>) {
		child = scheduler.Fork(child_closure, arguments, 5);
		const auto parent = *scheduler.CurrentThread();
		scheduler.WaitForChild(parent, *child);
		RequireFailure([&]() {
			static_cast<void>(scheduler.ObserveWait(parent));
		});
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const auto parent = scheduler.Fork(vm.CreateClosure(CallingProgram("await_actual_child")), arguments);
	static_cast<void>(scheduler.Advance(1));
	const auto tick = scheduler.CurrentTick();
	const auto observed = scheduler.ObserveWait(parent);
	const auto child_observed = scheduler.ObserveWait(*child);
	Require(observed.Status == ScriptThreadStatus::Running && observed.WaitingChild == child && observed.TimeoutTicks == -1 && observed.NotificationCount == 0 && observed.Context && !observed.ExecutionStopped && child_observed.Status == ScriptThreadStatus::LoadingFunction && !child_observed.Context, "stored actual child predicate was replaced with inferred thread status");
	Require(vm.ObserveContext(*observed.Context).State == sgre::script::SquirrelContextState::Suspended && scheduler.CurrentTick() == tick && scheduler.ObserveWait(parent).WaitingChild == child, "snapshot advanced the actual scheduler or child continuation");
	Require(scheduler.Advance(1).Completed == std::vector<ScriptThreadHandle>{*child, parent} && scheduler.Info(parent).Result == SquirrelValue(std::int32_t{73}), "snapshot changed actual child-first wake ordering/result");
	scheduler.Release(*child);
	RequireFailure([&]() {
		static_cast<void>(scheduler.ObserveWait(*child));
	});
	ScriptScheduler other(vm);
	const auto foreign = other.CreateDormant();
	RequireFailure([&]() {
		static_cast<void>(scheduler.ObserveWait(foreign));
	});
	other.Release(foreign);
	scheduler.Release(parent);
	vm.Close();
	RequireFailure([&]() {
		static_cast<void>(scheduler.ObserveWait(parent));
	});
	vm.RethrowReleaseFailure();
}

static void TestNotificationTimeoutAndNoStringRoots() {
	SquirrelVm vm(ReclaimingLimits());
	ScriptScheduler scheduler(vm);
	sgre::script::SquirrelHostRoot weak;
	vm.RegisterNative("await_notifications", [&](SquirrelVm& current, std::span<const SquirrelValue>) {
		std::vector<SquirrelValue> names;
		for (std::size_t index = 0; index < 40; ++index) {
			names.push_back(current.CreateString(std::string(160, 'n') + std::to_string(index)));
		}
		weak = current.Retain(current.CreateWeakReference(names[0]));
		scheduler.WaitForNotificationValues(*scheduler.CurrentThread(), names, 2.9f);
		sgre::script::SquirrelNativeResult result;
		result.Suspend = true;

		return result;
	});
	const std::array<SquirrelValue, 1> arguments{vm.RootTable()};
	const auto thread = scheduler.Fork(vm.CreateClosure(CallingProgram("await_notifications")), arguments);
	static_cast<void>(scheduler.Advance(1));
	const auto observed = scheduler.ObserveWait(thread);
	Require(observed.Status == ScriptThreadStatus::Running && observed.TimeoutTicks == 2 && observed.NotificationCount == 40 && observed.Notifications.size() == 32 && observed.NotificationsTruncated && observed.NotificationTextTruncated && observed.Notifications[0] == std::string(128, 'n'), "actual notification/timeout predicate was not copied with separate truncation flags");
	Require(vm.Type(vm.ResolveWeakReference(weak.Value())) == sgre::script::SquirrelValueType::String, "snapshot unexpectedly released a live wait name");
	static_cast<void>(scheduler.Advance(2));
	Require(scheduler.ObserveWait(thread).TimeoutTicks == 0 && scheduler.ObserveWait(thread).NotificationCount == 40, "timeout endpoint at zero was changed by observation");
	scheduler.SetExecutionStopped(thread, true);
	Require(scheduler.ObserveWait(thread).ExecutionStopped, "stored execution stop predicate is absent");
	scheduler.Stop(thread);
	Require(vm.Type(vm.ResolveWeakReference(weak.Value())) == sgre::script::SquirrelValueType::Null && observed.Notifications[0] == std::string(128, 'n'), "copied notification diagnostics retained a cancelled canonical wait string");
	scheduler.Release(thread);
	vm.Close();
	vm.RethrowReleaseFailure();
}

static void TestTypedPendingAndActualCachePin(const std::filesystem::path& directory) {
	const auto raw_file = directory / "compiled.bin";
	const auto wrapped_file = directory / "wrapped.bin";
	WriteFixture(raw_file, CompiledFixture());
	WriteFixture(wrapped_file, WrappedFixture(CompiledFixture(), "script/item.nut.m"));
	SquirrelVm vm;
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::core::BackgroundCallManager background;
	sgre::scene::CommonProcessTasks driver;
	ScriptScheduler scheduler(vm);
	sgre::io::ScriptReadBuffer physical_receiver;
	static_cast<void>(sgre::io::AppendPreparedScriptBufferReader(driver, background, mounts, physical_receiver, PreparedFixture(wrapped_file)));
	Require(!physical_receiver.Observe().Ready && !physical_receiver.CanClose(), "prepared physical lane lost its actual pending borrow gate");
	driver.Parallel().Update();
	DrainLane(background);
	driver.Parallel().Update();
	driver.Parallel().Update();
	Require(physical_receiver.Observe().Ready && physical_receiver.Observe().Completion == sgre::io::FileReadCompletion::Succeeded && physical_receiver.Observe().ByteCount == CompiledFixture().size(), "strict physical fixture did not produce the real independent compiled bytes");
	physical_receiver.Close();
	const auto actual_binary = ProduceActualActiveBinary(cache, "script/item.nut", raw_file);
	const auto ready = scheduler.ForkFile(vm.CreateString("script/item.nut"), cache, {});
	const auto ready_before = scheduler.ObserveWait(ready);
	const auto pin_count = cache.Info(actual_binary).References;
	Require(ready_before.FileOrigin && ready_before.TypedFileOrigin && ready_before.HasScriptBuffer && ready_before.BufferReady && ready_before.BufferCanClose && ready_before.BufferCompletion == sgre::io::FileReadCompletion::Succeeded && ready_before.BufferStorage == sgre::io::ScriptBufferStorage::ManagerBinaryPin && ready_before.Status == ScriptThreadStatus::LoadingFile && !ready_before.Context, "actual ActiveBinary receiving state was replaced with a synthetic ready token");
	static_cast<void>(scheduler.ObserveWait(ready));
	Require(cache.Info(actual_binary).References == pin_count && !scheduler.ObserveWait(ready).Context, "snapshot consumed the actual compile gate or changed the raw cache pin");
	Require(scheduler.Advance(1).Instructions == 2, "observed actual binary did not compile and execute its genuine handoff");
	const auto ready_after = scheduler.ObserveWait(ready);
	Require(ready_after.Status == ScriptThreadStatus::None && !ready_after.HasScriptBuffer && ready_after.Context && ready_after.Filename == "script/item.nut" && cache.Info(actual_binary).References + 1 == pin_count, "real compile/release ordering changed after an endpoint snapshot");
	const auto pending = scheduler.ForkFile(vm.CreateString("script/missing.nut"), cache, {});
	Require(scheduler.AppendFileRead(pending, driver, background, mounts, "synthetic-seed").has_value(), "pending typed receiver was not attached to its real Driver owner");
	const auto pending_before = scheduler.ObserveWait(pending);
	Require(pending_before.HasScriptBuffer && !pending_before.BufferReady && !pending_before.BufferCanClose && pending_before.BufferCompletion == sgre::io::FileReadCompletion::Pending && pending_before.BufferStorage == sgre::io::ScriptBufferStorage::OwnedRaw && !pending_before.Context, "pending actual raw receiver was prematurely published by observation");
	driver.Parallel().Update();
	DrainLane(background);
	driver.Parallel().Update();
	driver.Parallel().Update();
	const auto failed = scheduler.ObserveWait(pending);
	Require(failed.BufferReady && failed.BufferCanClose && failed.BufferCompletion == sgre::io::FileReadCompletion::ReadFailed && !failed.Context && failed.Status == ScriptThreadStatus::LoadingFile, "actual selected mapping failure was hidden or compiled by observation");
	static_cast<void>(scheduler.Advance(1));
	Require(scheduler.Info(pending).Status == ScriptThreadStatus::None && !scheduler.ObserveWait(pending).HasScriptBuffer && !scheduler.Info(pending).Error.empty(), "actual failed typed loader did not retain its error and release its singular buffer");
	scheduler.Release(ready);
	scheduler.Release(pending);
	cache.Release(actual_binary);
	vm.Close();
	vm.RethrowReleaseFailure();
	background.Close();
	std::filesystem::remove(raw_file);
	std::filesystem::remove(wrapped_file);
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("endpoint fixtures require one owned ignored directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestSuspendedScalarsAndNoRetainedValues();
		TestBoundsContextsAndNestedFrames();
		TestActualChildPredicateAndGuards();
		TestNotificationTimeoutAndNoStringRoots();
		TestTypedPendingAndActualCachePin(directory);
		std::cout << "five bounded actual VM/wait/typed-buffer endpoint diagnostic groups pass\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
