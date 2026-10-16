#include "sgre/scene/motion_texture_binding.hpp"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sgre::scene::tests {
	static void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	static void RequireFailure(const std::function<void()>& operation) {
		try {
			operation();
		} catch (const std::runtime_error&) {
			return;
		}
		throw std::runtime_error("invalid texture binding scope accepted");
	}

	struct BindingEvent {
		std::uint32_t Owner = 0;
		std::uint32_t Slot = 0;
		MutableTextureStage Stage = MutableTextureStage::Pixel;
		bool operator==(const BindingEvent&) const = default;
	};

	class FixtureBackend;

	class FixtureTexture final : public MutableTextureResource {
	public:
		FixtureTexture(FixtureBackend& backend, MutableTextureDescription description, std::uint32_t owner) : Backend(backend), Value(description), Owner(owner) {
		}
		~FixtureTexture() override;
		MutableTextureDescription Description() const noexcept override {
			return Value;
		}
		FixtureBackend& Backend;
		MutableTextureDescription Value;
		std::uint32_t Owner;
	};

	class FixtureView final : public MutableTextureView {
	public:
		FixtureView(FixtureBackend& backend, MutableTextureViewDescription description, std::uint32_t owner) : Backend(backend), Value(description), Owner(owner) {
		}
		~FixtureView() override;
		MutableTextureViewDescription Description() const noexcept override {
			return Value;
		}
		FixtureBackend& Backend;
		MutableTextureViewDescription Value;
		std::uint32_t Owner;
	};

	class FixtureSampler final : public MutableTextureSampler {
	public:
		FixtureSampler(FixtureBackend& backend, MutableTextureSamplerDescription description) : Backend(backend), Value(description) {
		}
		~FixtureSampler() override;
		MutableTextureSamplerDescription Description() const noexcept override {
			return Value;
		}
		FixtureBackend& Backend;
		MutableTextureSamplerDescription Value;
	};

	class FixtureBackend final : public MutableTextureBackend {
	public:
		std::array<BindingEvent, 32> Events;
		std::array<MutableTextureView*, 3> DeviceViews{};
		std::size_t EventCount = 0;
		std::uint32_t Created = 0;
		std::uint32_t Live = 0;
		std::uint32_t Samplers = 0;
		std::uint32_t FailSlot = 3;

		std::unique_ptr<MutableTextureResource> CreateTexture(const MutableTextureDescription& description, const MutableTextureInitialData&) override {
			++Live;
			return std::make_unique<FixtureTexture>(*this, description, ++Created);
		}

		std::unique_ptr<MutableTextureView> CreateView(MutableTextureResource& texture, const MutableTextureViewDescription& description) override {
			++Live;
			return std::make_unique<FixtureView>(*this, description, dynamic_cast<FixtureTexture&>(texture).Owner);
		}

		std::unique_ptr<MutableTextureSampler> CreateSampler(const MutableTextureSamplerDescription& description) override {
			++Live;
			++Samplers;
			return std::make_unique<FixtureSampler>(*this, description);
		}

		MutableTextureMapping Map(MutableTextureResource&, MutableTextureMapMode) override {
			throw std::runtime_error("texture binding fixture unexpectedly mapped GPU-only storage");
		}

		void Unmap(MutableTextureResource&) override {
			throw std::runtime_error("texture binding fixture unexpectedly unmapped GPU-only storage");
		}

		void Upload(MutableTextureResource&, std::span<const std::byte>, std::uint32_t) override {
			throw std::runtime_error("texture binding fixture unexpectedly uploaded bytes");
		}

		void Copy(MutableTextureResource&, MutableTextureResource&) override {
			throw std::runtime_error("texture binding fixture unexpectedly copied bytes");
		}

		void Bind(MutableTextureView* view, MutableTextureSampler* sampler, std::uint32_t slot, MutableTextureStage stage) override {
			Require(view != nullptr && sampler != nullptr && slot < 3 && stage == MutableTextureStage::Pixel, "binding scope did not use its actual view/sampler and native pixel slot");
			Require(EventCount < Events.size(), "binding fixture exceeds bounded events");
			Events[EventCount++] = {dynamic_cast<FixtureView&>(*view).Owner, slot, stage};
			if (slot == FailSlot) {
				throw std::runtime_error("fixture device bind failed");
			}
			DeviceViews[slot] = view;
		}
	};

	FixtureTexture::~FixtureTexture() {
		--Backend.Live;
	}

	FixtureView::~FixtureView() {
		--Backend.Live;
	}

	FixtureSampler::~FixtureSampler() {
		--Backend.Live;
	}

	static MutableTextureSpec Spec() {
		MutableTextureSpec spec;
		spec.Storage = MutableTextureStorage::GpuOnly;
		spec.Description.Width = 1;
		spec.Description.Height = 1;
		return spec;
	}

	static void TestActualOwnerOverridesAndRestore() {
		FixtureBackend backend;
		{
			MutableTextureOwner primary(backend, Spec());
			MutableTextureOwner auxiliary(backend, Spec());
			MutableTextureOwner copied(backend, Spec());
			MotionTextureBindingState state;
			state.Textures = {&primary, nullptr, &primary};
			state.AuxiliaryTarget = &auxiliary;
			state.CopiedTarget = &copied;
			state.BlendMode = 0x100;
			const auto before_live = backend.Live;
			{
				MotionTextureBindingScope scope(state);
				Require(state.Textures == std::array<MutableTextureOwner*, 3>{&primary, &auxiliary, &copied} && backend.Live == before_live, "scope did not borrow the exact overriding owners");
				scope.Bind();
				Require(backend.EventCount == 3 && backend.Events[0] == BindingEvent{1, 0} && backend.Events[1] == BindingEvent{2, 1} && backend.Events[2] == BindingEvent{3, 2} && backend.Samplers == 3, "actual owner bindings lost native slot/stage order");
				RequireFailure([&] {
					scope.Bind();
				});
			}
			Require(state.Textures == std::array<MutableTextureOwner*, 3>{&primary, nullptr, &primary} && backend.Live == 9, "restoration changed owner lifetime or saved nullable entries");
			Require(backend.DeviceViews[2] != nullptr && dynamic_cast<FixtureView&>(*backend.DeviceViews[2]).Owner == 3, "CPU restoration unexpectedly unbound the physical slot2 resource");
		}
		Require(backend.Live == 0, "binding scope retained actual owners after their resource scope ended");
	}

	static void TestNullOverrideAndAliasedSlots() {
		FixtureBackend backend;
		{
			MutableTextureOwner texture(backend, Spec());
			MotionTextureBindingState state;
			state.Textures = {&texture, &texture, &texture};
			state.BlendMode = 0x10d;
			MotionTextureBindingScope scope(state);
			scope.Bind();
			Require(backend.EventCount == 3 && backend.Samplers == 1, "same owner across three slots was deduplicated or recreated its cached sampler");
			scope.Restore();
			Require(state.Textures == std::array<MutableTextureOwner*, 3>{&texture, &texture, nullptr}, "native family restoration without a copied override failed to clear slot2");
			Require(backend.DeviceViews[2] != nullptr, "null restoration cleared physical device bindings");
			const auto events = backend.EventCount;
			scope.Restore();
			Require(backend.EventCount == events, "idempotent restoration bound or released a resource");
			RequireFailure([&] {
				scope.Bind();
			});
		}
		Require(backend.Live == 0, "aliased slot fixture leaked texture/sampler ownership");
	}

	static void TestOutsideFamilyAndBindFailure() {
		FixtureBackend backend;
		{
			MutableTextureOwner original(backend, Spec());
			MutableTextureOwner copied(backend, Spec());
			MotionTextureBindingState state;
			state.Textures = {nullptr, nullptr, &original};
			state.CopiedTarget = &copied;
			state.BlendMode = 0x200;
			{
				MotionTextureBindingScope scope(state);
				scope.Bind();
				Require(backend.EventCount == 1 && backend.Events[0] == BindingEvent{1, 2}, "outside-family copied texture overrode the original slot or null entries were cleared");
			}
			Require(state.Textures[2] == &original, "outside-family restoration changed slot2");
			state.AuxiliaryTarget = &copied;
			state.BlendMode = 0x100;
			backend.FailSlot = 1;
			RequireFailure([&] {
				MotionTextureBindingScope scope(state);
				scope.Bind();
			});
			Require(state.Textures[1] == nullptr && state.Textures[2] == &original, "authored bind exception left temporary CPU overrides active");
		}
		Require(backend.Live == 0, "bind exception scope retained resource ownership");
	}

	static void RunTests() {
		TestActualOwnerOverridesAndRestore();
		TestNullOverrideAndAliasedSlots();
		TestOutsideFamilyAndBindFailure();
	}
} // namespace sgre::scene::tests

int main() {
	try {
		sgre::scene::tests::RunTests();
		std::cout << "Actual motion texture-binding three owner/scope groups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
