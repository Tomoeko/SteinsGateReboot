#include "sgre/scene/resource_meter_process.hpp"

#include "sgre/resource/font.hpp"
#include "sgre/resource/psb.hpp"

#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sgre::scene {

	class ResourceMeterCall {
	public:
		explicit ResourceMeterCall(bool& calling) : m_Calling(calling) {
			if (calling) {
				throw std::runtime_error("ResourceMeter owner dispatch or retirement cannot reenter");
			}

			m_Calling = true;
		}

		~ResourceMeterCall() {
			m_Calling = false;
		}
		ResourceMeterCall(const ResourceMeterCall&) = delete;
		ResourceMeterCall& operator=(const ResourceMeterCall&) = delete;
		ResourceMeterCall(ResourceMeterCall&&) = delete;
		ResourceMeterCall& operator=(ResourceMeterCall&&) = delete;

	private:
		bool& m_Calling;
	};

	class ResourceMeterProcess::CallbackLifetime {
	public:
		explicit CallbackLifetime(ResourceMeterProcess& owner) : Owner(&owner) {
		}

		ResourceMeterProcess* Owner;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::size_t Calls = 0;
	};

	class ResourceMeterProcess::CallbackScope {
	public:
		explicit CallbackScope(const std::weak_ptr<CallbackLifetime>& lifetime) : m_State(lifetime.lock()) {
			if (!m_State || std::this_thread::get_id() != m_State->OwnerThread || !m_State->Owner) {
				throw std::runtime_error("ResourceMeter task callback parent has expired or belongs to another thread");
			}
			if (m_State->Calls == std::numeric_limits<std::size_t>::max()) {
				throw std::runtime_error("ResourceMeter callback scope count exceeds its supported range");
			}

			++m_State->Calls;
		}

		~CallbackScope() {
			--m_State->Calls;
		}
		CallbackScope(const CallbackScope&) = delete;
		CallbackScope& operator=(const CallbackScope&) = delete;
		CallbackScope(CallbackScope&&) = delete;
		CallbackScope& operator=(CallbackScope&&) = delete;

		[[nodiscard]] ResourceMeterProcess& Owner() const {
			return *m_State->Owner;
		}

	private:
		// Only this non-owning expiry/dispatch record survives the call; it never retains the Meter or font.
		std::shared_ptr<CallbackLifetime> m_State;
	};

	void ResourceMeterCounter::Advance() noexcept {
		++Value;
	}

	static std::shared_ptr<const LoadedBitmapFontAsset> PrepareMeterEmbeddedDescriptor(TextureCache& cache, const std::shared_ptr<const LoadedBitmapFontAsset>& supplied) {
		if (!supplied || supplied->CacheOwner != &cache || !supplied->Document || !supplied->Backing || supplied->Document->SerializedSize > supplied->Backing->size()) {
			throw std::runtime_error("ResourceMeter requires its actual complete referring embedded font/backing in the physical cache scope");
		}

		// Native532490 creates a fresh PSBObject descriptor, while its bytes remain the same resident input.
		auto owned = std::make_shared<LoadedBitmapFontAsset>();
		owned->Document = std::make_shared<const resource::PsbDocument>(*supplied->Document);
		owned->Backing = supplied->Backing;
		owned->Model = std::make_shared<const resource::BitmapFont>(resource::ReadBitmapFont(*owned->Document));
		owned->CacheOwner = &cache;
		owned->DocumentIdentity = cache.AllocateDocumentIdentity();

		return owned;
	}

	ResourceMeterProcess::ResourceMeterProcess(LooperHookTasks& actual_hooks, TextureCache& actual_cache, std::shared_ptr<const LoadedBitmapFontAsset> actual_embedded_font, ResourceMeterViewport actual_renderer_viewport) : m_Hooks(actual_hooks), m_CallbackLifetime(std::make_shared<CallbackLifetime>(*this)) {
		auto& pre_loop = actual_hooks.Group(LooperHookPhase::PreLoop);
		auto& post_loop = actual_hooks.Group(LooperHookPhase::PostLoop);
		if (&pre_loop == &post_loop || pre_loop.Status() != TaskStatus::Active || post_loop.Status() != TaskStatus::Active) {
			throw std::runtime_error("ResourceMeter construction requires its distinct active actual Looper hook groups");
		}
		pre_loop.RequireIdleTraversal();
		post_loop.RequireIdleTraversal();
		// Each real Append enforces its physical capacity; partial registration follows the original rollback order.

		m_FontTextures = std::make_unique<BitmapFontTextureOwner>(actual_cache, PrepareMeterEmbeddedDescriptor(actual_cache, actual_embedded_font));
		m_Indicator = std::make_unique<BitmapIndicator>(m_FontTextures->Asset().Model);
		m_FontTextures->SetFiltering(1, 1);
		const auto half_width = static_cast<float>(actual_renderer_viewport.Width) * 0.5f;
		const auto half_height = static_cast<float>(actual_renderer_viewport.Height) * 0.5f;
		m_Indicator->Placement().SetCoordinate({-half_width + 30, -half_height + 20});

		try {
			ParallelTaskCallbacks pre_clock;
			// Both actual Meter hook callback slots are native030f20. Optional Update/Draw therefore stay absent.
			pre_clock.Release = [this](auto) {
				m_PreLoopRetired = true;
			};
			m_PreLoopClock = pre_loop.Append(std::move(pre_clock));
			m_PreLoopRetired = false;

			ParallelTaskCallbacks post_clock;
			post_clock.Release = [this](auto) {
				m_PostLoopRetired = true;
			};
			m_PostLoopClock = post_loop.Append(std::move(post_clock));
			m_PostLoopRetired = false;
		} catch (...) {
			const auto failure = std::current_exception();
			// Preserve the construction failure after completing every published clock/font owner.
			try {
				Close();
			} catch (...) {
				if (!m_Closed) {
					std::terminate();
				}
			}

			std::rethrow_exception(failure);
		}
	}

	ResourceMeterProcess::~ResourceMeterProcess() noexcept {
		if (m_CallbackLifetime->Calls != 0) {
			// An active callback borrows this exact owner; no added strong self root defers its destruction.
			std::terminate();
		}
		try {
			Close();
		} catch (...) {
			if (!m_Closed) {
				std::terminate();
			}
		}
		m_CallbackLifetime->Owner = nullptr;
	}

	void ResourceMeterProcess::RequireOpen() const {
		if (std::this_thread::get_id() != m_OwnerThread || m_Closed || m_PreLoopRetired || m_PostLoopRetired || !m_Hooks.Group(LooperHookPhase::PreLoop).Contains(m_PreLoopClock) || !m_Hooks.Group(LooperHookPhase::PostLoop).Contains(m_PostLoopClock)) {
			throw std::runtime_error("ResourceMeter requires its actual live font/Common/hook registration owners");
		}
	}

	const CommonProcessTasks& ResourceMeterProcess::Tasks() const noexcept {
		return m_Common;
	}

	const BitmapIndicator& ResourceMeterProcess::Indicator() const {
		RequireOpen();

		return *m_Indicator;
	}

	const BitmapFontTextureOwner& ResourceMeterProcess::FontTextures() const {
		RequireOpen();

		return *m_FontTextures;
	}

	std::uint32_t ResourceMeterProcess::ProcessingCount() const {
		RequireOpen();

		return m_Counter.Value;
	}

	DefaultDriverOwnedTaskOperations ResourceMeterProcess::DriverTaskOperations() {
		RequireOpen();
		const std::weak_ptr<CallbackLifetime> lifetime = m_CallbackLifetime;
		DefaultDriverOwnedTaskOperations operations;
		operations.Update = [lifetime]() {
			const CallbackScope call(lifetime);
			call.Owner().Update();
		};
		operations.Draw = [lifetime](std::uint32_t argument) {
			const CallbackScope call(lifetime);
			call.Owner().Draw(argument);
		};
		operations.Retire = [lifetime]() {
			const CallbackScope call(lifetime);
			call.Owner().Close();
		};

		return operations;
	}

	void ResourceMeterProcess::Update() {
		RequireOpen();
		const ResourceMeterCall call(m_Calling);
		m_Common.Update([this]() {
			m_Counter.Advance();
		});
	}

	void ResourceMeterProcess::Draw(std::uint32_t argument) {
		RequireOpen();
		static_cast<void>(argument);

		throw std::runtime_error("unpaused ResourceMeter Draw requires recovered cached-width170 and actual renderer/background/font production");
	}

	bool ResourceMeterProcess::CanRetireHook(LooperHookPhase phase, ParallelTaskHandle task, bool retired) const noexcept {
		if (retired) {
			return true;
		}
		try {
			const auto& group = m_Hooks.Group(phase);

			return group.Contains(task) && group.CanExtract(task);
		} catch (...) {
			return false;
		}
	}

	bool ResourceMeterProcess::CanClose() const noexcept {
		return std::this_thread::get_id() == m_OwnerThread && (m_Closed || (!m_Calling && m_Common.CanClear() && CanRetireHook(LooperHookPhase::PostLoop, m_PostLoopClock, m_PostLoopRetired) && CanRetireHook(LooperHookPhase::PreLoop, m_PreLoopClock, m_PreLoopRetired)));
	}

	bool ResourceMeterProcess::IsClosed() const noexcept {
		return m_Closed;
	}

	void ResourceMeterProcess::RetireHook(LooperHookPhase phase, ParallelTaskHandle task, bool retired) {
		if (retired) {
			return;
		}

		auto clock = m_Hooks.Group(phase).Extract(task);
		clock.Release();
	}

	void ResourceMeterProcess::Close() {
		if (!CanClose()) {
			throw std::runtime_error("ResourceMeter retirement requires all actual hook/Common/owner preflights before mutation");
		}
		if (m_Closed) {
			return;
		}

		const ResourceMeterCall call(m_Calling);
		RetireHook(LooperHookPhase::PostLoop, m_PostLoopClock, m_PostLoopRetired);
		RetireHook(LooperHookPhase::PreLoop, m_PreLoopClock, m_PreLoopRetired);
		std::exception_ptr failure;
		try {
			if (m_FontTextures) {
				m_FontTextures->Close();
			}
		} catch (...) {
			// The accepted font provider finishes every source before reporting its first physical release failure.
			failure = std::current_exception();
		}

		m_Indicator.reset();
		m_FontTextures.reset();
		m_Common.Clear();
		m_Closed = true;
		if (failure) {
			std::rethrow_exception(failure);
		}
	}

} // namespace sgre::scene
