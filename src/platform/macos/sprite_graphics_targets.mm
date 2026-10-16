#include "sgre/render/sprite_graphics_targets.hpp"

#include "sprite_graphics_target_bridge.hpp"

#include "sgre/render/sprite_frame_encoder.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace sgre::render {
	class SpriteGraphicsTargets::Implementation {
	public:
		Implementation(SpriteGraphicsTargets& owner, SpritePassDescription description, const std::filesystem::path& shader_source, std::size_t maximum_targets)
		    : MaximumTargets(maximum_targets) {
			if (maximum_targets == 0 || maximum_targets > 1024) {
				throw std::runtime_error("graphics target registry requires a bounded nonempty capacity");
			}

			Lifetime = std::make_shared<LifetimeRecord>();
			Lifetime->Receiver = &owner;
			Targets.reserve(maximum_targets);
			Primary = std::make_shared<SpritePassRenderer>(description, shader_source);
			Targets.emplace_back(Primary);
		}

		~Implementation() {
			Lifetime->Receiver = nullptr;
		}

		struct LifetimeRecord {
			SpriteGraphicsTargets* Receiver = nullptr;
		};

		void RequireThread() const {
			if (std::this_thread::get_id() != Thread) {
				throw std::runtime_error("graphics target selection must remain on its actual serialized owner thread");
			}
		}

		std::shared_ptr<LifetimeRecord> Lifetime;
		std::shared_ptr<SpritePassRenderer> Primary;
		std::vector<std::weak_ptr<SpritePassRenderer>> Targets;
		std::weak_ptr<SpritePassRenderer> Current;
		std::size_t MaximumTargets;
		const std::thread::id Thread = std::this_thread::get_id();
		const SpritePassRenderer* CurrentIdentity = nullptr;
		scene::RenderTargetLayout Layout;
		GraphicsMaskControlState MaskControl;
		std::uint32_t MaskMode = 0;
	};

	SpriteGraphicsTargets::SpriteGraphicsTargets(SpritePassDescription primary_description, const std::filesystem::path& shader_source, std::size_t maximum_targets)
	    : m_Implementation(std::make_unique<Implementation>(*this, primary_description, shader_source, maximum_targets)) {
	}

	SpriteGraphicsTargets::~SpriteGraphicsTargets() = default;

	std::shared_ptr<SpritePassRenderer> SpriteGraphicsTargets::PrimaryTarget() const {
		m_Implementation->RequireThread();

		return m_Implementation->Primary;
	}

	std::shared_ptr<SpritePassRenderer> SpriteGraphicsTargets::AllocateTarget(SpritePassDescription description) {
		auto& state = *m_Implementation;
		state.RequireThread();
		SpriteGraphicsTargetBridge::RequireIdleEncoding(*state.Primary);
		const auto slot = std::find_if(state.Targets.begin(), state.Targets.end(), [](const auto& target) {
			return target.expired();
		});
		if (slot == state.Targets.end() && state.Targets.size() == state.MaximumTargets) {
			throw std::runtime_error("graphics target registry exceeds its allocation limit");
		}

		auto target = std::make_shared<SpritePassRenderer>(description, *state.Primary);
		if (slot == state.Targets.end()) {
			state.Targets.emplace_back(target);
		} else {
			*slot = target;
		}

		return target;
	}

	std::weak_ptr<SpritePassRenderer> SpriteGraphicsTargets::SelectTarget(const std::shared_ptr<SpritePassRenderer>& target) {
		auto& state = *m_Implementation;
		state.RequireThread();
		SpriteGraphicsTargetBridge::RequireIdleEncoding(*state.Primary);
		if (target && state.CurrentIdentity != nullptr && state.Current.expired()) {
			throw std::runtime_error("graphics must unbind its retired current target before selecting another allocation");
		}
		if (target) {
			const auto found = std::find_if(state.Targets.begin(), state.Targets.end(), [&target](const auto& registered) {
				return registered.lock() == target;
			});
			if (found == state.Targets.end()) {
				throw std::runtime_error("graphics selection requires an actual target allocated by this owner");
			}
		}

		const auto previous = state.Current;
		const auto previous_identity = state.CurrentIdentity;
		state.Current = target;
		state.CurrentIdentity = target.get();
		if (target) {
			if (target.get() != previous_identity) {
				SpriteGraphicsTargetBridge::ClearShaderTextures(*target);
			}
			const auto description = target->Description();
			state.Layout = scene::PrepareRenderTargetLayout(description.Width, description.Height, 1, 0);
		}

		return previous;
	}

	std::shared_ptr<SpritePassRenderer> SpriteGraphicsTargets::CurrentTarget() const {
		auto& state = *m_Implementation;
		state.RequireThread();
		auto target = state.Current.lock();
		if (!target || target.get() != state.CurrentIdentity) {
			throw std::runtime_error("graphics current target is absent or its allocation owner has retired");
		}

		return target;
	}

	std::shared_ptr<SpritePassRenderer> SpriteGraphicsTargets::RequireRegisteredTarget(const SpritePassRenderer& actual_target) const {
		auto& state = *m_Implementation;
		state.RequireThread();
		for (const auto& weak : state.Targets) {
			if (auto target = weak.lock(); target && target.get() == &actual_target) {
				return target;
			}
		}
		throw std::runtime_error("live graphics target requires its exact registered allocation owner");
	}

	void SpriteGraphicsTargets::SelectFrameTarget(SpriteFrameEncoder& actual_encoder, const std::shared_ptr<SpritePassRenderer>& actual_target, const std::optional<std::array<float, 4>>& clear_color) {
		auto& state = *m_Implementation;
		state.RequireThread();
		if (!actual_target || RequireRegisteredTarget(*actual_target) != actual_target || CurrentTarget().get() != &actual_encoder.CurrentRenderTarget()) {
			throw std::runtime_error("live graphics target selection requires its exact current encoder and registered target");
		}
		actual_encoder.SelectRenderTarget(*actual_target, clear_color);
		state.Current = actual_target;
		state.CurrentIdentity = actual_target.get();
		const auto description = actual_target->Description();
		state.Layout = scene::PrepareRenderTargetLayout(description.Width, description.Height, 1, 0);
	}

	scene::RenderTargetLayout SpriteGraphicsTargets::CurrentLayout() const {
		static_cast<void>(CurrentTarget());

		return m_Implementation->Layout;
	}

	void SpriteGraphicsTargets::ConfigureMaskControl(std::uint8_t operation, float threshold_minimum, float threshold_maximum) {
		auto& state = *m_Implementation;
		state.RequireThread();
		state.MaskControl.ThresholdMinimum = threshold_minimum;
		state.MaskControl.ThresholdMaximum = threshold_maximum;
		state.MaskControl.Operation = operation;
	}

	void SpriteGraphicsTargets::ConfigureOrdinaryMaskControl(float threshold_minimum, float threshold_maximum) {
		ConfigureMaskControl(0, threshold_minimum, threshold_maximum);
	}

	GraphicsMaskControlState SpriteGraphicsTargets::MaskControl() const {
		m_Implementation->RequireThread();

		return m_Implementation->MaskControl;
	}

	void SpriteGraphicsTargets::SetMaskMode(std::uint32_t mode) {
		auto& state = *m_Implementation;
		state.RequireThread();
		state.MaskMode = mode;
	}

	std::uint32_t SpriteGraphicsTargets::MaskMode() const {
		m_Implementation->RequireThread();

		return m_Implementation->MaskMode;
	}

	std::unique_ptr<scene::MutableTextureBackend> SpriteGraphicsTargets::CreateMutableTextureBackend() {
		auto& state = *m_Implementation;
		state.RequireThread();
		const std::weak_ptr<Implementation::LifetimeRecord> lifetime = state.Lifetime;
		auto available = [lifetime]() {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				return false;
			}
			live->Receiver->m_Implementation->RequireThread();

			return true;
		};
		auto source = [lifetime]() {
			const auto live = lifetime.lock();
			if (!live || live->Receiver == nullptr) {
				throw std::runtime_error("actual graphics source owner has retired");
			}

			return live->Receiver->CurrentTarget();
		};

		return SpriteGraphicsTargetBridge::CreateMutableTextureBackend(*state.Primary, std::move(source), std::move(available));
	}

	SpritePresentationSubmission SpriteGraphicsTargets::Submit(std::span<const SpritePassDraw> draws, const std::array<float, 16>& projection_registers, const std::array<float, 4>& clear_color, SpritePassLoadAction load_action) {
		const auto target = CurrentTarget();
		SpritePresentationSubmission result;
		result.Owner = target.get();
		result.Identity = target->Submit(draws, projection_registers, clear_color, load_action);

		return result;
	}
} // namespace sgre::render
