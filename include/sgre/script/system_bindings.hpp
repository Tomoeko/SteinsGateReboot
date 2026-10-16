#pragma once

#include "sgre/core/frame_clock.hpp"
#include "sgre/core/locale.hpp"
#include "sgre/platform/host_time.hpp"
#include "sgre/resource/archive_mounts.hpp"
#include "sgre/resource/resource_cache.hpp"
#include "sgre/script/squirrel_vm.hpp"

#include <functional>
#include <memory>
#include <string_view>

namespace sgre::core {
	class LooperFrame;
	class ApplicationEventControl;
} // namespace sgre::core

namespace sgre::script {

	struct ScriptProcessTerminationServices {
		std::function<void()> RequireLive;
		// Nonreturning exit(0) service; it deliberately does not unwind the script or C++ stack.
		std::function<void()> TerminateProcess;
	};

	class ScriptSystemBindings {
	public:
		using DiskFileExistence = std::function<bool(std::string_view filename)>;
		using LocalDateTimeReader = std::function<platform::HostLocalDateTime()>;
		using ArchiveReadMountResolver = std::function<resource::ArchiveReadMountInput(std::string_view prefix, std::string_view index_identity, std::string_view body_identity)>;
		ScriptSystemBindings(SquirrelVm& vm, core::FrameClock& clock, LocalDateTimeReader local_date_time = {});
		~ScriptSystemBindings();
		ScriptSystemBindings(const ScriptSystemBindings&) = delete;
		ScriptSystemBindings& operator=(const ScriptSystemBindings&) = delete;
		ScriptSystemBindings(ScriptSystemBindings&&) = delete;
		ScriptSystemBindings& operator=(ScriptSystemBindings&&) = delete;

		[[nodiscard]] SquirrelValue SystemClass() const;
		// The renderer publishes its actual target extent; this is independent of desktop display bounds.
		void ConfigureRenderTarget(std::uint32_t width, std::uint32_t height);
		// Same containing Looper as Clock; accumulated ticks, including variable-step timing, supply system seconds.
		void ConfigureLooperTime(core::LooperFrame& actual_looper);
		// Actual containing event owner outlives these bindings. Native System.exit queues the WM_CLOSE reason.
		void ConfigureApplicationClose(core::ApplicationEventControl& actual_events);
		// Platform-owned process boundary. Its actual owner encloses these bindings; headless diagnostics leave it absent.
		void ConfigureProcessTermination(ScriptProcessTerminationServices services);
		// Both callbacks address the same actual graphics owner, whose serialized lifetime encloses these bindings.
		void ConfigureMaskMode(std::function<void(std::uint32_t)> write_mode, std::function<std::uint32_t()> read_mode);
		// The cache is a borrowed engine owner and must outlive these callbacks.
		void ConfigureResourceCache(resource::ResourceCache& cache);
		void ConfigureArchiveMounts(resource::ArchiveMounts& mounts);
		// Native root isExistFile reads disk root zero, independently of System archive-remapping queries.
		// The configured service owns or borrows its actual disk root through these bindings.
		void ConfigureDiskFileExistence(DiskFileExistence service);
		// The actual reader encloses queued mount reception; this resolver borrows immutable physical inputs prepared at startup.
		void ConfigureArchiveReadMounts(resource::ArchiveMounts& mounts, ArchiveReadMountResolver resolver);
		// Locale is initialized at bootstrap; it is read through native static System methods afterward.
		void ConfigureLocale(core::OriginalLocale locale);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
