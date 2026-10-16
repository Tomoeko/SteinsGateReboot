#include "sgre/scene/motion_copied_target_owner.hpp"

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

	static void RequireFailure(const std::function<void()>& operation, std::string_view context) {
		try {
			operation();
		} catch (const std::runtime_error& error) {
			Require(std::string_view(error.what()).find(context) != std::string_view::npos, "copied target failure omitted context");
			return;
		}

		throw std::runtime_error("invalid copied target operation accepted");
	}

	struct FixtureEvent {
		std::string_view Action;
		std::uint32_t Owner = 0;
		std::uint32_t Extra = 0;
		bool operator==(const FixtureEvent&) const = default;
	};

	struct FixtureAllocation {
		MutableTextureDescription Description;
		std::uint32_t Owner = 0;
	};

	class FixtureBackend;

	class FixtureTexture final : public MutableTextureResource {
	public:
		FixtureTexture(FixtureBackend& backend, std::shared_ptr<FixtureAllocation> allocation) : Backend(backend), Allocation(std::move(allocation)) {
		}
		~FixtureTexture() override;
		MutableTextureDescription Description() const noexcept override;
		FixtureBackend& Backend;
		std::shared_ptr<FixtureAllocation> Allocation;
	};

	class FixtureShaderView final : public MutableTextureView {
	public:
		FixtureShaderView(FixtureBackend& backend, std::shared_ptr<FixtureAllocation> allocation, const MutableTextureViewDescription& description) : Backend(backend), Allocation(std::move(allocation)), Value(description) {
		}
		~FixtureShaderView() override;
		MutableTextureViewDescription Description() const noexcept override {
			return Value;
		}
		FixtureBackend& Backend;
		std::shared_ptr<FixtureAllocation> Allocation;
		MutableTextureViewDescription Value;
	};

	class FixtureTargetView final : public MotionCopiedTargetView {
	public:
		FixtureTargetView(FixtureBackend& backend, std::shared_ptr<FixtureAllocation> allocation) : Backend(backend), Allocation(std::move(allocation)) {
		}
		~FixtureTargetView() override;
		FixtureBackend& Backend;
		std::shared_ptr<FixtureAllocation> Allocation;
	};

	class FixtureBackend final : public MutableTextureBackend, public MotionCopiedTargetBackend {
	public:
		std::array<FixtureEvent, 256> Events;
		std::array<MutableTextureDescription, 16> Descriptions;
		std::array<std::weak_ptr<FixtureAllocation>, 16> Allocations;
		std::size_t EventCount = 0;
		std::uint32_t Created = 0;
		std::uint32_t Live = 0;
		bool FailRenderView = false;
		bool ThrowRenderView = false;
		bool FailTexture = false;
		bool DescriptionMismatch = false;

		void Record(FixtureEvent event) noexcept {
			if (EventCount >= Events.size()) {
				std::terminate();
			}
			Events[EventCount++] = event;
		}

		std::span<const FixtureEvent> Recorded() const noexcept {
			return std::span(Events).first(EventCount);
		}

		std::unique_ptr<MutableTextureResource> CreateTexture(const MutableTextureDescription& description, const MutableTextureInitialData& initial) override {
			const auto identity = ++Created;
			Require(identity <= Descriptions.size() && initial.Bytes.empty(), "copied target fixture creation exceeds its bounded domain");
			Require(description.RenderTarget && description.ShaderResource && description.MipLevels == 1 && description.SampleCount == 1 && !description.CpuRead && !description.CpuWrite && !description.DepthStencil && description.Usage == MutableTextureUsage::Default, "copied target changed the native kind5 descriptor");
			Record({"create_texture", identity});
			Descriptions[identity - 1] = description;
			if (FailTexture) {
				return {};
			}

			auto allocation = std::make_shared<FixtureAllocation>();
			allocation->Description = description;
			allocation->Owner = identity;
			if (DescriptionMismatch) {
				++allocation->Description.Width;
			}
			Allocations[identity - 1] = allocation;
			++Live;
			return std::make_unique<FixtureTexture>(*this, std::move(allocation));
		}

		std::unique_ptr<MutableTextureView> CreateView(MutableTextureResource& resource, const MutableTextureViewDescription& description) override {
			auto& texture = dynamic_cast<FixtureTexture&>(resource);
			Record({"create_srv", texture.Allocation->Owner});
			++Live;
			return std::make_unique<FixtureShaderView>(*this, texture.Allocation, description);
		}

		std::unique_ptr<MotionCopiedTargetView> CreateRenderTargetView(MutableTextureResource& resource) override {
			auto& texture = dynamic_cast<FixtureTexture&>(resource);
			Record({"create_rtv", texture.Allocation->Owner, static_cast<std::uint32_t>(FailRenderView)});
			if (ThrowRenderView) {
				throw std::runtime_error("fixture render-target view failure");
			}
			if (FailRenderView) {
				return {};
			}

			++Live;
			return std::make_unique<FixtureTargetView>(*this, texture.Allocation);
		}

		std::unique_ptr<MutableTextureSampler> CreateSampler(const MutableTextureSamplerDescription&) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly created a sampler");
		}

		MutableTextureMapping Map(MutableTextureResource&, MutableTextureMapMode) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly mapped its texture");
		}

		void Unmap(MutableTextureResource&) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly unmapped its texture");
		}

		void Upload(MutableTextureResource&, std::span<const std::byte>, std::uint32_t) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly uploaded bytes");
		}

		void Copy(MutableTextureResource&, MutableTextureResource&) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly copied bytes");
		}

		void Bind(MutableTextureView*, MutableTextureSampler*, std::uint32_t, MutableTextureStage) override {
			throw std::runtime_error("copied target ownership fixture unexpectedly bound a texture");
		}

		void Release(std::string_view kind, std::uint32_t identity) noexcept {
			Record({kind, identity});
			--Live;
			if (kind == "release_texture") {
				// The separately owned RTV retains the physical allocation after MTexture retirement.
				Record({"physical_refs", identity, static_cast<std::uint32_t>(Allocations[identity - 1].use_count())});
			}
		}
	};

	FixtureTexture::~FixtureTexture() {
		Backend.Release("release_texture", Allocation->Owner);
	}

	MutableTextureDescription FixtureTexture::Description() const noexcept {
		Backend.Record({"get_descriptor", Allocation->Owner});
		return Allocation->Description;
	}

	FixtureShaderView::~FixtureShaderView() {
		Backend.Release("release_srv", Allocation->Owner);
	}

	FixtureTargetView::~FixtureTargetView() {
		Backend.Release("release_rtv", Allocation->Owner);
	}

	static MotionBounds Rectangle(float width, float height) {
		return {2, 3, 2 + width, 3 + height};
	}

	static void TestReplacementAndRelease() {
		FixtureBackend backend;
		MotionCopiedTargetOwner owner(backend, backend);
		Require(owner.Ensure(Rectangle(9.25f, 7.5f), 87), "initial copied target did not replace");
		Require(owner.State().Size.Width == 10 && owner.State().Size.Height == 8 && owner.State().HasTarget && owner.State().HasTexture && owner.State().HasRenderTargetView && backend.Live == 3, "copied target creation state differs");
		const auto* first_texture = owner.Texture();
		Require(first_texture != nullptr && first_texture->State().Storage == MutableTextureStorage::RenderTarget && first_texture->Resource().Description().OriginalFormat == 87, "copied target lost its actual borrowed kind5 resource");
		backend.EventCount = 0;
		Require(!owner.Ensure(Rectangle(5, 4), 28) && owner.Texture() == first_texture && backend.EventCount == 0, "inclusive half-size retention read the new format or replaced the owner");
		Require(owner.Ensure(Rectangle(10.25f, 8), 28), "larger copied target retained stale dimensions");
		Require(owner.State().Size.Width == 11 && owner.Texture()->State().Description.OriginalFormat == 28 && backend.Allocations[0].expired(), "replacement did not use the new target format or release the old physical allocation");
		Require(backend.Events[0] == FixtureEvent{"release_srv", 1} && backend.Events[1] == FixtureEvent{"release_texture", 1} && backend.Events[2] == FixtureEvent{"physical_refs", 1, 2} && backend.Events[3] == FixtureEvent{"release_rtv", 1} && backend.Events[4] == FixtureEvent{"create_texture", 2}, "copied target replacement reordered MTexture/RTV retirement and allocation");
		backend.EventCount = 0;
		owner.Close();
		Require(backend.Live == 0 && backend.Allocations[1].expired() && !owner.Texture() && owner.State().Closed && !owner.State().HasTarget && owner.State().Size.Width == 11, "copied target Close leaked or reset the retained dimension cache");
		Require(backend.Events[0] == FixtureEvent{"release_srv", 2} && backend.Events[1] == FixtureEvent{"release_texture", 2} && backend.Events[3] == FixtureEvent{"release_rtv", 2}, "copied target Close release order differs");
		const auto event_count = backend.EventCount;
		owner.Close();
		Require(backend.EventCount == event_count, "copied target Close repeated native disposal");
		RequireFailure([&] {
			static_cast<void>(owner.Ensure(Rectangle(20, 20), 87));
		},
		    "closed");
	}

	static void TestFailedViewAndRetention() {
		FixtureBackend backend;
		backend.FailRenderView = true;
		MotionCopiedTargetOwner owner(backend, backend);
		Require(owner.Ensure(Rectangle(8, 6), 87), "failed RTV omitted the outer target transition");
		Require(owner.State().HasTarget && !owner.State().HasTexture && !owner.State().HasRenderTargetView && !owner.Texture() && backend.Live == 0 && backend.Allocations[0].expired(), "failed RTV kept a dangling texture or removed the native outer target");
		Require(backend.Events[3] == FixtureEvent{"create_rtv", 1, 1} && backend.Events[4] == FixtureEvent{"release_srv", 1} && backend.Events[5] == FixtureEvent{"release_texture", 1}, "failed RTV cleanup order differs");
		backend.FailRenderView = false;
		backend.EventCount = 0;
		Require(!owner.Ensure(Rectangle(4, 3), 28) && backend.EventCount == 0 && !owner.Texture(), "failed retained target was silently reconstructed");
		Require(owner.Ensure(Rectangle(9, 7), 28) && owner.Texture() && backend.Live == 3, "later genuine resize could not replace the failed target");
		owner.Close();
		Require(backend.Live == 0, "failed-view fixture leaked a later valid target");
	}

	static void TestBoundsAndPlatformFailures() {
		FixtureBackend backend;
		MutableTextureLimits limits;
		limits.MaximumExtent = 16;
		MotionCopiedTargetOwner owner(backend, backend, limits);
		Require(owner.Ensure(Rectangle(8, 6), 28), "failure fixture could not create initial target");
		const auto* texture = owner.Texture();
		backend.EventCount = 0;
		RequireFailure([&] {
			static_cast<void>(owner.Ensure(Rectangle(17, 7), 28));
		},
		    "extent");
		Require(owner.Texture() == texture && backend.EventCount == 0, "extent rejection disposed the current target");
		backend.ThrowRenderView = true;
		RequireFailure([&] {
			static_cast<void>(owner.Ensure(Rectangle(9, 7), 28));
		},
		    "render-target view");
		Require(owner.State().HasTarget && !owner.Texture() && backend.Live == 0, "throwing render-view failure leaked its texture");
		backend.ThrowRenderView = false;
		backend.FailTexture = true;
		RequireFailure([&] {
			static_cast<void>(owner.Ensure(Rectangle(10, 8), 28));
		},
		    "platform creation");
		Require(owner.State().Size.Width == 10 && owner.State().HasTarget && !owner.Texture() && backend.Live == 0, "platform texture failure left stale target resources");
		backend.FailTexture = false;
		backend.DescriptionMismatch = true;
		RequireFailure([&] {
			static_cast<void>(owner.Ensure(Rectangle(11, 9), 28));
		},
		    "description differs");
		Require(!owner.Texture() && backend.Live == 0 && backend.Allocations[3].expired(), "mismatched resource descriptor leaked its allocation");
		owner.Close();
	}

	static void RunTests() {
		TestReplacementAndRelease();
		TestFailedViewAndRetention();
		TestBoundsAndPlatformFailures();
	}
} // namespace sgre::scene::tests

int main() {
	try {
		sgre::scene::tests::RunTests();
		std::cout << "Copied target ownership checks passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
