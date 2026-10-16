#pragma once

#include "sgre/script/squirrel_vm.hpp"

#include <array>
#include <cstddef>
#include <functional>

namespace sgre::script {

	class ScriptAffineLayerLinks;

	struct ScriptAffineLayerLinkCallbacks {
		// Class is borrowed from the genuine binding. Lookup checks registered native owner identity without side effects.
		SquirrelObjectIdentity NativeClass;
		std::function<ScriptAffineLayerLinks*(const SquirrelValue&)> FindOwner;
		std::function<void()> MarkMatrixDirty;
		std::function<void()> MarkSortDirty;
	};

	class ScriptAffineLayerLinks {
	public:
		// The VM, genuine instance and owner callbacks outlive this state. No self root is added.
		ScriptAffineLayerLinks(SquirrelVm& vm, const SquirrelValue& instance, ScriptAffineLayerLinkCallbacks callbacks);
		~ScriptAffineLayerLinks() = default;
		ScriptAffineLayerLinks(const ScriptAffineLayerLinks&) = delete;
		ScriptAffineLayerLinks& operator=(const ScriptAffineLayerLinks&) = delete;
		ScriptAffineLayerLinks(ScriptAffineLayerLinks&&) = delete;
		ScriptAffineLayerLinks& operator=(ScriptAffineLayerLinks&&) = delete;

		[[nodiscard]] const SquirrelValue& ParentLink() const;
		// Native Children is an owned array of strong instances, distinct from the weak parent link.
		[[nodiscard]] const SquirrelValue& Children() const;
		[[nodiscard]] ScriptAffineLayerLinks* ResolvedParent();
		void SetLink(const SquirrelValue& parent);
		void Unlink();
		void DetachChildren();
		void Invalidate();
		void AssignLinks(ScriptAffineLayerLinks& source);
		// The enclosing native owner invokes this before three-matrix/common Layer retirement.
		void Retire();
		[[nodiscard]] bool IsRetired() const noexcept;

	private:
		static constexpr std::size_t kMaxDepth = 256;
		static constexpr std::size_t kMaxChildren = 65536;
		static constexpr std::size_t kMaxTraversal = 65536;

		void RequireLive() const;
		[[nodiscard]] ScriptAffineLayerLinks* Resolve(const SquirrelValue& value);
		[[nodiscard]] SquirrelValue Self() const;
		[[nodiscard]] std::size_t ChildCount() const;
		[[nodiscard]] SquirrelValue ReadChild(std::size_t index);
		void RequireAcyclicParent(ScriptAffineLayerLinks* parent) const;
		void Invalidate(std::array<ScriptAffineLayerLinks*, kMaxDepth>& path, std::size_t depth, std::size_t& remaining);

		SquirrelVm& m_Vm;
		SquirrelObjectIdentity m_Instance;
		ScriptAffineLayerLinkCallbacks m_Callbacks;
		SquirrelHostRoot m_Parent;
		SquirrelHostRoot m_Children;
		bool m_Retired = false;
	};

} // namespace sgre::script
