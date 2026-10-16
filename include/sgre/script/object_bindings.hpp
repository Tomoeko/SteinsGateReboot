#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace sgre::script {
	struct ScriptObjectBindingLimits {
		std::size_t MaxInstances = 65536;
		std::size_t MaxSubscriptions = 65536;
	};

	class ScriptObjectBindings {
	public:
		using CompletionCallback = std::function<bool(const SquirrelValue&)>;
		using NativeRelease = std::function<void(const SquirrelValue&)>;

		// Native callbacks hold a weak owner. The VM and borrowed registry identities must outlive this owner.
		explicit ScriptObjectBindings(SquirrelVm& vm, ScriptObjectBindingLimits limits = {});
		~ScriptObjectBindings();
		ScriptObjectBindings(const ScriptObjectBindings&) = delete;
		ScriptObjectBindings& operator=(const ScriptObjectBindings&) = delete;
		ScriptObjectBindings(ScriptObjectBindings&&) = delete;
		ScriptObjectBindings& operator=(ScriptObjectBindings&&) = delete;

		[[nodiscard]] SquirrelValue ObjectClass() const;
		void AttachInstance(const SquirrelValue& instance, const SquirrelValue& delegate = {});
		// Installs the singular native release hook: script destructor, then the supplied owner cleanup.
		void ConfigureNativeRelease(const SquirrelValue& instance, NativeRelease release);
		[[nodiscard]] bool HasInstance(const SquirrelValue& instance) const;
		[[nodiscard]] SquirrelValue Delegate(const SquirrelValue& instance) const;
		void SetDelegate(const SquirrelValue& instance, const SquirrelValue& delegate);
		void Subscribe(const SquirrelValue& object, const SquirrelValue& listener, CompletionCallback callback);
		void Unsubscribe(const SquirrelValue& object, const SquirrelValue& listener);
		void Notify(const SquirrelValue& object, bool all = true);
		// Preflight before derived cleanup; active completion callbacks still borrow this native state.
		void RequireCanRelease(const SquirrelValue& instance) const;
		// Explicit native lifetime completion notifies subscribers before removing the Object record.
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};
} // namespace sgre::script
