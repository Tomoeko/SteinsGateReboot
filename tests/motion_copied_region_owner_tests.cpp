#include "sgre/scene/motion_copied_region_owner.hpp"

#include <array>
#include <bit>
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

	class FixtureCopies final : public MotionRegionCopyBackend {
	public:
		explicit FixtureCopies(FixtureBackend& backend) : Backend(backend) {
		}
		void CopyFromCurrentTarget(MutableTextureResource& resource, const MotionBounds& rectangle) override {
			const auto& texture = dynamic_cast<FixtureTexture&>(resource);
			if (FailCopy) {
				throw std::runtime_error("fixture copy submission failed");
			}
			Backend.Record({"copy_region", texture.Allocation->Owner, static_cast<std::uint32_t>(Boxes.size())});
			Boxes.push_back({static_cast<std::uint32_t>(rectangle.MinimumX), static_cast<std::uint32_t>(rectangle.MinimumY), 0, static_cast<std::uint32_t>(rectangle.MaximumX), static_cast<std::uint32_t>(rectangle.MaximumY), 1});
		}
		FixtureBackend& Backend;
		bool FailCopy = false;
		std::vector<std::array<std::uint32_t, 6>> Boxes;
	};

	static MotionCopyRegionPlan CopyPlan(const MotionBounds& rectangle) {
		MotionCopyRegionPlan result;
		result.Action = MotionCopyRegionAction::Copy;
		result.Rectangle = rectangle;
		return result;
	}

	static void TestRegionTransitions() {
		FixtureBackend backend;
		FixtureCopies copies(backend);
		MotionCopiedRegionOwner owner(backend, backend, copies);
		Require(owner.Apply(CopyPlan({2, 3, 10, 9}), 28), "copied region did not request post-copy bounds reset");
		const auto initial = owner.State();
		const auto* texture = owner.TargetTexture();
		Require(owner.SelectedTexture() == texture && initial.Selected && initial.Target.Size.Width == 8 && initial.Target.Size.Height == 6, "copied region did not select its genuine owned target");
		Require(initial.UvAffine == std::array<float, 4>{.125f, 1.0f / 6.0f, -.25f, -.5f} && texture->State().DesiredSampler.FilterWord == 0 && texture->State().SamplerDirty, "copied region changed UV arithmetic or nearest/clamp sampler preparation");
		backend.EventCount = 0;
		MotionCopyRegionPlan plan;
		Require(!owner.Apply(plan, 87) && !owner.SelectedTexture() && owner.TargetTexture() == texture && owner.State().UvAffine == initial.UvAffine && backend.EventCount == 0, "disabled copy discarded retained ownership or affine state");
		plan.Action = MotionCopyRegionAction::Preserve;
		Require(!owner.Apply(plan, 87) && !owner.SelectedTexture() && backend.EventCount == 0, "preserved copy reactivated a disabled texture");
		Require(owner.Apply(CopyPlan({0, 0, 4, 3}), 87) && owner.TargetTexture() == texture && owner.SelectedTexture() == texture && backend.Created == 1, "half-size copy replaced or failed to reselect the actual old-format target");
		Require(owner.State().UvAffine == std::array<float, 4>{.125f, 1.0f / 6.0f, 0, 0} && std::bit_cast<std::uint32_t>(owner.State().UvAffine[2]) == 0, "retained-target reciprocal or signed-zero offset differs");
		owner.Close();
		Require(backend.Live == 0 && !owner.SelectedTexture() && !owner.TargetTexture() && owner.State().Target.Closed, "region Close leaked or exposed a stale texture");
	}

	static void TestRegionFailurePhases() {
		FixtureBackend backend;
		FixtureCopies copies(backend);
		MotionCopiedRegionOwner owner(backend, backend, copies);
		backend.FailRenderView = true;
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({0, 0, 8, 6}), 28));
		},
		    "no texture");
		Require(copies.Boxes.empty() && backend.Live == 0 && owner.State().Target.HasTarget && !owner.State().Selected, "failed RTV reached copy or left a selected stale owner");
		backend.FailRenderView = false;
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({0, 0, 4, 3}), 28));
		},
		    "no texture");
		Require(backend.Created == 1 && copies.Boxes.empty(), "retained failed target silently retried physical creation");
		copies.FailCopy = true;
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({0, 0, 9, 7}), 28));
		},
		    "submission failed");
		Require(owner.TargetTexture() && !owner.SelectedTexture() && owner.State().UvAffine == std::array<float, 4>{} && backend.Live == 3, "copy submission failure committed selection/UV state");
		copies.FailCopy = false;
		Require(owner.Apply(CopyPlan({0, 0, 9, 7}), 28) && owner.SelectedTexture(), "valid retained target could not recover after an authored copy error");
		owner.Close();
		Require(backend.Live == 0, "region failure fixture leaked owners");
	}

	static void TestRegionPreparedDomain() {
		FixtureBackend backend;
		FixtureCopies copies(backend);
		MutableTextureLimits limits;
		limits.MaximumExtent = 8;
		MotionCopiedRegionOwner owner(backend, backend, copies, limits);
		Require(owner.Apply(CopyPlan({0, 0, 8, 6}), 28), "prepared-domain fixture did not create its valid target");
		const auto* texture = owner.TargetTexture();
		backend.EventCount = 0;
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({0, 0, 9, 7}), 28));
		},
		    "extent");
		Require(owner.SelectedTexture() == texture && backend.EventCount == 0, "extent preflight altered ownership or selection");
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({.5f, 0, 8, 6}), 28));
		},
		    "integer coordinates");
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({-1, 0, 8, 6}), 28));
		},
		    "integer coordinates");
		RequireFailure([&] {
			static_cast<void>(owner.Apply(CopyPlan({0, 0, 0, 6}), 28));
		},
		    "empty");
		MotionCopyRegionPlan invalid;
		invalid.Action = static_cast<MotionCopyRegionAction>(99);
		RequireFailure([&] {
			static_cast<void>(owner.Apply(invalid, 28));
		},
		    "action");
		Require(backend.EventCount == 0 && owner.SelectedTexture() == texture, "prepared-domain rejection submitted or changed state");
		owner.Close();
		RequireFailure([&] {
			static_cast<void>(owner.Apply(MotionCopyRegionPlan{}, 28));
		},
		    "closed");
		Require(backend.Live == 0, "prepared-domain fixture leaked its target");
	}
} // namespace sgre::scene::tests

int main() {
	try {
		sgre::scene::tests::TestRegionTransitions();
		sgre::scene::tests::TestRegionFailurePhases();
		sgre::scene::tests::TestRegionPreparedDomain();
		std::cout << "Copied region three phase/domain/lifetime groups passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
