#include "sgre/scene/motion_static_source.hpp"

#include <stdexcept>
#include <utility>

namespace sgre::scene {

	static constexpr std::size_t kMaximumStaticSourceNameBytes = 65536;

	class StaticSourceBindingScope {
	public:
		explicit StaticSourceBindingScope(bool& active)
		    : m_Active(active) {
			if (active) {
				throw std::runtime_error("motion static source cannot reenter its observational texture lookup");
			}

			active = true;
		}

		~StaticSourceBindingScope() {
			m_Active = false;
		}

	private:
		bool& m_Active;
	};

	static std::string_view StaticSourceName(std::string_view text) {
		return text.substr(0, text.find('\0'));
	}

	MotionStaticSourceBinding::MotionStaticSourceBinding(const PreparedMotionSourceRegistry& registry, MotionProjectIdentity project, MotionStaticSourceServices services, const MotionExternalSourceCell* external)
	    : m_Registry(registry), m_Project(project), m_Services(std::move(services)), m_External(external) {
		if (project.DocumentInstance == 0 || !m_Services.FindTexture) {
			throw std::runtime_error("motion static source binding requires its actual project and registered texture service");
		}
	}

	bool MotionStaticSourceBinding::Bind(const PreparedMotionFrame& frame) {
		const StaticSourceBindingScope binding_scope(m_Binding);
		m_CurrentFrame = &frame;
		SynchronizeExternal();
		if (m_External != nullptr && m_External->Binding && m_External->Binding->Installed) {
			return false;
		}

		return BindAuthored(frame);
	}

	void MotionStaticSourceBinding::SynchronizeExternal() const {
		if (m_External == nullptr || m_ExternalRevision == m_External->Revision) {
			return;
		}
		if (m_External->Binding && m_External->Binding->Installed) {
			m_State = MotionStaticSourceState{};
			m_State.External = *m_External->Binding;
			m_State.Ready = true;
		} else if (m_State.External) {
			m_State.External->Installed = false;
			if (m_External->RestoreRequested && m_CurrentFrame != nullptr) {
				static_cast<void>(BindAuthored(*m_CurrentFrame));
			}
		}
		m_ExternalRevision = m_External->Revision;
	}

	bool MotionStaticSourceBinding::BindAuthored(const PreparedMotionFrame& frame) const {
		if (frame.Hidden) {
			return false;
		}

		const auto source = StaticSourceName(frame.Attributes.Source);
		const auto icon = StaticSourceName(frame.Attributes.Icon);
		if (source == m_State.Source && icon == m_State.Icon) {
			return false;
		}

		if (source.size() > kMaximumStaticSourceNameBytes || icon.size() > kMaximumStaticSourceNameBytes) {
			throw std::runtime_error("motion static source pair exceeds its prepared string bound");
		}

		m_State.Source = source;
		m_State.Icon = icon;
		m_State.Ready = false;
		m_State.Binding.reset();
		m_State.Texture.reset();
		m_State.Blank.reset();
		m_State.External.reset();
		if (source == "blank") {
			m_State.Blank = ReadMotionBlankSourceRectangle(icon);
			m_State.Ready = true;

			return true;
		}
		const auto binding = m_Registry.Resolve(source, icon, m_Project);
		if (!binding) {
			return true;
		}

		const auto texture = m_Services.FindTexture(*binding);
		if (!texture) {
			return true;
		}

		if (texture->Source != binding->Source || texture->Slot.Owner == nullptr || texture->Slot.Generation == 0 || texture->Texture.Owner == nullptr || texture->Texture.Generation == 0) {
			throw std::runtime_error("motion static source texture must carry the actual registration identities for its resolved source");
		}

		m_State.Binding = binding;
		m_State.Texture = texture;
		m_State.Ready = true;

		return true;
	}

	const MotionStaticSourceState& MotionStaticSourceBinding::State() const {
		if (m_Binding) {
			// The observational lookup may inspect its committed pending pair; external synchronization waits for the outer bind.
			return m_State;
		}
		const StaticSourceBindingScope binding_scope(m_Binding);
		SynchronizeExternal();

		return m_State;
	}

	std::optional<MotionBoundSpriteCommand> MotionStaticSourceBinding::BuildSprite(const PreparedMotionLayer& layer, const MotionHierarchyState& state, float depth_projection) const {
		const StaticSourceBindingScope binding_scope(m_Binding);
		SynchronizeExternal();
		if (m_State.External) {
			throw std::runtime_error("motion external source publication requires its actual selected geometry and negative-index frame consumer");
		}
		if (!m_State.Ready || m_State.Blank || state.Attributes.Hidden || (state.ModulationColor & 255) == 0) {
			return std::nullopt;
		}

		if (!m_State.Binding || !m_State.Texture) {
			throw std::runtime_error("motion static source ready state lacks its actual resource/texture binding");
		}

		if (layer.Type != 0 || layer.MeshTransform != 0) {
			throw std::runtime_error("motion static sprite publication requires ordinary undeformed icon geometry");
		}

		// A retained icon mesh does not activate deformation in this ordinary selected-node domain.
		const auto sprite = BuildMotionSpriteCommand(layer, state, *m_State.Binding, depth_projection, false);
		if (!sprite) {
			return std::nullopt;
		}

		MotionBoundSpriteCommand output;
		output.Sprite = *sprite;
		output.Slot = m_State.Texture->Slot;
		output.Texture = m_State.Texture->Texture;

		return output;
	}

} // namespace sgre::scene
