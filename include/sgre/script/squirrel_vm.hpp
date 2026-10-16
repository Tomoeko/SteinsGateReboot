#pragma once

#include "sgre/script/squirrel_bytecode.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sgre::script {
	// Values retain object storage; explicit host roots separately protect it from native garbage collection.
	struct SquirrelObjectHandle {
		SquirrelObjectHandle();
		~SquirrelObjectHandle();
		SquirrelObjectHandle(const SquirrelObjectHandle& other);
		SquirrelObjectHandle& operator=(const SquirrelObjectHandle& other);
		SquirrelObjectHandle(SquirrelObjectHandle&& other) noexcept;
		SquirrelObjectHandle& operator=(SquirrelObjectHandle&& other) noexcept;
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const SquirrelObjectHandle& other) const;

	private:
		struct Lifetime;
		std::shared_ptr<Lifetime> m_Lifetime;
		bool m_IsStrong = false;
		void Acquire();
		void Release() noexcept;
		friend class SquirrelVm;
	};

	// A generation-checked non-owning identity; resolving it never retargets a recycled arena slot.
	struct SquirrelObjectIdentity {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const SquirrelObjectIdentity& other) const;

	private:
		std::weak_ptr<void> m_LifetimeToken;
		friend class SquirrelVm;
	};

	// String bytes are immutable. Shared copies retain their canonical native identity after VM adoption.
	class SquirrelString {
	public:
		SquirrelString(std::string text);
		SquirrelString(const char* text);
		SquirrelString(const SquirrelString& other);
		SquirrelString& operator=(const SquirrelString& other);
		SquirrelString(SquirrelString&&) noexcept;
		SquirrelString& operator=(SquirrelString&&) noexcept;
		~SquirrelString();
		[[nodiscard]] const std::string& Text() const;
		[[nodiscard]] bool SharesIdentity(const SquirrelString& other) const;
		bool operator==(const SquirrelString& other) const;

	private:
		struct Storage;
		explicit SquirrelString(std::shared_ptr<Storage> storage);
		[[nodiscard]] std::shared_ptr<Storage> Record() const;
		std::shared_ptr<Storage> m_Storage;
		std::weak_ptr<Storage> m_BorrowedStorage;
		friend class SquirrelVm;
	};

	using SquirrelValue = std::variant<std::monostate, std::int32_t, float, bool, SquirrelString, SquirrelObjectHandle>;
	[[nodiscard]] const std::string& StringText(const SquirrelValue& value);
	[[nodiscard]] const std::string* TryStringText(const SquirrelValue& value);
	// Native sq_getinteger accepts booleans and uses CVTTSS2SI for floats; other types leave the caller's output unchanged.
	[[nodiscard]] std::optional<std::int32_t> TryNativeInteger(const SquirrelValue& value);

	// An explicit native AddRef/Release relationship; the scoped root does not extend its VM's lifetime.
	class SquirrelHostRoot {
	public:
		SquirrelHostRoot();
		~SquirrelHostRoot();
		SquirrelHostRoot(const SquirrelHostRoot&) = delete;
		SquirrelHostRoot& operator=(const SquirrelHostRoot&) = delete;
		SquirrelHostRoot(SquirrelHostRoot&&) noexcept;
		SquirrelHostRoot& operator=(SquirrelHostRoot&&) noexcept;
		[[nodiscard]] const SquirrelValue& Value() const;

	private:
		struct Implementation;
		explicit SquirrelHostRoot(std::unique_ptr<Implementation> implementation);
		std::unique_ptr<Implementation> m_Implementation;
		friend class SquirrelVm;
	};

	enum class SquirrelValueType {
		Null,
		Integer,
		Float,
		Boolean,
		String,
		Table,
		Array,
		Closure,
		NativeClosure,
		Class,
		Instance,
		Generator,
		WeakReference,
		UserData
	};

	enum class SquirrelPropertyReadKind {
		RawPresence,
		Integer,
		Number,
		Boolean,
		String
	};

	// Only converted native values escape; no VM result owner survives its physical stack pop.
	struct SquirrelPropertyReadResult {
		bool Present = false;
		std::int32_t Integer = 0;
		float Number = 0;
		bool Boolean = false;
		std::string String;
	};

	struct SquirrelContextHandle {
		std::uint32_t Index = 0;
		std::uint64_t Generation = 0;
		const void* Owner = nullptr;
		bool operator==(const SquirrelContextHandle&) const = default;
	};

	// Native adapters retain registry ownership; the identity borrows its type tag.
	struct SquirrelInstanceHostIdentity {
		const void* TypeTag = nullptr;
		std::uint64_t Value = 0;
		bool operator==(const SquirrelInstanceHostIdentity&) const = default;
	};

	enum class SquirrelContextState {
		Idle,
		Running,
		Suspended
	};

	class SquirrelVmException : public std::runtime_error {
	public:
		SquirrelVmException(SquirrelValue value, std::string message);
		[[nodiscard]] const SquirrelValue& ThrownValue() const;

	private:
		SquirrelValue m_Value;
	};

	enum class SquirrelRunState {
		Completed,
		Suspended
	};

	struct SquirrelNativeResult {
		// Existing value callbacks remain compatibility mode until their native stack status is verified.
		SquirrelValue Value;
		bool Suspend = false;
		std::optional<std::int32_t> NativeStatus = std::nullopt;
		bool PushReturnedValue = false;
		[[nodiscard]] static SquirrelNativeResult NoValues();
		[[nodiscard]] static SquirrelNativeResult ReturnValue(SquirrelValue value);
		[[nodiscard]] static SquirrelNativeResult StackStatus(std::int32_t status);
	};

	struct SquirrelNativeSignature {
		// Positive counts are exact; negative counts require at least their absolute value.
		std::int32_t ParameterCheck = 0;
		std::vector<std::int32_t> TypeCheck;
		std::optional<std::string> Name;
	};

	struct SquirrelRunResult {
		SquirrelRunState State = SquirrelRunState::Completed;
		SquirrelValue Value;
		std::uint64_t Instructions = 0;
		SquirrelContextHandle Context;
	};

	struct SquirrelVmLimits {
		std::size_t MaxObjects = 65536;
		// Enable only after native adapters retain external roots and register their release hooks.
		bool ReclaimObjects = false;
		std::size_t MaxContexts = 256;
		std::size_t MaxContainerSlots = 1024 * 1024;
		std::size_t MaxStringBytes = 16 * 1024 * 1024;
		std::size_t MaxInternedStrings = 65536;
		std::size_t MaxInternedStringBytes = 64 * 1024 * 1024;
		std::size_t MaxFrames = 100;
		std::size_t MaxRegisters = 1024 * 1024;
		std::size_t MaxTraps = 65536;
		std::size_t MaxUserDataBytes = 8 * 1024 * 1024;
	};

	struct SquirrelTraceEvent {
		std::string_view Source;
		std::string_view Function;
		std::size_t Instruction = 0;
		SquirrelInstruction Operation;
		SquirrelContextHandle Context;
	};

	inline constexpr std::size_t kSquirrelObservationFrames = 32;
	inline constexpr std::size_t kSquirrelObservationRegisters = 16;
	inline constexpr std::size_t kSquirrelObservationTextBytes = 128;

	// Authored diagnostics own only scalar/string copies, never VM values or roots.
	struct SquirrelObservedValue {
		SquirrelValueType Type = SquirrelValueType::Null;
		std::optional<std::int32_t> Integer;
		std::optional<std::uint32_t> FloatBits;
		std::optional<bool> Boolean;
		std::string Text;
		bool TextTruncated = false;
		std::optional<std::uint32_t> ObjectIndex;
		std::uint64_t ObjectGeneration = 0;
	};

	struct SquirrelObservedRegister {
		std::size_t Index = 0;
		SquirrelObservedValue Value;
	};

	struct SquirrelObservedFrame {
		std::string Source;
		std::string Function;
		bool SourceTruncated = false;
		bool FunctionTruncated = false;
		std::size_t NextInstruction = 0;
		std::optional<SquirrelInstruction> PreviousOperation;
		std::uint32_t SourceLine = 0;
		std::size_t RegisterCount = 0;
		bool RegistersTruncated = false;
		std::vector<SquirrelObservedRegister> Registers;
	};

	struct SquirrelContextObservation {
		SquirrelContextState State = SquirrelContextState::Idle;
		std::size_t FrameCount = 0;
		bool FramesTruncated = false;
		std::size_t NativeFrameCount = 0;
		bool HasSuspendedAction = false;
		std::uint8_t SuspendedRegister = 0;
		std::vector<SquirrelObservedFrame> Frames;
	};

	class SquirrelVm {
	public:
		using NativeCallback = std::function<SquirrelNativeResult(SquirrelVm&, std::span<const SquirrelValue>)>;
		using UserDataReleaseCallback = std::function<void(SquirrelVm&, const SquirrelValue&, std::span<std::uint8_t>)>;
		using InstanceReleaseCallback = std::function<void(SquirrelVm&, const SquirrelValue&)>;
		using TraceCallback = std::function<void(const SquirrelTraceEvent&)>;

		explicit SquirrelVm(SquirrelVmLimits limits = {});
		~SquirrelVm();
		// Observes the existing Close entry guards without retiring values, roots or contexts.
		void RequireCanClose() const;
		void Close();
		[[nodiscard]] bool IsClosed() const;
		SquirrelVm(const SquirrelVm&) = delete;
		SquirrelVm& operator=(const SquirrelVm&) = delete;
		SquirrelVm(SquirrelVm&&) = delete;
		SquirrelVm& operator=(SquirrelVm&&) = delete;

		// The executing context owns this root slot; a new context copies its creator's current root.
		[[nodiscard]] SquirrelValue RootTable() const;
		void SetRootTable(const SquirrelValue& table);
		[[nodiscard]] SquirrelValue RootTableInContext(SquirrelContextHandle context) const;
		void SetRootTableInContext(SquirrelContextHandle context, const SquirrelValue& table);
		[[nodiscard]] SquirrelValue ConstantTable() const;
		void SetConstantTable(const SquirrelValue& table);
		[[nodiscard]] SquirrelHostRoot Retain(const SquirrelValue& value);
		[[nodiscard]] std::uint32_t RetainedReferenceCount(const SquirrelValue& value) const;
		[[nodiscard]] SquirrelValue CreateString(std::string text);
		[[nodiscard]] SquirrelValue CreateTable();
		// Payloads are fully initialized; native uninitialized allocation bytes are not reconstructed.
		[[nodiscard]] SquirrelValue CreateUserData(std::span<const std::uint8_t> payload);
		// The span borrows live userdata and requires its owner to survive all VM operations using it.
		[[nodiscard]] std::span<std::uint8_t> UserDataBytes(const SquirrelValue& userdata);
		void SetUserDataReleaseHook(const SquirrelValue& userdata, UserDataReleaseCallback callback);
		void SetUserDataTypeTag(const SquirrelValue& userdata, const void* type_tag);
		[[nodiscard]] const void* UserDataTypeTag(const SquirrelValue& userdata) const;
		void SetUserDataDelegate(const SquirrelValue& userdata, const SquirrelValue& delegate);
		[[nodiscard]] SquirrelValue CreateArray(std::span<const SquirrelValue> values = {});
		void AppendArray(const SquirrelValue& array, const SquirrelValue& value);
		[[nodiscard]] SquirrelValue RemoveArray(const SquirrelValue& array, std::int32_t index);
		void ClearArray(const SquirrelValue& array);
		[[nodiscard]] SquirrelValue CreateClass(const SquirrelValue& base = {}, const SquirrelValue& attributes = {});
		// Native 2252c0 class tag is a borrowed opaque identity, separate from userdata and native instance payloads.
		// The tag owner encloses this class and its derived queries. Null/reassignment remain valid after class locking.
		void SetClassTypeTag(const SquirrelValue& class_value, const void* type_tag);
		[[nodiscard]] const void* ClassTypeTag(const SquirrelValue& class_value) const;
		// Native 223320 reads only the immediate class tag; NewClass initializes its own tag to null even with a tagged base.
		[[nodiscard]] const void* InstanceClassTypeTag(const SquirrelValue& instance) const;
		// The ancestry predicate from 222f30, without reading or substituting its separately unrecovered raw instance payload.
		// A null requested tag accepts any checked live instance. No tag is retained, dereferenced, serialized or marked by GC.
		[[nodiscard]] bool InstanceClassHasTypeTag(const SquirrelValue& instance, const void* type_tag) const;
		// This is the native create-instance operation; it does not invoke constructor.
		[[nodiscard]] SquirrelValue CreateInstance(const SquirrelValue& class_value);
		[[nodiscard]] SquirrelObjectIdentity BorrowObjectIdentity(const SquirrelValue& object) const;
		[[nodiscard]] std::optional<SquirrelValue> ResolveObjectIdentity(const SquirrelObjectIdentity& identity) const;
		// Lends one const instance receiver for this callback only, without acquiring or marking an object root.
		// Copies, host roots, VM stores and invocation outputs retain normally. Do not keep a pointer/reference to receiver.
		// VM lifetime encloses the call; same-receiver reentry and Close reject. Distinct receivers nest to depth 64. Collection may retire an unrooted receiver.
		void DispatchBorrowedReceiver(const SquirrelObjectIdentity& identity, const std::function<void(const SquirrelValue&)>& operation);
		[[nodiscard]] std::uint32_t CollectGarbage();
		void SetInstanceReleaseHook(const SquirrelValue& instance, InstanceReleaseCallback callback);
		void RethrowReleaseFailure();
		void SetInstanceHostIdentity(const SquirrelValue& instance, SquirrelInstanceHostIdentity identity);
		[[nodiscard]] SquirrelInstanceHostIdentity InstanceHostIdentity(const SquirrelValue& instance, const void* type_tag) const;
		[[nodiscard]] SquirrelValue CreateClosure(BytecodeProgram program);
		// Selects a checked idle receiving context for prototype preparation; no closure root or context owner is added.
		[[nodiscard]] SquirrelValue CreateClosureInContext(SquirrelContextHandle context, BytecodeProgram program);
		[[nodiscard]] SquirrelValue CreateNativeClosure(std::string name, NativeCallback callback, SquirrelNativeSignature signature = {});
		// Captures use the native stack push order; callbacks see them appended in reverse order.
		[[nodiscard]] SquirrelValue CreateNativeClosureWithCaptures(std::string name, NativeCallback callback, std::span<const SquirrelValue> pushed_captures, SquirrelNativeSignature signature = {});
		void SetNativeSignature(const SquirrelValue& callable, SquirrelNativeSignature signature);
		// These checked raw cells are available only while an actual native callback frame is active.
		[[nodiscard]] std::size_t NativeStackSize() const;
		[[nodiscard]] SquirrelValue NativeStackValue(std::int32_t index) const;
		// Native scalar getters can inspect inactive allocated cells; this read never grows or writes them.
		[[nodiscard]] SquirrelValue NativeAllocatedStackValue(std::int32_t index) const;
		void PushNativeValue(const SquirrelValue& value);
		void SetNativeStackSize(std::size_t count);
		// Native225340/25b950 validates the active VM only; CALL consumes the returned raw suspension request.
		[[nodiscard]] SquirrelNativeResult RequestNativeSuspension();
		[[nodiscard]] SquirrelValue CreateWeakReference(const SquirrelValue& value);
		[[nodiscard]] SquirrelValue ResolveWeakReference(const SquirrelValue& value) const;
		[[nodiscard]] SquirrelValue BindEnvironment(const SquirrelValue& callable, const SquirrelValue& environment);
		[[nodiscard]] bool HasBoundEnvironment(const SquirrelValue& callable) const;
		void RegisterNative(std::string name, NativeCallback callback);
		void InstallRecoveredBaseLibrary();
		void NewSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value, bool is_static = false);
		void SetSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value);
		[[nodiscard]] SquirrelValue GetSlot(const SquirrelValue& object, const SquirrelValue& key) const;
		[[nodiscard]] std::optional<SquirrelValue> FindRawSlot(const SquirrelValue& object, const SquirrelValue& key) const;
		// Native general get: executes _get, permits type delegates except Class, and never retries the root.
		[[nodiscard]] SquirrelValue ReadSlot(const SquirrelValue& object, const SquirrelValue& key, std::uint64_t instruction_budget = 1000000);
		// General native lookup runs against the selected VM context and restores its caller afterward.
		[[nodiscard]] SquirrelValue ReadSlotInContext(SquirrelContextHandle context, const SquirrelValue& object, const SquirrelValue& key, std::uint64_t instruction_budget = 1000000);
		// Pushes object and string key, aliases the native getter output to that key cell, converts, then pops both.
		// Raw presence follows table/userdata delegates, skips metamethod/default/root retry, and pops on either outcome.
		// General failure records the native index error and returns the converter's zero/empty fallback.
		[[nodiscard]] SquirrelPropertyReadResult ReadPropertyInContext(SquirrelContextHandle context, const SquirrelValue& object, std::string_view key, SquirrelPropertyReadKind kind, std::uint64_t instruction_budget = 1000000);
		void AssignSlot(const SquirrelValue& object, const SquirrelValue& key, const SquirrelValue& value, std::uint64_t instruction_budget = 1000000);
		[[nodiscard]] bool IsInstanceOf(const SquirrelValue& instance, const SquirrelValue& base) const;
		[[nodiscard]] bool HasSlot(const SquirrelValue& object, const SquirrelValue& key) const;
		[[nodiscard]] std::size_t Length(const SquirrelValue& object) const;
		[[nodiscard]] SquirrelValueType Type(const SquirrelValue& value) const;
		[[nodiscard]] std::string TypeName(const SquirrelValue& value) const;
		[[nodiscard]] std::string ToString(const SquirrelValue& value, std::uint64_t instruction_budget = 1000000);
		[[nodiscard]] std::int32_t CompareValues(const SquirrelValue& left, const SquirrelValue& right, std::uint64_t instruction_budget = 1000000);
		void SetTraceCallback(TraceCallback callback);

		// Arguments include the receiver in position zero, matching the native VM's calling convention.
		[[nodiscard]] SquirrelRunResult Invoke(const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget = 1000000);
		[[nodiscard]] SquirrelRunResult Resume(const SquirrelValue& value = {}, std::uint64_t instruction_budget = 1000000);
		[[nodiscard]] bool IsSuspended() const;
		void ResetExecution();

		// Contexts share the object arena and constants while retaining independent root/error slots.
		[[nodiscard]] SquirrelContextHandle RootContext() const;
		[[nodiscard]] SquirrelContextHandle CurrentContext() const;
		[[nodiscard]] SquirrelValue LastError() const;
		[[nodiscard]] SquirrelValue LastErrorInContext(SquirrelContextHandle context) const;
		// Native diagnostic replacement owns the raw value without error-handler dispatch or exception wrapping.
		void SetLastError(const SquirrelValue& value);
		void SetLastErrorInContext(SquirrelContextHandle context, const SquirrelValue& value);
		[[nodiscard]] SquirrelContextHandle CreateContext();
		[[nodiscard]] SquirrelContextHandle CreateContextFrom(SquirrelContextHandle creator);
		[[nodiscard]] SquirrelContextState ContextState(SquirrelContextHandle context) const;
		// Native-callback-only exact caller metadata; bounded current frames, no copies, calls or retention.
		[[nodiscard]] bool NativeCallerMatches(std::string_view source, std::string_view function, std::size_t next_instruction, std::size_t max_frames = kSquirrelObservationFrames) const;
		// Serialized endpoint inspection performs no call, context selection, resume, or VM retention.
		[[nodiscard]] SquirrelContextObservation ObserveContext(SquirrelContextHandle context, std::size_t max_frames = kSquirrelObservationFrames, std::size_t max_registers = 8) const;
		[[nodiscard]] SquirrelRunResult InvokeInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget = 1000000, bool raise_error = true);
		// Matches retval=false: preserves native temporary/error/suspension state while discarding facade output.
		[[nodiscard]] SquirrelRunResult InvokeWithoutResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget = 1000000, bool raise_error = true);
		// Capture the real output, unwind invocation state, then retire the output in its called context (native call followed by pop).
		[[nodiscard]] SquirrelRunResult InvokeThenDiscardResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget = 1000000, bool raise_error = true);
		// Lends the completed output after call unwind, then pops only that output in its receiving context.
		// Preserves prior target/caller results. Authored guards and suspension reject without invoking the observer.
		// The observer may copy/retain the borrowed value deliberately; its parameter itself does not escape this call.
		[[nodiscard]] SquirrelRunResult InvokeObserveCompletedResultInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, const std::function<void(const SquirrelValue&)>& observer, std::uint64_t instruction_budget = 1000000, bool raise_error = true);
		[[nodiscard]] SquirrelRunResult ResumeInContext(SquirrelContextHandle context, const SquirrelValue& value = {}, std::uint64_t instruction_budget = 1000000);
		void ResetContext(SquirrelContextHandle context);
		void ReleaseContext(SquirrelContextHandle context);

	private:
		[[nodiscard]] SquirrelRunResult CallInContext(SquirrelContextHandle context, const SquirrelValue& callable, std::span<const SquirrelValue> arguments, std::uint64_t instruction_budget, bool raise_error, bool return_value, bool discard_after_unwind = false);
		class Implementation;
		std::unique_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
