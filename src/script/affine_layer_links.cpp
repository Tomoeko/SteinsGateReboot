#include "sgre/script/affine_layer_links.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::script {

	ScriptAffineLayerLinks::ScriptAffineLayerLinks(SquirrelVm& vm, const SquirrelValue& instance, ScriptAffineLayerLinkCallbacks callbacks) : m_Vm(vm), m_Instance(vm.BorrowObjectIdentity(instance)), m_Callbacks(std::move(callbacks)), m_Parent(vm.Retain(SquirrelValue{})), m_Children(vm.Retain(vm.CreateArray())) {
		const auto native_class = vm.ResolveObjectIdentity(m_Callbacks.NativeClass);
		if (vm.Type(instance) != SquirrelValueType::Instance || !m_Callbacks.FindOwner || !native_class || vm.Type(*native_class) != SquirrelValueType::Class || !m_Callbacks.MarkMatrixDirty || !m_Callbacks.MarkSortDirty) {
			throw std::runtime_error("AffineLayer links require a live native instance and scoped owner callbacks");
		}
	}

	void ScriptAffineLayerLinks::RequireLive() const {
		if (m_Retired || m_Vm.IsClosed()) {
			throw std::runtime_error("AffineLayer link state has retired or its VM is closed");
		}
	}

	const SquirrelValue& ScriptAffineLayerLinks::ParentLink() const {
		RequireLive();

		return m_Parent.Value();
	}

	const SquirrelValue& ScriptAffineLayerLinks::Children() const {
		RequireLive();

		return m_Children.Value();
	}

	ScriptAffineLayerLinks* ScriptAffineLayerLinks::Resolve(const SquirrelValue& value) {
		const auto resolved = m_Vm.Type(value) == SquirrelValueType::WeakReference ? m_Vm.ResolveWeakReference(value) : value;

		if (m_Vm.Type(resolved) != SquirrelValueType::Instance) {
			m_Vm.SetLastErrorInContext(m_Vm.RootContext(), std::string("the object is not a class instance"));

			return nullptr;
		}

		if (auto* owner = m_Callbacks.FindOwner(resolved)) {
			return owner;
		}

		const auto native_class = m_Vm.ResolveObjectIdentity(m_Callbacks.NativeClass);
		if (!native_class || !m_Vm.IsInstanceOf(resolved, *native_class)) {
			m_Vm.SetLastErrorInContext(m_Vm.RootContext(), std::string("invalid type tag"));
		}

		return nullptr;
	}

	ScriptAffineLayerLinks* ScriptAffineLayerLinks::ResolvedParent() {
		RequireLive();

		return Resolve(m_Parent.Value());
	}

	SquirrelValue ScriptAffineLayerLinks::Self() const {
		const auto resolved = m_Vm.ResolveObjectIdentity(m_Instance);
		if (!resolved) {
			throw std::runtime_error("AffineLayer link operation lost its native instance owner");
		}

		return *resolved;
	}

	std::size_t ScriptAffineLayerLinks::ChildCount() const {
		const auto count = m_Vm.Length(m_Children.Value());
		if (count > kMaxChildren) {
			throw std::runtime_error("AffineLayer child array exceeds its traversal limit");
		}

		return count;
	}

	SquirrelValue ScriptAffineLayerLinks::ReadChild(std::size_t index) {
		if (index >= ChildCount()) {
			// The native live-index loop pushes null after failed sq_get, then continues type lookup.
			m_Vm.SetLastErrorInContext(m_Vm.RootContext(), std::string("the index doesn't exist"));

			return {};
		}

		const auto value = m_Vm.GetSlot(m_Children.Value(), static_cast<std::int32_t>(index));

		return m_Vm.Type(value) == SquirrelValueType::WeakReference ? m_Vm.ResolveWeakReference(value) : value;
	}

	void ScriptAffineLayerLinks::RequireAcyclicParent(ScriptAffineLayerLinks* parent) const {
		std::array<ScriptAffineLayerLinks*, kMaxDepth> path{};
		std::size_t count = 0;
		while (parent) {
			if (parent == this || count == path.size() || std::find(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(count), parent) != path.begin() + static_cast<std::ptrdiff_t>(count)) {
				throw std::runtime_error("AffineLayer proposed link contains a cycle or exceeds its depth limit");
			}

			path[count++] = parent;
			const auto& stored = parent->m_Parent.Value();
			const auto value = m_Vm.Type(stored) == SquirrelValueType::WeakReference ? m_Vm.ResolveWeakReference(stored) : stored;
			parent = m_Callbacks.FindOwner(value);
		}
	}

	void ScriptAffineLayerLinks::SetLink(const SquirrelValue& parent_value) {
		RequireLive();
		auto* parent = Resolve(parent_value);
		if (parent == Resolve(m_Parent.Value())) {
			return;
		}

		RequireAcyclicParent(parent);
		Unlink();
		if (parent) {
			m_Parent = {};
			const auto resolved = m_Vm.Type(parent_value) == SquirrelValueType::WeakReference ? m_Vm.ResolveWeakReference(parent_value) : parent_value;
			m_Parent = m_Vm.Retain(m_Vm.CreateWeakReference(resolved));
			if (parent->ChildCount() == kMaxChildren) {
				throw std::runtime_error("AffineLayer parent child array exceeds its allocation limit");
			}

			m_Vm.AppendArray(parent->m_Children.Value(), Self());
		}

		m_Callbacks.MarkSortDirty();
		Invalidate();
	}

	void ScriptAffineLayerLinks::Unlink() {
		RequireLive();
		auto* parent = Resolve(m_Parent.Value());
		if (!parent) {
			// Native returns before clearing an expired stored weak parent.
			return;
		}

		const auto self = Self();
		const auto count = parent->ChildCount();
		for (std::size_t index = 0; index < count; ++index) {
			if (m_Vm.CompareValues(parent->ReadChild(index), self) == 0) {
				static_cast<void>(m_Vm.RemoveArray(parent->m_Children.Value(), static_cast<std::int32_t>(index)));
				break;
			}
		}

		m_Parent = m_Vm.Retain(SquirrelValue{});
	}

	void ScriptAffineLayerLinks::DetachChildren() {
		RequireLive();
		const auto count = ChildCount();
		for (std::size_t index = 0; index < count; ++index) {
			auto child_root = m_Vm.Retain(ReadChild(index));
			if (auto* child = Resolve(child_root.Value())) {
				if (child->Resolve(child->m_Parent.Value()) == this) {
					child->m_Parent = m_Vm.Retain(SquirrelValue{});
				}
			}
		}

		m_Vm.ClearArray(m_Children.Value());
	}

	void ScriptAffineLayerLinks::Invalidate(std::array<ScriptAffineLayerLinks*, kMaxDepth>& path, std::size_t depth, std::size_t& remaining) {
		RequireLive();
		if (depth == path.size() || remaining == 0 || std::find(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(depth), this) != path.begin() + static_cast<std::ptrdiff_t>(depth)) {
			throw std::runtime_error("AffineLayer child invalidation contains a cycle or exceeds its work limit");
		}

		--remaining;
		path[depth] = this;
		m_Callbacks.MarkMatrixDirty();
		const auto count = ChildCount();
		for (std::size_t index = 0; index < count; ++index) {
			auto child_root = m_Vm.Retain(ReadChild(index));
			if (auto* child = Resolve(child_root.Value())) {
				child->Invalidate(path, depth + 1, remaining);
			}
		}
	}

	void ScriptAffineLayerLinks::Invalidate() {
		std::array<ScriptAffineLayerLinks*, kMaxDepth> path{};
		auto remaining = kMaxTraversal;
		Invalidate(path, 0, remaining);
	}

	void ScriptAffineLayerLinks::AssignLinks(ScriptAffineLayerLinks& source) {
		RequireLive();
		source.RequireLive();
		if (&m_Vm != &source.m_Vm) {
			throw std::runtime_error("AffineLayer link assignment cannot cross VM owners");
		}

		SetLink(source.m_Parent.Value());
		const auto count = source.ChildCount();
		for (std::size_t index = 0; index < count; ++index) {
			auto child_root = m_Vm.Retain(source.ReadChild(index));
			if (auto* child = Resolve(child_root.Value())) {
				// Keep the native live-index traversal: reparenting removes a source element.
				child->SetLink(Self());
			}
		}
	}

	void ScriptAffineLayerLinks::Retire() {
		if (m_Retired) {
			return;
		}

		if (!m_Vm.IsClosed()) {
			Unlink();
			DetachChildren();
		}

		m_Retired = true;
		m_Children = {};
		m_Parent = {};
	}

	bool ScriptAffineLayerLinks::IsRetired() const noexcept {
		return m_Retired;
	}

} // namespace sgre::script
