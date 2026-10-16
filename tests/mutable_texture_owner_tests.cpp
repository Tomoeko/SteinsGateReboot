#include "sgre/scene/mutable_texture_owner.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
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
			Require(std::string_view(error.what()).find(context) != std::string_view::npos, "mutable texture failure omitted context");
			return;
		}
		throw std::runtime_error("invalid mutable texture operation accepted");
	}
	struct FixtureEvent {
		std::string_view Action;
		std::uint32_t Owner = 0;
		std::uint32_t Value = 0;
		std::uint32_t Extra = 0;
		bool operator==(const FixtureEvent&) const = default;
	};
	struct FixtureRecord {
		MutableTextureDescription Description;
		std::vector<std::byte> Bytes;
		bool Alive = false;
	};
	class FixtureBackend;
	class FixtureTexture final : public MutableTextureResource {
	public:
		FixtureTexture(FixtureBackend& backend, std::uint32_t owner) : m_Backend(backend), m_Owner(owner) {
		}
		~FixtureTexture() override;
		MutableTextureDescription Description() const noexcept override;
		std::uint32_t Owner() const noexcept {
			return m_Owner;
		}

	private:
		FixtureBackend& m_Backend;
		std::uint32_t m_Owner;
	};
	class FixtureView final : public MutableTextureView {
	public:
		FixtureView(FixtureBackend& backend, std::uint32_t owner, const MutableTextureViewDescription& description) : m_Backend(backend), m_Owner(owner), m_Description(description) {
		}
		~FixtureView() override;
		MutableTextureViewDescription Description() const noexcept override {
			return m_Description;
		}
		std::uint32_t Owner() const noexcept {
			return m_Owner;
		}

	private:
		FixtureBackend& m_Backend;
		std::uint32_t m_Owner;
		MutableTextureViewDescription m_Description;
	};
	class FixtureSampler final : public MutableTextureSampler {
	public:
		FixtureSampler(FixtureBackend& backend, std::uint32_t sequence, const MutableTextureSamplerDescription& description) : m_Backend(backend), m_Sequence(sequence), m_Description(description) {
		}
		~FixtureSampler() override;
		MutableTextureSamplerDescription Description() const noexcept override {
			return m_Description;
		}
		std::uint32_t Sequence() const noexcept {
			return m_Sequence;
		}

	private:
		FixtureBackend& m_Backend;
		std::uint32_t m_Sequence;
		MutableTextureSamplerDescription m_Description;
	};
	class FixtureBackend final : public MutableTextureBackend {
	public:
		std::array<FixtureRecord, 4> Records;
		std::array<FixtureEvent, 128> Events;
		std::size_t EventCount = 0;
		std::uint32_t TextureCount = 0;
		std::uint32_t SamplerCount = 0;
		std::uint32_t Live = 0;
		std::uint32_t FailTextureOn = 0;
		bool FailMap = false;
		bool FailView = false;
		bool FailSampler = false;
		bool ChangeDescription = false;
		bool ThrowUpload = false;
		MutableTextureSamplerDescription LastSampler;

		void Record(FixtureEvent event) noexcept {
			if (EventCount >= Events.size()) {
				std::terminate();
			}
			Events[EventCount++] = event;
		}
		void ClearEvents() noexcept {
			EventCount = 0;
		}
		std::span<const FixtureEvent> Recorded() const noexcept {
			return std::span(Events).first(EventCount);
		}
		std::unique_ptr<MutableTextureResource> CreateTexture(const MutableTextureDescription& description, const MutableTextureInitialData& initial) override {
			const auto owner = ++TextureCount;
			Record({"create", owner});
			if (owner == FailTextureOn) {
				return {};
			}
			Require(owner <= Records.size() && description.Width <= 64 && description.Height <= 64 && description.OriginalFormat == 28, "texture fixture exceeds its bounded RGBA layout");
			auto& record = Records[owner - 1];
			record.Description = description;
			record.Bytes.resize(std::size_t{description.Width} * description.Height * 4);
			record.Alive = true;
			if (!initial.Bytes.empty()) {
				const auto row_bytes = std::size_t{description.Width} * 4;
				for (std::size_t row = 0; row < description.Height; ++row) {
					std::copy_n(initial.Bytes.begin() + static_cast<std::ptrdiff_t>(row * initial.RowPitch), row_bytes, record.Bytes.begin() + static_cast<std::ptrdiff_t>(row * row_bytes));
				}
			}
			if (ChangeDescription) {
				++record.Description.Width;
			}
			++Live;
			return std::make_unique<FixtureTexture>(*this, owner);
		}
		std::unique_ptr<MutableTextureView> CreateView(MutableTextureResource& resource, const MutableTextureViewDescription& description) override {
			const auto owner = static_cast<FixtureTexture&>(resource).Owner();
			Record({"view", owner});
			if (FailView) {
				return {};
			}
			++Live;
			return std::make_unique<FixtureView>(*this, owner, description);
		}
		std::unique_ptr<MutableTextureSampler> CreateSampler(const MutableTextureSamplerDescription& description) override {
			const auto sequence = ++SamplerCount;
			Record({"sampler", sequence, description.FilterWord});
			LastSampler = description;
			if (FailSampler) {
				return {};
			}
			++Live;
			return std::make_unique<FixtureSampler>(*this, sequence, description);
		}
		MutableTextureMapping Map(MutableTextureResource& resource, MutableTextureMapMode mode) override {
			const auto owner = static_cast<FixtureTexture&>(resource).Owner();
			Record({"map", owner, static_cast<std::uint32_t>(mode)});
			if (FailMap) {
				return {};
			}
			auto& record = Records[owner - 1];
			return {record.Bytes, record.Description.Width * 4, true};
		}
		void Unmap(MutableTextureResource& resource) override {
			Record({"unmap", static_cast<FixtureTexture&>(resource).Owner()});
		}
		void Upload(MutableTextureResource& resource, std::span<const std::byte> bytes, std::uint32_t row_pitch) override {
			const auto owner = static_cast<FixtureTexture&>(resource).Owner();
			Record({"upload", owner, row_pitch});
			if (ThrowUpload) {
				throw std::runtime_error("fixture upload failed");
			}
			auto& record = Records[owner - 1];
			Require(row_pitch == record.Description.Width * 4 && bytes.size() == record.Bytes.size(), "shadow upload changed row layout");
			std::copy(bytes.begin(), bytes.end(), record.Bytes.begin());
		}
		void Copy(MutableTextureResource& destination, MutableTextureResource& source) override {
			const auto target = static_cast<FixtureTexture&>(destination).Owner();
			const auto origin = static_cast<FixtureTexture&>(source).Owner();
			Record({"copy", target, origin});
			Require(Records[target - 1].Bytes.size() == Records[origin - 1].Bytes.size(), "companion copy changed layout");
			std::copy(Records[origin - 1].Bytes.begin(), Records[origin - 1].Bytes.end(), Records[target - 1].Bytes.begin());
		}
		void Bind(MutableTextureView* view, MutableTextureSampler* sampler, std::uint32_t slot, MutableTextureStage stage) override {
			Record({"bind", view ? static_cast<FixtureView*>(view)->Owner() : 0, slot, static_cast<std::uint32_t>(stage)});
			Record({"bind_sampler", sampler ? static_cast<FixtureSampler*>(sampler)->Sequence() : 0});
		}
		void Release(std::string_view kind, std::uint32_t owner) noexcept {
			Record({kind, owner});
			--Live;
			if (kind == "release_texture") {
				Records[owner - 1].Alive = false;
			}
		}
	};
	FixtureTexture::~FixtureTexture() {
		m_Backend.Release("release_texture", m_Owner);
	}
	MutableTextureDescription FixtureTexture::Description() const noexcept {
		return m_Backend.Records[m_Owner - 1].Description;
	}
	FixtureView::~FixtureView() {
		m_Backend.Release("release_view", m_Owner);
	}
	FixtureSampler::~FixtureSampler() {
		m_Backend.Release("release_sampler", m_Sequence);
	}
	static MutableTextureSpec Spec(MutableTextureStorage storage) {
		MutableTextureSpec spec;
		spec.Storage = storage;
		spec.Description.Width = 3;
		spec.Description.Height = 2;
		return spec;
	}
	static void RequireEvents(const FixtureBackend& backend, std::initializer_list<FixtureEvent> expected) {
		Require(std::ranges::equal(backend.Recorded(), expected), "mutable texture backend call order changed");
	}
	static void TestShadowWritesAndRetry() {
		FixtureBackend backend;
		{
			MutableTextureOwner texture(backend, Spec(MutableTextureStorage::CpuShadow));
			backend.ClearEvents();
			const auto mapped = texture.Map();
			Require(mapped.Succeeded && mapped.Bytes.size() == 24 && mapped.RowPitch == 12 && texture.State().CachedRowPitch == 0, "shadow map populated the native pitch cache or changed byte bounds");
			for (std::size_t index = 0; index < mapped.Bytes.size(); ++index) {
				mapped.Bytes[index] = static_cast<std::byte>(index + 1);
			}
			const std::vector<std::byte> expected(mapped.Bytes.begin(), mapped.Bytes.end());
			Require(texture.Map().Bytes.empty(), "shadow map permitted lock reentry");
			texture.Unmap();
			backend.ThrowUpload = true;
			RequireFailure([&]() {
				texture.Bind(7, MutableTextureStage::Pixel);
			},
			    "upload failed");
			Require(texture.State().Dirty, "failed upload lost pending shadow changes");
			backend.ThrowUpload = false;
			texture.Bind(7, MutableTextureStage::Pixel);
			Require(std::ranges::equal(expected, backend.Records[0].Bytes), "shadow bytes did not reach the texture upload");
			Require(!texture.State().Dirty && texture.State().CachedRowPitch == 12, "successful upload failed to clear dirty state/cache pitch");
			RequireEvents(backend, {{"upload", 1, 12}, {"upload", 1, 12}, {"sampler", 1}, {"bind", 1, 7, 1}, {"bind_sampler", 1}});
			backend.ClearEvents();
			Require(texture.Map(true).Succeeded && !texture.State().Dirty, "preserving shadow map marked data dirty");
			texture.Unmap();
			texture.Bind(7, MutableTextureStage::Pixel);
			RequireEvents(backend, {{"bind", 1, 7, 1}, {"bind_sampler", 1}});
			backend.ClearEvents();
		}
		RequireEvents(backend, {{"release_view", 1}, {"release_sampler", 1}, {"release_texture", 1}});
		Require(backend.Live == 0, "shadow texture leaked an exclusive backend resource");
	}
	static void TestMapFailurePitchAndRelease() {
		FixtureBackend backend;
		backend.FailMap = true;
		{
			MutableTextureOwner texture(backend, Spec(MutableTextureStorage::DynamicWrite));
			backend.ClearEvents();
			Require(texture.RowPitch() == 12 && !texture.State().Locked, "failed temporary pitch map changed lock state");
			Require(!texture.Map(true).Succeeded && texture.State().Locked && !texture.State().Dirty, "failed preserving map did not retain native lock state");
			Require(texture.Map().Bytes.empty(), "failed map permitted reentry");
			texture.Unmap();
			texture.Unmap();
			RequireEvents(backend, {{"map", 1, 4}, {"map", 1, 4}, {"unmap", 1}});
			backend.FailMap = false;
			Require(texture.Map().Succeeded && texture.State().Dirty, "successful dynamic map failed to mark changed data");
			backend.ClearEvents();
		}
		RequireEvents(backend, {{"release_view", 1}, {"release_texture", 1}});
		Require(backend.Live == 0, "mapped destruction leaked resources or added an unmap");
		FixtureBackend staging_backend;
		MutableTextureOwner staging(staging_backend, Spec(MutableTextureStorage::Staging));
		staging_backend.ClearEvents();
		Require(staging.RowPitch() == 12 && staging.Map(true).Succeeded, "staging pitch/map failed");
		staging.Unmap();
		RequireEvents(staging_backend, {{"map", 1, 1}, {"unmap", 1}, {"map", 1, 3}, {"unmap", 1}});
	}
	static void TestSamplerCache() {
		FixtureBackend backend;
		MutableTextureOwner texture(backend, Spec(MutableTextureStorage::GpuOnly));
		texture.Bind(0, MutableTextureStage::Vertex);
		const auto original = backend.LastSampler;
		Require(original.AddressW == 1 && original.MaximumAnisotropy == 2 && original.Comparison == 1 && original.MinimumLod == -std::numeric_limits<float>::max() && original.MaximumLod == std::numeric_limits<float>::max(), "sampler defaults changed");
		backend.ClearEvents();
		texture.SetFiltering(1, 1);
		Require(texture.State().DesiredSampler.FilterWord == 21 && !texture.State().SamplerDirty, "filter-only change dirtied existing sampler");
		texture.Bind(0, MutableTextureStage::Vertex);
		RequireEvents(backend, {{"bind", 1}, {"bind_sampler", 1}});
		backend.ClearEvents();
		texture.SetAddress(0, 0);
		texture.Bind(0, MutableTextureStage::Vertex);
		RequireEvents(backend, {{"release_sampler", 1}, {"sampler", 2, 21}, {"bind", 1}, {"bind_sampler", 2}});
		Require(backend.LastSampler.AddressU == 3 && backend.LastSampler.AddressV == 3, "same-address replacement changed sampler address");
		texture.SetFiltering(3, 0);
		Require(texture.State().DesiredSampler.FilterWord == 16, "native asymmetric filter branch was normalized");
		texture.SetFiltering(1, 0);
		Require(texture.State().DesiredSampler.FilterWord == 4, "native mip/asymmetric filter branch changed");
		RequireFailure([&]() {
			texture.SetFiltering(6, 0);
		},
		    "filter index");
		RequireFailure([&]() {
			texture.SetAddress(3, 0);
		},
		    "address index");
		RequireFailure([&]() {
			texture.Bind(0, static_cast<MutableTextureStage>(2));
		},
		    "shader stage");
	}
	static void TestCompanionOrderAndData() {
		FixtureBackend backend;
		{
			MutableTextureOwner texture(backend, Spec(MutableTextureStorage::StagingCompanion));
			Require(texture.Companion() && !texture.State().HasView && texture.Companion()->State().HasView, "staging companion did not own the sampled view");
			backend.ClearEvents();
			const auto mapped = texture.Map();
			mapped.Bytes[5] = std::byte{0xa7};
			texture.Unmap();
			texture.Bind(0x123, MutableTextureStage::Pixel);
			Require(backend.Records[1].Bytes[5] == std::byte{0xa7}, "staging changes did not copy into the sampled companion");
			RequireEvents(backend, {{"map", 1, 3}, {"unmap", 1}, {"copy", 2, 1}, {"sampler", 1}, {"bind", 2, 0x23, 1}, {"bind_sampler", 1}});
			backend.ClearEvents();
			texture.SetFiltering(1, 1);
			texture.Bind(1, MutableTextureStage::Pixel);
			Require(texture.Companion()->State().DesiredSampler.FilterWord == 0, "filter-only parent change incorrectly propagated to companion");
			texture.SetAddress(1, 2);
			texture.Bind(1, MutableTextureStage::Pixel);
			Require(backend.LastSampler.FilterWord == 21 && backend.LastSampler.AddressU == 1 && backend.LastSampler.AddressV == 2, "address invalidation did not propagate companion sampler values");
			backend.ClearEvents();
		}
		RequireEvents(backend, {{"release_view", 2}, {"release_sampler", 2}, {"release_texture", 2}, {"release_texture", 1}});
		Require(backend.Live == 0, "companion destruction leaked a platform resource");
	}
	static void TestInitialDataAndFailures() {
		FixtureBackend backend;
		std::array<std::byte, 32> bytes;
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			bytes[index] = static_cast<std::byte>(index);
		}
		{
			MutableTextureOwner texture(backend, Spec(MutableTextureStorage::GpuOnly), {bytes, 16});
			Require(backend.Records[0].Bytes[12] == std::byte{16} && backend.Records[0].Bytes[23] == std::byte{27}, "initial data lost explicit row padding");
		}
		Require(backend.Live == 0, "initial-data texture leaked");
		FixtureBackend failed;
		failed.FailTextureOn = 2;
		RequireFailure([&]() {
			MutableTextureOwner texture(failed, Spec(MutableTextureStorage::StagingCompanion));
		},
		    "creation failed");
		Require(failed.Live == 0, "failed companion creation did not release the parent texture");
		RequireEvents(failed, {{"create", 1}, {"create", 2}, {"release_texture", 1}});
		FixtureBackend malformed;
		malformed.ChangeDescription = true;
		RequireFailure([&]() {
			MutableTextureOwner texture(malformed, Spec(MutableTextureStorage::CpuShadow));
		},
		    "prepared layout");
		Require(malformed.Live == 0, "unexpected platform description leaked ownership");
		FixtureBackend missing;
		missing.FailView = true;
		missing.FailSampler = true;
		{
			MutableTextureOwner texture(missing, Spec(MutableTextureStorage::GpuOnly));
			missing.ClearEvents();
			texture.Bind(2, MutableTextureStage::Pixel);
			texture.Bind(2, MutableTextureStage::Pixel);
			RequireEvents(missing, {{"sampler", 1}, {"bind", 0, 2, 1}, {"bind_sampler", 0}, {"sampler", 2}, {"bind", 0, 2, 1}, {"bind_sampler", 0}});
		}
		Require(missing.Live == 0, "nullable native view/sampler creation leaked a texture");
	}
	static void TestLimitsAndLayout() {
		FixtureBackend backend;
		auto spec = Spec(MutableTextureStorage::CpuShadow);
		MutableTextureLimits limits;
		limits.MaximumShadowBytes = 23;
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec, {}, limits);
		},
		    "shadow exceeds");
		spec.Description.Width = 0;
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec);
		},
		    "prepared domain");
		spec = Spec(MutableTextureStorage::CpuShadow);
		spec.Description.OriginalFormat = 71;
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec);
		},
		    "layout is not recovered");
		spec = Spec(MutableTextureStorage::GpuOnly);
		spec.Description.MipLevels = 33;
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec);
		},
		    "prepared domain");
		spec.Description.MipLevels = 1;
		spec.Description.SampleCount = 33;
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec);
		},
		    "prepared domain");
		spec = Spec(MutableTextureStorage::GpuOnly);
		std::array<std::byte, 23> bytes{};
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec, {bytes, 12});
		},
		    "truncated");
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec, {bytes, 11});
		},
		    "truncated");
		spec.Storage = static_cast<MutableTextureStorage>(7);
		RequireFailure([&]() {
			MutableTextureOwner texture(backend, spec);
		},
		    "storage kind");
		Require(backend.TextureCount == 0 && backend.Live == 0, "invalid prepared input entered platform allocation");
	}
} // namespace sgre::scene::tests

int main() {
	try {
		sgre::scene::tests::TestShadowWritesAndRetry();
		sgre::scene::tests::TestMapFailurePitchAndRelease();
		sgre::scene::tests::TestSamplerCache();
		sgre::scene::tests::TestCompanionOrderAndData();
		sgre::scene::tests::TestInitialDataAndFailures();
		sgre::scene::tests::TestLimitsAndLayout();
		std::cout << "Mutable texture ownership, byte updates, native cache/map behavior and bounded failures passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
