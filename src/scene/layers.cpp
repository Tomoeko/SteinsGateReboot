#include "sgre/scene/layers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sgre::scene {

	static constexpr std::uint32_t kNoLayer = std::numeric_limits<std::uint32_t>::max();

	struct LayerManagerState {
		std::uint32_t First = kNoLayer;
		std::uint32_t Last = kNoLayer;
		std::size_t Count = 0;
		bool Dirty = false;
		bool Traversing = false;
		std::uint32_t Current = kNoLayer;
	};

	struct LayerRecord {
		LayerState State;
		LayerManagerState Children;
		std::uint64_t Generation = 1;
		std::uint32_t Previous = kNoLayer;
		std::uint32_t Next = kNoLayer;
		bool Active = false;
	};

	struct LayerScreenRootRecord {
		LayerManagerState Children;
		std::uint64_t Generation = 1;
		bool Active = false;
	};

	static void RequireFinite(float value) {
		if (!std::isfinite(value)) {
			throw std::runtime_error("layer state requires finite coordinates and priorities");
		}
	}

	static void RequireFinite(LayerPoint value) {
		RequireFinite(value.X);
		RequireFinite(value.Y);
	}

	static LayerClip MakeClip(LayerPoint origin, LayerPoint extent) {
		RequireFinite(origin);
		RequireFinite(extent);
		LayerClip clip;
		clip.Left = origin.X;
		clip.Top = origin.Y;
		clip.Right = origin.X + extent.X;
		clip.Bottom = origin.Y + extent.Y;
		RequireFinite(clip.Right);
		RequireFinite(clip.Bottom);

		return clip;
	}

	class LayerTree::Implementation {
	public:
		explicit Implementation(std::size_t max_layers) {
			if (max_layers == 0 || max_layers > 65536) {
				throw std::runtime_error("layer tree requires a bounded positive capacity");
			}

			Layers.resize(max_layers);
			Order.resize(max_layers);
			Scratch.resize(max_layers);
			ScreenRoots.resize(64);
		}

		std::vector<LayerRecord> Layers;
		std::vector<std::uint32_t> Order;
		std::vector<std::uint32_t> Scratch;
		std::vector<LayerScreenRootRecord> ScreenRoots;
		LayerManagerState Root;

		bool Contains(LayerHandle handle) const {
			return handle.Owner == this && handle.Index < Layers.size() && Layers[handle.Index].Active && Layers[handle.Index].Generation == handle.Generation;
		}

		LayerRecord& Record(LayerHandle handle) {
			if (!Contains(handle)) {
				throw std::runtime_error("layer handle is stale or belongs to another tree");
			}

			return Layers[handle.Index];
		}

		const LayerRecord& Record(LayerHandle handle) const {
			if (!Contains(handle)) {
				throw std::runtime_error("layer handle is stale or belongs to another tree");
			}

			return Layers[handle.Index];
		}

		LayerHandle Handle(std::uint32_t index) const {
			return {index, Layers[index].Generation, this};
		}

		bool ContainsScreenRoot(LayerScreenRootHandle handle) const {
			return handle.Owner == this && handle.Index < ScreenRoots.size() && ScreenRoots[handle.Index].Active && ScreenRoots[handle.Index].Generation == handle.Generation;
		}

		LayerScreenRootRecord& ScreenRoot(LayerScreenRootHandle handle) {
			if (!ContainsScreenRoot(handle)) {
				throw std::runtime_error("layer screen root is stale or belongs to another tree");
			}

			return ScreenRoots[handle.Index];
		}

		const LayerScreenRootRecord& ScreenRoot(LayerScreenRootHandle handle) const {
			if (!ContainsScreenRoot(handle)) {
				throw std::runtime_error("layer screen root is stale or belongs to another tree");
			}

			return ScreenRoots[handle.Index];
		}

		LayerManagerState& Manager(std::optional<LayerHandle> owner, std::optional<LayerScreenRootHandle> screen = {}) {
			if (owner && Record(*owner).State.Kind != LayerKind::Folder) {
				throw std::runtime_error("layer owner must provide a folder child manager");
			}

			return owner ? Record(*owner).Children : screen ? ScreenRoot(*screen).Children
			                                                : Root;
		}

		const LayerManagerState& Manager(std::optional<LayerHandle> owner, std::optional<LayerScreenRootHandle> screen = {}) const {
			if (owner && Record(*owner).State.Kind != LayerKind::Folder) {
				throw std::runtime_error("layer owner must provide a folder child manager");
			}

			return owner ? Record(*owner).Children : screen ? ScreenRoot(*screen).Children
			                                                : Root;
		}

		void RequireCanDetach(LayerHandle handle) const {
			const auto& record = Record(handle);
			if (record.State.Attached && Manager(record.State.Owner, record.State.ScreenOwner).Current == handle.Index) {
				throw std::runtime_error("native Layer manager cannot unlink its current traversal member");
			}
		}

		void RequireCanRelease(LayerHandle handle) const {
			RequireCanDetach(handle);
			const auto& record = Record(handle);
			if (record.Generation == std::numeric_limits<std::uint64_t>::max()) {
				throw std::runtime_error("layer generation counter exhausted");
			}
			if (record.Children.Traversing) {
				throw std::runtime_error("native Layer manager owner cannot retire during child traversal");
			}
		}

		static void RequireCanClear(const LayerManagerState& manager) {
			if (manager.Traversing) {
				throw std::runtime_error("native Layer manager cannot clear during child traversal");
			}
		}

		class Traversal {
		public:
			explicit Traversal(LayerManagerState& manager)
			    : m_Manager(manager) {
				if (manager.Traversing) {
					throw std::runtime_error("native Layer manager traversal cannot reenter the same owner");
				}

				m_Manager.Traversing = true;
			}

			~Traversal() {
				m_Manager.Current = kNoLayer;
				m_Manager.Traversing = false;
			}

		private:
			LayerManagerState& m_Manager;
		};

		void Visit(LayerManagerState& manager, const std::function<void(LayerHandle)>& visit, std::size_t max_visits) {
			if (!visit || max_visits == 0 || max_visits > 65536) {
				throw std::runtime_error("native Layer manager requires its concrete visitor and a bounded positive visit limit");
			}

			const Traversal traversal(manager);
			std::size_t visits = 0;
			for (auto index = manager.First; index != kNoLayer; index = Layers[index].Next) {
				if (visits == max_visits) {
					throw std::runtime_error("native Layer manager traversal visit limit exceeded");
				}

				++visits;
				manager.Current = index;
				visit(Handle(index));
			}
		}

		void Detach(LayerHandle handle) {
			RequireCanDetach(handle);
			auto& record = Record(handle);
			if (!record.State.Attached) {
				return;
			}

			auto& manager = Manager(record.State.Owner, record.State.ScreenOwner);
			if (record.Previous == kNoLayer) {
				manager.First = record.Next;
			} else {
				Layers[record.Previous].Next = record.Next;
			}

			if (record.Next == kNoLayer) {
				manager.Last = record.Previous;
			} else {
				Layers[record.Next].Previous = record.Previous;
			}

			--manager.Count;
			manager.Dirty = true;
			record.Previous = kNoLayer;
			record.Next = kNoLayer;
			record.State.Attached = false;
		}

		void Attach(LayerHandle handle, std::optional<LayerHandle> owner, std::optional<LayerScreenRootHandle> screen = {}) {
			auto& record = Record(handle);
			auto& manager = Manager(owner, screen);
			record.State.Owner = owner;
			record.State.ScreenOwner = screen;
			record.State.Attached = true;
			record.Previous = manager.Last;
			if (manager.Last == kNoLayer) {
				manager.First = handle.Index;
			} else {
				Layers[manager.Last].Next = handle.Index;
			}

			manager.Last = handle.Index;
			++manager.Count;
			manager.Dirty = true;
		}

		LayerHandle Create(LayerKind kind, std::optional<LayerHandle> owner, std::optional<LayerScreenRootHandle> screen = {}) {
			static_cast<void>(Manager(owner, screen));
			for (std::uint32_t index = 0; index < Layers.size(); ++index) {
				auto& record = Layers[index];
				if (record.Active) {
					continue;
				}

				record.State = {};
				record.State.Kind = kind;
				record.State.Attached = false;
				record.Active = true;
				const auto handle = Handle(index);
				Attach(handle, owner, screen);

				return handle;
			}

			throw std::runtime_error("layer tree capacity exceeded");
		}

		void Clear(LayerManagerState& manager) {
			RequireCanClear(manager);
			while (manager.First != kNoLayer) {
				const auto child = Handle(manager.First);
				Detach(child);
				auto& record = Record(child);
				record.State.Owner.reset();
				record.State.ScreenOwner.reset();
			}
		}

		void Prepare(LayerManagerState& manager) {
			if (manager.Traversing) {
				throw std::runtime_error("native Layer manager cannot sort during child traversal");
			}

			if (!manager.Dirty) {
				return;
			}

			std::size_t count = 0;
			for (auto index = manager.First; index != kNoLayer; index = Layers[index].Next) {
				Order[count++] = index;
			}

			for (std::size_t width = 1; width < count; width *= 2) {
				for (std::size_t begin = 0; begin < count; begin += width * 2) {
					const auto middle = std::min(begin + width, count);
					const auto end = std::min(begin + width * 2, count);
					auto left = begin;
					auto right = middle;
					for (auto output = begin; output < end; ++output) {
						if (right < end && (left == middle || Layers[Order[right]].State.Priority < Layers[Order[left]].State.Priority)) {
							Scratch[output] = Order[right++];
						} else {
							Scratch[output] = Order[left++];
						}
					}
				}

				Order.swap(Scratch);
			}

			for (std::size_t index = 0; index < count; ++index) {
				auto& record = Layers[Order[index]];
				record.Previous = index == 0 ? kNoLayer : Order[index - 1];
				record.Next = index + 1 == count ? kNoLayer : Order[index + 1];
			}

			manager.First = count == 0 ? kNoLayer : Order[0];
			manager.Last = count == 0 ? kNoLayer : Order[count - 1];
			manager.Dirty = false;
		}

		std::size_t Write(const LayerManagerState& manager, std::span<LayerHandle> output) const {
			if (output.size() < manager.Count) {
				throw std::runtime_error("layer child output is too small");
			}

			std::size_t count = 0;
			for (auto index = manager.First; index != kNoLayer; index = Layers[index].Next) {
				output[count++] = Handle(index);
			}

			return count;
		}
	};

	LayerTree::LayerTree(std::size_t max_layers)
	    : m_Implementation(std::make_unique<Implementation>(max_layers)) {
	}

	LayerTree::~LayerTree() = default;

	LayerHandle LayerTree::CreateBase(std::optional<LayerHandle> owner) {
		return m_Implementation->Create(LayerKind::Base, owner);
	}

	LayerHandle LayerTree::CreateBaseInScreen(LayerScreenRootHandle screen) {
		return m_Implementation->Create(LayerKind::Base, {}, screen);
	}

	LayerHandle LayerTree::CreateFolder(std::optional<LayerHandle> owner) {
		return m_Implementation->Create(LayerKind::Folder, owner);
	}

	LayerHandle LayerTree::CreateFolderInScreen(LayerScreenRootHandle screen) {
		return m_Implementation->Create(LayerKind::Folder, {}, screen);
	}

	LayerHandle LayerTree::CreateMotion(std::optional<LayerHandle> owner) {
		return m_Implementation->Create(LayerKind::Motion, owner);
	}

	LayerHandle LayerTree::CreateMotionInScreen(LayerScreenRootHandle screen) {
		return m_Implementation->Create(LayerKind::Motion, {}, screen);
	}

	LayerHandle LayerTree::CreateRawTexture(std::optional<LayerHandle> owner) {
		return m_Implementation->Create(LayerKind::RawTexture, owner);
	}

	LayerHandle LayerTree::CreateRawTextureInScreen(LayerScreenRootHandle screen) {
		return m_Implementation->Create(LayerKind::RawTexture, {}, screen);
	}

	LayerScreenRootHandle LayerTree::CreateScreenRoot() {
		auto& state = *m_Implementation;
		for (std::uint32_t index = 0; index < state.ScreenRoots.size(); ++index) {
			auto& record = state.ScreenRoots[index];
			if (record.Active) {
				continue;
			}

			record.Children = {};
			record.Active = true;

			return {index, record.Generation, &state};
		}

		throw std::runtime_error("layer screen root capacity exceeded");
	}

	bool LayerTree::ContainsScreenRoot(LayerScreenRootHandle screen) const {
		return m_Implementation->ContainsScreenRoot(screen);
	}

	void LayerTree::ClearScreenRoot(LayerScreenRootHandle screen) {
		m_Implementation->Clear(m_Implementation->ScreenRoot(screen).Children);
	}

	void LayerTree::RequireCanReleaseScreenRoot(LayerScreenRootHandle screen) const {
		const auto& record = m_Implementation->ScreenRoot(screen);
		if (record.Generation == std::numeric_limits<std::uint64_t>::max()) {
			throw std::runtime_error("layer screen root generation exhausted");
		}

		Implementation::RequireCanClear(record.Children);
	}

	void LayerTree::ReleaseScreenRoot(LayerScreenRootHandle screen) {
		RequireCanReleaseScreenRoot(screen);
		auto& state = *m_Implementation;
		auto& record = state.ScreenRoot(screen);
		state.Clear(record.Children);
		record.Active = false;
		++record.Generation;
	}

	void LayerTree::SetScreenOwner(LayerHandle layer, LayerScreenRootHandle screen) {
		auto& state = *m_Implementation;
		static_cast<void>(state.Record(layer));
		static_cast<void>(state.ScreenRoot(screen));
		state.Detach(layer);
		state.Attach(layer, {}, screen);
	}

	std::size_t LayerTree::ScreenChildCount(LayerScreenRootHandle screen) const {
		return m_Implementation->ScreenRoot(screen).Children.Count;
	}

	void LayerTree::RequireCanClearScreenChildren(LayerScreenRootHandle screen) const {
		Implementation::RequireCanClear(m_Implementation->ScreenRoot(screen).Children);
	}

	void LayerTree::VisitScreenChildren(LayerScreenRootHandle screen, const std::function<void(LayerHandle)>& visit, std::size_t max_visits) {
		m_Implementation->Visit(m_Implementation->ScreenRoot(screen).Children, visit, max_visits);
	}

	void LayerTree::PrepareScreenChildren(LayerScreenRootHandle screen) {
		m_Implementation->Prepare(m_Implementation->ScreenRoot(screen).Children);
	}

	std::size_t LayerTree::WriteScreenChildren(LayerScreenRootHandle screen, std::span<LayerHandle> output) const {
		return m_Implementation->Write(m_Implementation->ScreenRoot(screen).Children, output);
	}

	bool LayerTree::Contains(LayerHandle layer) const {
		return m_Implementation->Contains(layer);
	}

	LayerState LayerTree::State(LayerHandle layer) const {
		return m_Implementation->Record(layer).State;
	}

	LayerAffineTransform LayerTree::WorldTransform(LayerHandle layer) const {
		const auto& state = *m_Implementation;
		const auto& record = state.Record(layer);
		auto result = PrepareLayerLocalTransform(record.State.Transform);
		for (auto ancestor = record.State.Owner; ancestor; ancestor = state.Record(*ancestor).State.Owner) {
			result = ComposeLayerTransforms(result, PrepareLayerLocalTransform(state.Record(*ancestor).State.Transform));
		}

		return result;
	}

	void LayerTree::SetOwner(LayerHandle layer, std::optional<LayerHandle> owner) {
		auto& state = *m_Implementation;
		static_cast<void>(state.Record(layer));
		static_cast<void>(state.Manager(owner));
		for (auto ancestor = owner; ancestor; ancestor = state.Record(*ancestor).State.Owner) {
			if (*ancestor == layer) {
				throw std::runtime_error("layer ownership cycle rejected");
			}
		}

		state.Detach(layer);
		state.Attach(layer, owner);
	}

	void LayerTree::Clear(LayerHandle folder) {
		auto& state = *m_Implementation;
		state.Clear(state.Manager(folder));
	}

	void LayerTree::ClearRoot() {
		auto& state = *m_Implementation;
		state.Clear(state.Manager({}));
	}

	void LayerTree::Detach(LayerHandle layer) {
		auto& state = *m_Implementation;
		state.Detach(layer);
		auto& record = state.Record(layer);
		record.State.Owner.reset();
		record.State.ScreenOwner.reset();
	}

	void LayerTree::Release(LayerHandle layer) {
		auto& state = *m_Implementation;
		state.RequireCanRelease(layer);
		auto& record = state.Record(layer);
		if (record.State.Kind == LayerKind::Folder) {
			state.Clear(record.Children);
		}
		state.Detach(layer);
		record.State = {};
		record.Active = false;
		++record.Generation;
	}

	void LayerTree::RequireCanRelease(LayerHandle layer) const {
		m_Implementation->RequireCanRelease(layer);
	}

	void LayerTree::RequireCanClearChildren(std::optional<LayerHandle> owner) const {
		Implementation::RequireCanClear(m_Implementation->Manager(owner));
	}

	void LayerTree::VisitChildren(std::optional<LayerHandle> owner, const std::function<void(LayerHandle)>& visit, std::size_t max_visits) {
		m_Implementation->Visit(m_Implementation->Manager(owner), visit, max_visits);
	}

	std::size_t LayerTree::ChildCount(std::optional<LayerHandle> owner) const {
		return m_Implementation->Manager(owner).Count;
	}

	void LayerTree::PrepareChildren(std::optional<LayerHandle> owner) {
		m_Implementation->Prepare(m_Implementation->Manager(owner));
	}

	std::size_t LayerTree::WriteChildren(std::optional<LayerHandle> owner, std::span<LayerHandle> output) const {
		return m_Implementation->Write(m_Implementation->Manager(owner), output);
	}

	void LayerTree::SetPriority(LayerHandle layer, float priority) {
		RequireFinite(priority);
		auto& state = *m_Implementation;
		auto& record = state.Record(layer);
		record.State.Priority = priority;
		if (record.State.Attached) {
			state.Manager(record.State.Owner, record.State.ScreenOwner).Dirty = true;
		}
	}

	void LayerTree::SetVisible(LayerHandle layer, bool visible) {
		m_Implementation->Record(layer).State.Visible = visible;
	}

	void LayerTree::SetSmoothing(LayerHandle layer, bool smoothing) {
		m_Implementation->Record(layer).State.Smoothing = smoothing;
	}

	void LayerTree::SetOffset(LayerHandle layer, LayerPoint offset) {
		RequireFinite(offset);
		m_Implementation->Record(layer).State.Transform.Offset = offset;
	}

	void LayerTree::SetCenter(LayerHandle layer, LayerPoint center) {
		RequireFinite(center);
		m_Implementation->Record(layer).State.Transform.Center = center;
	}

	void LayerTree::SetScale(LayerHandle layer, LayerPoint scale) {
		RequireFinite(scale);
		m_Implementation->Record(layer).State.Transform.Scale = scale;
	}

	void LayerTree::SetZoom(LayerHandle layer, float zoom) {
		SetScale(layer, {zoom, zoom});
	}

	void LayerTree::SetAngleRadians(LayerHandle layer, float radians) {
		RequireFinite(radians);
		m_Implementation->Record(layer).State.Transform.AngleRadians = radians;
	}

	void LayerTree::SetAngleDegrees(LayerHandle layer, float degrees) {
		SetAngleRadians(layer, degrees * 0.0174532924f);
	}

	float LayerTree::AngleDegrees(LayerHandle layer) const {
		return m_Implementation->Record(layer).State.Transform.AngleRadians * 57.2957802f;
	}

	void LayerTree::SetBaseClip(LayerHandle layer, LayerPoint origin, LayerPoint extent) {
		m_Implementation->Record(layer).State.BaseClip = MakeClip(origin, extent);
	}

	void LayerTree::ResetBaseClip(LayerHandle layer) {
		m_Implementation->Record(layer).State.BaseClip = {};
	}

	void LayerTree::SetClip(LayerHandle layer, LayerPoint origin, LayerPoint extent) {
		m_Implementation->Record(layer).State.Clip = MakeClip(origin, extent);
	}

	void LayerTree::ResetClip(LayerHandle layer) {
		m_Implementation->Record(layer).State.Clip = {};
	}

} // namespace sgre::scene
