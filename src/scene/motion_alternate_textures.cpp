#include "sgre/scene/motion_alternate_textures.hpp"

#include "sgre/scene/motion_player_commands.hpp"
#include "sgre/scene/motion_selected_layer_nodes.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static std::string_view AlternateUid(std::string_view uid) {
		if (uid.size() > kMaximumMotionCommandNameBytes) {
			throw std::runtime_error("motion alternate texture UID exceeds its native string bound");
		}

		return uid.substr(0, uid.find('\0'));
	}

	static std::int32_t AlternateRectangleInteger(float value) {
		if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f) {
			throw std::runtime_error("motion alternate texture rectangle exceeds the native signed conversion domain");
		}

		return static_cast<std::int32_t>(value);
	}

	class AlternateMutation {
	public:
		explicit AlternateMutation(bool& active) : m_Active(active) {
			if (active) {
				throw std::runtime_error("motion alternate texture mutation cannot reenter its owner");
			}
			active = true;
		}
		~AlternateMutation() {
			m_Active = false;
		}

	private:
		bool& m_Active;
	};

	class MotionAlternateTextures::Listener final : public MotionLayerUidObserver {
	public:
		Listener(MotionSharedTextures& textures, MotionExternalSourceBinding binding) : Textures(textures), Binding(binding) {
		}
		void LayerAdded(std::string_view, const MotionLayerUidMatch& match) noexcept override {
			Install(match, Binding, Textures);
		}
		void LayerRemoved(std::string_view, const MotionLayerUidMatch& match) noexcept override {
			Remove(match, Restore, Textures);
		}
		MotionSharedTextures& Textures;
		MotionExternalSourceBinding Binding;
		bool Restore = true;
	};

	MotionAlternateTextures::MotionAlternateTextures(const MotionPlayerCommands& commands, std::function<void()> require_unqueued, std::size_t maximum_listeners) : m_Uids(commands.LayerUids()), m_Textures(commands.Manager().ExternalTextures()), m_RequireUnqueued(std::move(require_unqueued)), m_Maximum(maximum_listeners) {
		if (&m_Uids.UltimatePlayer() != &commands || m_Uids.m_AlternateTextures != nullptr || !m_RequireUnqueued || maximum_listeners == 0 || maximum_listeners > 65536) {
			throw std::runtime_error("motion alternate textures require their exclusive actual ultimate player and queue lifetime service");
		}
		m_Uids.m_AlternateTextures = this;
	}

	MotionAlternateTextures::~MotionAlternateTextures() noexcept {
		try {
			Clear();
		} catch (...) {
			std::terminate();
		}
		m_Uids.m_AlternateTextures = nullptr;
	}

	void MotionAlternateTextures::Install(const MotionLayerUidMatch& match, const MotionExternalSourceBinding& binding, MotionSharedTextures& textures) noexcept {
		const auto* selected = match.Layer.m_Selected;
		const auto ordinal = match.Layer.m_NativeOrdinal;
		if (selected == nullptr || ordinal == 0 || ordinal > selected->m_ExternalSources.size() || binding.Reference == nullptr) {
			std::terminate();
		}
		auto& cell = selected->m_ExternalSources[ordinal - 1];
		if (cell.Binding && cell.Binding->Installed) {
			textures.ReleaseReservedExternal(cell.Binding->Slot);
		}
		auto installed = binding;
		installed.Slot = textures.RegisterReservedExternal(*binding.Reference);
		installed.Installed = true;
		cell.Binding = installed;
		if (cell.Revision == std::numeric_limits<std::uint64_t>::max()) {
			std::terminate();
		}
		++cell.Revision;
		cell.RestoreRequested = false;
		cell.Dirty = true;
	}

	void MotionAlternateTextures::Remove(const MotionLayerUidMatch& match, bool restore, MotionSharedTextures& textures) noexcept {
		const auto* selected = match.Layer.m_Selected;
		const auto ordinal = match.Layer.m_NativeOrdinal;
		if (selected == nullptr || ordinal == 0 || ordinal > selected->m_ExternalSources.size()) {
			std::terminate();
		}
		auto& cell = selected->m_ExternalSources[ordinal - 1];
		if (!cell.Binding || !cell.Binding->Installed || cell.Revision == std::numeric_limits<std::uint64_t>::max()) {
			std::terminate();
		}
		textures.ReleaseReservedExternal(cell.Binding->Slot);
		cell.Binding->Installed = false;
		++cell.Revision;
		cell.RestoreRequested = restore;
		cell.Dirty = cell.Dirty || restore;
	}

	void MotionAlternateTextures::RemoveListener(std::map<std::string, std::unique_ptr<Listener>, std::less<>>::iterator position, bool restore) {
		auto& listener = *position->second;
		listener.Restore = restore;
		m_Uids.Unsubscribe(position->first, listener);
		m_Textures.UnreserveExternal(*listener.Binding.Reference);
		m_Listeners.erase(position);
	}

	void MotionAlternateTextures::Register(std::string_view uid, const NativeTextureReference& reference, const MotionBounds& rectangle, resource::MotionPoint origin) {
		const AlternateMutation mutation(m_Mutating);
		m_RequireUnqueued();
		uid = AlternateUid(uid);
		static_cast<void>(reference.Read());
		MotionExternalSourceBinding binding;
		binding.Reference = &reference;
		binding.Table = &m_Textures;
		binding.Left = AlternateRectangleInteger(rectangle.MinimumX);
		binding.Top = AlternateRectangleInteger(rectangle.MinimumY);
		binding.Width = AlternateRectangleInteger(rectangle.MaximumX - rectangle.MinimumX);
		binding.Height = AlternateRectangleInteger(rectangle.MaximumY - rectangle.MinimumY);
		binding.Origin = origin;
		if (binding.Width <= 0 || binding.Height <= 0 || !std::isfinite(origin.X) || !std::isfinite(origin.Y)) {
			throw std::runtime_error("motion alternate texture requires a finite positive source rectangle and origin");
		}
		const auto previous = m_Listeners.find(uid);
		if (previous == m_Listeners.end() && m_Listeners.size() == m_Maximum) {
			throw std::runtime_error("motion alternate texture listener capacity exhausted");
		}
		auto replacement = std::make_unique<Listener>(m_Textures, binding);
		// Allocate the map node and reservation before native replacement clears the old listener without restoration.
		std::map<std::string, std::unique_ptr<Listener>, std::less<>> prepared;
		prepared.emplace(std::string(uid), std::move(replacement));
		m_Textures.ReserveExternal(reference);
		try {
			if (previous != m_Listeners.end()) {
				RemoveListener(previous, false);
			}
			auto node = prepared.extract(prepared.begin());
			auto result = m_Listeners.insert(std::move(node));
			try {
				m_Uids.Subscribe(result.position->first, *result.position->second);
			} catch (...) {
				m_Listeners.erase(result.position);
				throw;
			}
		} catch (...) {
			m_Textures.UnreserveExternal(reference);
			throw;
		}
	}

	bool MotionAlternateTextures::Unregister(std::string_view uid) {
		const AlternateMutation mutation(m_Mutating);
		m_RequireUnqueued();
		const auto found = m_Listeners.find(AlternateUid(uid));
		if (found == m_Listeners.end()) {
			return false;
		}
		RemoveListener(found, true);

		return true;
	}

	void MotionAlternateTextures::Clear() {
		const AlternateMutation mutation(m_Mutating);
		if (!m_Listeners.empty()) {
			m_RequireUnqueued();
		}
		while (!m_Listeners.empty()) {
			RemoveListener(m_Listeners.begin(), true);
		}
	}

	std::size_t MotionAlternateTextures::Count() const noexcept {
		return m_Listeners.size();
	}

} // namespace sgre::scene
