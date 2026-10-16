#pragma once

#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sgre::script {

	struct ScriptNativeDrawObjectCallbacks {
		std::function<void(std::uint32_t, std::uint32_t)> Draw;
		std::function<void()> Release;
	};

	class ScriptNativeDrawObjectState;

	class ScriptNativeDrawObjectBorrow {
	public:
		[[nodiscard]] bool IsLive() const noexcept;
		// This token never retains the script receiver. It checks the native payload after the VM pop.
		void Draw(std::uint32_t argument, std::uint32_t opacity) const;

	private:
		friend class ScriptDrawObjectBindings;
		explicit ScriptNativeDrawObjectBorrow(std::weak_ptr<ScriptNativeDrawObjectState> owner);
		std::weak_ptr<ScriptNativeDrawObjectState> m_Owner;
	};

	class ScriptDrawObjectBindings {
	public:
		// Concrete first-party subclass owners attach through C++; the original DrawObject script factory is forbidden.
		ScriptDrawObjectBindings(SquirrelVm& vm, ScriptObjectBindings& objects, std::size_t max_instances = 1024);
		~ScriptDrawObjectBindings();
		ScriptDrawObjectBindings(const ScriptDrawObjectBindings&) = delete;
		ScriptDrawObjectBindings& operator=(const ScriptDrawObjectBindings&) = delete;
		ScriptDrawObjectBindings(ScriptDrawObjectBindings&&) = delete;
		ScriptDrawObjectBindings& operator=(ScriptDrawObjectBindings&&) = delete;

		[[nodiscard]] SquirrelValue DrawObjectClass() const;
		void AttachNativeDrawOwner(const SquirrelValue& instance, ScriptNativeDrawObjectCallbacks callbacks);
		[[nodiscard]] std::optional<ScriptNativeDrawObjectBorrow> BorrowNativeDrawOwner(const SquirrelValue& instance) const;
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
