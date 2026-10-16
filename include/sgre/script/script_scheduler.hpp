#pragma once

#include "sgre/script/script_buffer_load.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::script {

	struct ScriptThreadHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const ScriptThreadHandle&) const = default;
	};

	enum class ScriptThreadStatus {
		None = 0,
		LoadingFile = 1,
		LoadingFunction = 2,
		Stopped = 3,
		Running = 4,
		Waiting = 5
	};

	struct ScriptThreadInfo {
		ScriptThreadStatus Status = ScriptThreadStatus::None;
		std::int32_t Priority = 10;
		std::int32_t ElapsedTicks = 0;
		std::int32_t TimeoutTicks = -1;
		std::int32_t NativeCallResult = 0;
		bool ExecutionStopped = false;
		SquirrelValue Result;
		std::string Error;
		std::optional<SquirrelContextHandle> Context;
		SquirrelValue Filename;
		bool HasScriptBuffer = false;
	};

	inline constexpr std::size_t kScriptWaitObservationNotifications = 32;

	// Endpoint diagnostics borrow scalar owner identities and copy bounded text; they own no VM values.
	struct ScriptThreadWaitObservation {
		ScriptThreadStatus Status = ScriptThreadStatus::None;
		bool ExecutionStopped = false;
		std::int32_t ElapsedTicks = 0;
		std::int32_t TimeoutTicks = -1;
		std::optional<ScriptThreadHandle> WaitingChild;
		std::size_t NotificationCount = 0;
		std::vector<std::string> Notifications;
		bool NotificationsTruncated = false;
		bool NotificationTextTruncated = false;
		std::optional<SquirrelContextHandle> Context;
		bool FileOrigin = false;
		bool TypedFileOrigin = false;
		std::string Filename;
		bool FilenameTruncated = false;
		bool HasScriptBuffer = false;
		bool BufferReady = false;
		bool BufferCanClose = true;
		std::optional<io::FileReadCompletion> BufferCompletion;
		std::optional<io::ScriptBufferStorage> BufferStorage;
	};

	enum class ScriptThreadReceiver {
		Explicit,
		ChildRoot
	};

	struct ScriptSchedulerLimits {
		std::size_t MaxThreads = 256;
		std::size_t MaxWaitNotifications = 256;
		std::uint64_t InstructionsPerThread = 1000000;
	};

	struct ScriptSchedulerTick {
		std::uint64_t Instructions = 0;
		std::size_t ActiveThreads = 0;
		std::vector<ScriptThreadHandle> Completed;
	};

	class ScriptScheduler {
	public:
		using LoadPoll = std::function<std::optional<SquirrelValue>()>;
		using CompletionCallback = std::function<void()>;
		// The VM owns values and lazily created child contexts and must outlive this scheduler and its native closures.
		explicit ScriptScheduler(SquirrelVm& vm, ScriptSchedulerLimits limits = {});
		~ScriptScheduler();
		ScriptScheduler(const ScriptScheduler&) = delete;
		ScriptScheduler& operator=(const ScriptScheduler&) = delete;
		ScriptScheduler(ScriptScheduler&&) = delete;
		ScriptScheduler& operator=(ScriptScheduler&&) = delete;

		// Explicit arguments include their receiver; ChildRoot stores only user parameters and creates from the base VM at readiness.
		[[nodiscard]] ScriptThreadHandle Fork(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::int32_t priority = 10, ScriptThreadReceiver receiver = ScriptThreadReceiver::Explicit);
		// Poll only an already requested load; its captured VM values require explicit roots in the load service.
		// Disk I/O and bytecode decoding belong to the asset worker; pending work owns no child context.
		[[nodiscard]] ScriptThreadHandle ForkLoading(LoadPoll poll, std::span<const SquirrelValue> arguments, std::int32_t priority = 10, ScriptThreadReceiver receiver = ScriptThreadReceiver::Explicit);
		// Owns the concrete native filename/buffer fields. Request attachment is a separate phase after native association.
		[[nodiscard]] ScriptThreadHandle ForkFile(const SquirrelValue& actual_filename, resource::ResourceCache& actual_cache, std::span<const SquirrelValue> arguments, std::int32_t priority = 10, ScriptThreadReceiver receiver = ScriptThreadReceiver::ChildRoot);
		[[nodiscard]] std::optional<scene::ParallelTaskHandle> AppendFileRead(ScriptThreadHandle thread, scene::CommonProcessTasks& actual_driver, core::BackgroundCallManager& background, resource::ArchiveMounts& mounts, std::string_view seed);
		[[nodiscard]] ScriptThreadHandle CreateDormant();
		// The native active array owns registered instances; dormant instances receive no registration root.
		void AttachNativeInstance(ScriptThreadHandle thread, const SquirrelValue& instance, CompletionCallback completion = {});
		[[nodiscard]] SquirrelValue NativeThreadList() const;
		[[nodiscard]] ScriptThreadInfo Info(ScriptThreadHandle thread) const;
		// Exact AdvanceThread wait predicates, without selection, notification or any retained VM root.
		[[nodiscard]] ScriptThreadWaitObservation ObserveWait(ScriptThreadHandle thread) const;
		[[nodiscard]] std::vector<ScriptThreadHandle> ActiveThreads() const;
		[[nodiscard]] std::optional<ScriptThreadHandle> CurrentThread() const;
		[[nodiscard]] std::int32_t CurrentTick() const;
		[[nodiscard]] std::int32_t DiffTick() const;
		[[nodiscard]] ScriptSchedulerTick Advance(std::int32_t delta_ticks);
		// Native267990 starts only a LoadingFunction thread through its ordinary step, with zero ticks.
		void Cue(ScriptThreadHandle thread);
		void SetPriority(ScriptThreadHandle thread, std::int32_t priority);
		void SetExecutionStopped(ScriptThreadHandle thread, bool stopped);
		void Stop(ScriptThreadHandle thread);
		// Native267bd0 retains the suspended context/callable/resume while publishing an explicit exit result.
		void Exit(ScriptThreadHandle thread, const SquirrelValue& result = {});
		void RequireCanStop(ScriptThreadHandle thread) const;
		// Actual driver cleanup visits the live native array before VM Close. File-origin ScriptBuffer owners remain unsupported.
		// Completion services must be the attached native Object notifications; contexts/results/callables survive until owner release.
		void ShutdownNativeFunctionThreads();
		// Typed file rows require their actual reader borrows fenced first; generic LoadPoll rows remain unsupported.
		// Readonly all-row admission; actual shutdown still rechecks live rows after callback mutation.
		void RequireCanShutdownNativeThreads() const;
		void ShutdownNativeThreads();
		void Release(ScriptThreadHandle thread);

		// A native callback sets the wait then returns Suspend=true; completion wakes the original call.
		void WaitForChild(ScriptThreadHandle parent, ScriptThreadHandle child);
		void WakeChild(ScriptThreadHandle parent, ScriptThreadHandle child);
		void WaitForNotifications(ScriptThreadHandle thread, std::span<const std::string> notifications, std::optional<SquirrelValue> timeout = {});
		// Native waits retain original values and compare NUL-terminated notification prefixes.
		void WaitForNotificationValues(ScriptThreadHandle thread, std::span<const SquirrelValue> notifications, std::optional<SquirrelValue> timeout = {});
		void Notify(std::string_view notification);

	private:
		[[nodiscard]] std::size_t RequireCanShutdownNativeThreadsImpl(bool allow_typed_files) const;
		void ShutdownNativeThreadsImpl(bool allow_typed_files);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
