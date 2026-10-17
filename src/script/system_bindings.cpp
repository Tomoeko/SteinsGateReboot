#include "sgre/script/system_bindings.hpp"

#include "sgre/core/application_event_control.hpp"
#include "sgre/core/looper_frame.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace sgre::script {

	static bool VariableFrameTruth(const SquirrelValue& value) {
		if (std::holds_alternative<std::monostate>(value)) {
			return false;
		}
		if (const auto* number = std::get_if<float>(&value)) {
			return *number != 0;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return *number != 0;
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean;
		}

		return true;
	}

	class SystemTerminationCall {
	public:
		explicit SystemTerminationCall(bool& operating) : m_Operating(operating) {
			if (operating) {
				throw std::runtime_error("System process termination service cannot reenter configuration or invocation");
			}

			m_Operating = true;
		}
		~SystemTerminationCall() noexcept {
			m_Operating = false;
		}
		SystemTerminationCall(const SystemTerminationCall&) = delete;
		SystemTerminationCall& operator=(const SystemTerminationCall&) = delete;

	private:
		bool& m_Operating;
	};

	class ScriptSystemBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, core::FrameClock& clock, LocalDateTimeReader local_date_time)
		    : Vm(vm), Clock(clock), LocalDateTime(std::move(local_date_time)) {
		}

		SquirrelVm& Vm;
		core::FrameClock& Clock;
		core::LooperFrame* Looper = nullptr;
		core::ApplicationEventControl* ApplicationEvents = nullptr;
		std::optional<ScriptProcessTerminationServices> ProcessTermination;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		bool TerminationOperating = false;
		LocalDateTimeReader LocalDateTime;
		std::function<void(std::uint32_t)> WriteMaskMode;
		std::function<std::uint32_t()> ReadMaskMode;
		resource::ResourceCache* Resources = nullptr;
		resource::ArchiveMounts* Mounts = nullptr;
		ArchiveReadMountResolver ResolveArchiveRead;
		DiskFileExistence DiskFileExists;
		std::optional<core::OriginalLocale> Locale;
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		SquirrelValue Class;
	};

	ScriptSystemBindings::ScriptSystemBindings(SquirrelVm& vm, core::FrameClock& clock, LocalDateTimeReader local_date_time)
	    : m_Implementation(std::make_shared<Implementation>(vm, clock, std::move(local_date_time))) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass();
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		const auto method = [&](std::string name, auto operation, bool is_static = true) {
			const auto closure = vm.CreateNativeClosure("System." + name, [weak, operation](SquirrelVm&, std::span<const SquirrelValue> arguments) {
				const auto owner = weak.lock();
				if (!owner) {
					throw std::runtime_error("native System bindings have expired");
				}

				return operation(*owner, arguments);
			});
			vm.NewSlot(state.Class, name, closure, is_static);
		};

		// Native 04b530 is the registered empty constructor; build flavor does not change 04b570's false.
		method(
		    "constructor", [](Implementation&, auto) {
			    return SquirrelNativeResult{};
		    },
		    false);
		method(
		    "weakref", [](Implementation& owner, auto) {
			    return SquirrelNativeResult::ReturnValue(owner.Vm.CreateWeakReference(owner.Vm.NativeStackValue(-1)));
		    },
		    false);
		for (const bool raw : {false, true}) {
			const std::string name = raw ? "isExistentFileRaw" : "isExistentFile";
			const std::array<std::uint8_t, 8> descriptor{static_cast<std::uint8_t>(raw ? 17 : 16), 0, 0, 0, 0, 0, 0, 0};
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			const auto closure = vm.CreateNativeClosureWithCaptures(
			    "System." + name, [weak, raw, descriptor](SquirrelVm& active, auto) {
				    const auto owner = weak.lock();
				    if (!owner || &owner->Vm != &active) {
					    throw std::runtime_error("native System bindings have expired");
				    }
				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin()) || owner->Mounts == nullptr) {
					    throw std::runtime_error("System file existence requires its actual archive owner and callback descriptor");
				    }

				    // Native043a70 stringifies physical argument two; native285a10 appends .m before remapping.
				    auto filename = active.ToString(active.NativeStackValue(2));
				    if (const auto end = filename.find('\0'); end != std::string::npos) {
					    filename.resize(end);
				    }
				    if (!raw) {
					    filename += ".m";
				    }
				    // Native4d5480 tests archive remapping, without opening or checking a loose physical file.
				    const auto resolved = filename.empty() ? std::optional<resource::ResolvedArchiveEntry>{} : owner->Mounts->Resolve(filename);
				    return SquirrelNativeResult::ReturnValue(resolved && resolved->BodyIdentity != resolved->ResourceFilename);
			    },
			    captures);
			vm.NewSlot(state.Class, name, closure, true);
		}
		// Native048030 publishes this physical-file predicate on root before init's optional true fallback.
		const std::array<std::uint8_t, 8> disk_descriptor{0x80, 0xb5, 0x04, 0x40, 0x01, 0, 0, 0};
		const std::array<SquirrelValue, 1> disk_captures{vm.CreateUserData(disk_descriptor)};
		const auto disk_exists = vm.CreateNativeClosureWithCaptures("isExistFile", [weak, disk_descriptor](SquirrelVm& active, auto) {
			const auto owner = weak.lock();
			if (!owner || &owner->Vm != &active || std::this_thread::get_id() != owner->OwnerThread || active.IsClosed()) {
				throw std::runtime_error("native disk file existence requires its live serialized System binding owner");
			}
			const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
			if (payload.size() != disk_descriptor.size() || !std::equal(payload.begin(), payload.end(), disk_descriptor.begin()) || !owner->DiskFileExists) {
				throw std::runtime_error("native disk file existence requires its actual configured disk-root service and callback descriptor");
			}

			// Original043a70 stringifies physical argument two once;04b340 appends only its C-string prefix.
			const auto filename = active.ToString(active.NativeStackValue(2));
			const auto prefix = std::string_view(filename).substr(0, filename.find('\0'));

			return SquirrelNativeResult::ReturnValue(owner->DiskFileExists(prefix));
		},
		    disk_captures);
		vm.NewSlot(vm.RootTable(), std::string("isExistFile"), disk_exists);
		vm.NewSlot(vm.RootTable(), std::string("reqExitGame"), vm.CreateNativeClosure("reqExitGame", [weak](SquirrelVm& active, std::span<const SquirrelValue>) {
			const auto owner = weak.lock();
			if (!owner || &owner->Vm != &active || std::this_thread::get_id() != owner->OwnerThread || active.IsClosed() || owner->ApplicationEvents == nullptr) {
				throw std::runtime_error("reqExitGame requires its live serialized containing application close owner");
			}
			// Bootstrap 0x140047dc8 captures 0x14004cfb0, requesting reason 2 at 0x1404e0ae0.
			owner->ApplicationEvents->RequestClose(2);

			return SquirrelNativeResult::NoValues();
		}));
		method("exit", [](Implementation& owner, auto) {
			if (owner.ApplicationEvents == nullptr) {
				throw std::runtime_error("System.exit requires its actual containing application close owner");
			}

			// Native283d70 sends WM_CLOSE; native4e7520 publishes close reason1 without terminating here.
			owner.ApplicationEvents->RequestClose(1);

			return SquirrelNativeResult::NoValues();
		});
		method("terminate", [](Implementation& owner, auto) -> SquirrelNativeResult {
			if (std::this_thread::get_id() != owner.OwnerThread || owner.Vm.IsClosed()) {
				throw std::runtime_error("System process termination requires its live serialized binding owner");
			}
			const SystemTerminationCall call(owner.TerminationOperating);
			if (!owner.ProcessTermination) {
				throw std::runtime_error("System.terminate requires its original CRT process-exit boundary; no platform process termination service is configured");
			}

			owner.ProcessTermination->RequireLive();
			if (owner.Vm.IsClosed()) {
				throw std::runtime_error("System process termination preflight retired its actual VM");
			}

			// Native289db0 invokes CRT exit(0); this boundary does not imply script or stack unwind.
			owner.ProcessTermination->TerminateProcess();
			throw std::runtime_error("System process termination service returned from its nonreturning exit(0) boundary");
		});
		method("getDebugBuild", [](Implementation&, auto) {
			SquirrelNativeResult result;
			result.Value = false;

			return result;
		});
		method("getSpec", [](Implementation&, auto) {
			SquirrelNativeResult result;
			result.Value = std::string("win");

			return result;
		});
		method("getLanguageString", [](Implementation& owner, auto) {
			if (!owner.Locale) {
				throw std::runtime_error("System language/country service has not been initialized");
			}

			SquirrelNativeResult result;
			result.Value = owner.Vm.CreateString(std::string(owner.Locale->Language));

			return result;
		});
		method("getCountryString", [](Implementation& owner, auto) {
			if (!owner.Locale) {
				throw std::runtime_error("System language/country service has not been initialized");
			}

			SquirrelNativeResult result;
			result.Value = owner.Vm.CreateString(std::string(owner.Locale->Country));

			return result;
		});
		method("getLanguageInt", [](Implementation& owner, auto) {
			if (!owner.Locale) {
				throw std::runtime_error("System language/country service has not been initialized");
			}

			SquirrelNativeResult result;
			result.Value = static_cast<std::int32_t>(core::PackOriginalLocalePrefix(owner.Locale->Language));

			return result;
		});
		method("getCountryInt", [](Implementation& owner, auto) {
			if (!owner.Locale) {
				throw std::runtime_error("System language/country service has not been initialized");
			}

			SquirrelNativeResult result;
			result.Value = static_cast<std::int32_t>(core::PackOriginalLocalePrefix(owner.Locale->Country));

			return result;
		});
		const std::array<std::uint8_t, 8> passed_frame_descriptor{4, 0, 0, 0, 0, 0, 0, 0};
		const std::array<SquirrelValue, 1> passed_frame_captures{vm.CreateUserData(passed_frame_descriptor)};
		const auto passed_frame = vm.CreateNativeClosureWithCaptures(
		    "System.getPassedFrame", [weak, passed_frame_descriptor](SquirrelVm& active, auto) {
			    const auto owner = weak.lock();
			    if (!owner || &owner->Vm != &active) {
				    throw std::runtime_error("native System bindings have expired");
			    }

			    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
			    if (payload.size() != passed_frame_descriptor.size() || !std::equal(payload.begin(), payload.end(), passed_frame_descriptor.begin()) || owner->Looper == nullptr) {
				    throw std::runtime_error("System.getPassedFrame requires its actual containing Looper and static descriptor");
			    }

			    const auto base_count = owner->Looper->FixedBaseCount();
			    // Native2845d0 -> 4e05d0 returns fixed +8 or MAXSS(variable +20, 1), without consuming either clock.
			    const auto step = owner->Clock.VariableStep();
			    return SquirrelNativeResult::ReturnValue(owner->Clock.VariableFrame() ? (step > 1.0f ? step : 1.0f) : static_cast<float>(base_count));
		    },
		    passed_frame_captures);
		vm.NewSlot(state.Class, "getPassedFrame", passed_frame, true);
		const std::array<std::uint8_t, 8> seconds_descriptor{3, 0, 0, 0, 0, 0, 0, 0};
		const std::array<SquirrelValue, 1> seconds_captures{vm.CreateUserData(seconds_descriptor)};
		const auto system_second = vm.CreateNativeClosureWithCaptures(
		    "System.getSystemSecond", [weak, seconds_descriptor](SquirrelVm& active, auto) {
			    const auto owner = weak.lock();
			    if (!owner || &owner->Vm != &active) {
				    throw std::runtime_error("native System bindings have expired");
			    }

			    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
			    if (payload.size() != seconds_descriptor.size() || !std::equal(payload.begin(), payload.end(), seconds_descriptor.begin()) || owner->Looper == nullptr) {
				    throw std::runtime_error("System.getSystemSecond requires its actual containing Looper and static descriptor");
			    }

			    // Native284f80 reads Looper+18, divides in double precision, then uses CVTTSD2SI.
			    const auto seconds = owner->Looper->AccumulatedTicks() / 60.0;
			    const auto value = !std::isfinite(seconds) || seconds < -2147483648.0 || seconds >= 2147483648.0 ? std::numeric_limits<std::int32_t>::min() : static_cast<std::int32_t>(seconds);

			    return SquirrelNativeResult::ReturnValue(value);
		    },
		    seconds_captures);
		vm.NewSlot(state.Class, "getSystemSecond", system_second, true);
		for (const bool write_mode : {false, true}) {
			const std::array<std::uint8_t, 8> descriptor{1, 0, 0, 0, static_cast<std::uint8_t>(write_mode), 0, 0, 0};
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			const auto name = write_mode ? "setVariableFrame" : "getVariableFrame";
			const auto closure = vm.CreateNativeClosureWithCaptures(
			    std::string("System.") + name, [weak, descriptor, write_mode](SquirrelVm& active, auto) {
				    const auto owner = weak.lock();
				    if (!owner || &owner->Vm != &active) {
					    throw std::runtime_error("native System bindings have expired");
				    }

				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
					    throw std::runtime_error("System variable-frame descriptor does not match its original static operation");
				    }
				    if (!write_mode) {
					    return SquirrelNativeResult::ReturnValue(owner->Clock.VariableFrame());
				    }

				    // Native043b70 uses225b00/25a420 truth testing at stack2 and returns zero values.
				    owner->Clock.SetVariableFrame(VariableFrameTruth(active.NativeAllocatedStackValue(2)));

				    return SquirrelNativeResult::NoValues();
			    },
			    captures);
			vm.NewSlot(state.Class, std::string(name), closure, true);
		}
		for (const bool write_mode : {false, true}) {
			const std::array<std::uint8_t, 8> descriptor{2, 0, 0, 0, static_cast<std::uint8_t>(write_mode), 0, 0, 0};
			const std::array<SquirrelValue, 1> captures{vm.CreateUserData(descriptor)};
			const auto name = write_mode ? "setMaskMode" : "getMaskMode";
			const auto closure = vm.CreateNativeClosureWithCaptures(
			    std::string("System.") + name, [weak, descriptor, write_mode](SquirrelVm& active, auto) {
				    const auto owner = weak.lock();
				    if (!owner || &owner->Vm != &active) {
					    throw std::runtime_error("native System bindings have expired");
				    }
				    const auto payload = active.UserDataBytes(active.NativeStackValue(-1));
				    if (payload.size() != descriptor.size() || !std::equal(payload.begin(), payload.end(), descriptor.begin())) {
					    throw std::runtime_error("System mask-mode descriptor differs from its actual static operation");
				    }
				    if (!owner->WriteMaskMode || !owner->ReadMaskMode) {
					    throw std::runtime_error("System mask mode requires its actual shared graphics owner");
				    }
				    if (!write_mode) {
					    return SquirrelNativeResult::ReturnValue(std::bit_cast<std::int32_t>(owner->ReadMaskMode()));
				    }

				    // Native043510 initializes zero, reads stack2, ignores failed integer conversion and returns no values.
				    const auto value = TryNativeInteger(active.NativeAllocatedStackValue(2)).value_or(0);
				    owner->WriteMaskMode(static_cast<std::uint32_t>(value));

				    return SquirrelNativeResult::NoValues();
			    },
			    captures);
			vm.NewSlot(state.Class, std::string(name), closure, true);
		}
		method("getScreenBounds", [](Implementation& owner, auto) {
			if (owner.Width == 0 || owner.Height == 0) {
				throw std::runtime_error("System render-target extent has not been configured");
			}

			const auto half_width = static_cast<float>(owner.Width) * 0.5f;
			const auto half_height = static_cast<float>(owner.Height) * 0.5f;
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("left"), -half_width);
			owner.Vm.NewSlot(table, std::string("top"), -half_height);
			owner.Vm.NewSlot(table, std::string("right"), half_width);
			owner.Vm.NewSlot(table, std::string("bottom"), half_height);
			owner.Vm.NewSlot(table, std::string("width"), half_width - -half_width);
			owner.Vm.NewSlot(table, std::string("height"), half_height - -half_height);
			SquirrelNativeResult result;
			result.Value = table;

			return result;
		});
		method("getLocalDateTime", [](Implementation& owner, auto) {
			if (!owner.LocalDateTime) {
				throw std::runtime_error("System local date/time service is unavailable");
			}

			const auto time = owner.LocalDateTime();
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("sec"), time.Second);
			owner.Vm.NewSlot(table, std::string("min"), time.Minute);
			owner.Vm.NewSlot(table, std::string("hour"), time.Hour);
			owner.Vm.NewSlot(table, std::string("mday"), time.MonthDay);
			owner.Vm.NewSlot(table, std::string("mon"), time.Month);
			owner.Vm.NewSlot(table, std::string("year"), time.Year);
			owner.Vm.NewSlot(table, std::string("wday"), time.WeekDay);
			owner.Vm.NewSlot(table, std::string("yday"), time.YearDay);
			SquirrelNativeResult result;
			result.Value = table;

			return result;
		});
		method("setResourceCacheLimitSize", [](Implementation& owner, const auto arguments) {
			if (owner.Resources == nullptr) {
				throw std::runtime_error("System resource cache has not been configured");
			}

			// Sqrat 043510 initializes zero and ignores failed sq_getinteger conversion.
			const auto value = arguments.size() > 1 ? TryNativeInteger(arguments[1]).value_or(0) : 0;
			owner.Resources->SetLimit(static_cast<std::uint32_t>(value));

			return SquirrelNativeResult{};
		});
		method("getResourceCacheLimitSize", [](Implementation& owner, auto) {
			if (owner.Resources == nullptr) {
				throw std::runtime_error("System resource cache has not been configured");
			}

			SquirrelNativeResult result;
			result.Value = std::bit_cast<std::int32_t>(owner.Resources->Limit());

			return result;
		});
		method("clearResourceCache", [](Implementation& owner, auto) {
			if (owner.Resources == nullptr) {
				throw std::runtime_error("System resource cache has not been configured");
			}

			owner.Resources->ClearCache();

			return SquirrelNativeResult{};
		});
		method("getResourceState", [](Implementation& owner, auto) {
			if (owner.Resources == nullptr) {
				throw std::runtime_error("System resource cache has not been configured");
			}

			const auto state = owner.Resources->State();
			const auto table = owner.Vm.CreateTable();
			owner.Vm.NewSlot(table, std::string("cacheLimitSize"), std::bit_cast<std::int32_t>(state.CacheLimitSize));
			owner.Vm.NewSlot(table, std::string("totalActiveSize"), std::bit_cast<std::int32_t>(state.TotalActiveSize));
			owner.Vm.NewSlot(table, std::string("totalCacheSize"), std::bit_cast<std::int32_t>(state.TotalCacheSize));
			owner.Vm.NewSlot(table, std::string("loadingEntryNum"), std::bit_cast<std::int32_t>(state.LoadingEntryNum));
			owner.Vm.NewSlot(table, std::string("activeEntryNum"), std::bit_cast<std::int32_t>(state.ActiveEntryNum));
			owner.Vm.NewSlot(table, std::string("cacheEntryNum"), std::bit_cast<std::int32_t>(state.CacheEntryNum));
			SquirrelNativeResult result;
			result.Value = table;

			return result;
		});
		vm.NewSlot(vm.RootTable(), std::string("System"), state.Class);
		method("mountArchive", [](Implementation& owner, const auto arguments) {
			if (owner.Mounts == nullptr) {
				throw std::runtime_error("System archive mounts have not been configured");
			}

			if (arguments.size() < 4) {
				throw std::runtime_error("invalid argument.");
			}
			std::array<std::string_view, 3> names;
			for (std::size_t index = 0; index < names.size(); ++index) {
				const auto* text = TryStringText(arguments[index + 1]);
				if (text == nullptr) {
					throw std::runtime_error("invalid argument " + std::to_string(index + 1) + ".");
				}

				// Original285ca9 copies strings using strlen after sq_getstring succeeds.
				names[index] = std::string_view(*text).substr(0, text->find('\0'));
			}
			bool alternate = false;
			if (arguments.size() > 4) {
				const auto value = TryNativeInteger(arguments[4]);
				if (!value) {
					throw std::runtime_error("invalid argument 4.");
				}

				alternate = *value != 0;
			}
			if (owner.ResolveArchiveRead) {
				if (alternate) {
					throw std::runtime_error("System.mountArchive alternate physical read mode remains unrecovered");
				}
				auto input = owner.ResolveArchiveRead(names[0], names[1], names[2]);
				if (input.Prefix != names[0] || input.IndexIdentity != names[1] || input.BodyIdentity != names[2] || input.IndexRead.OriginalFilename != names[1] || input.BodyFilename.empty()) {
					throw std::runtime_error("System.mountArchive prepared physical input differs from its actual script request");
				}

				owner.Mounts->QueueReadMount(std::move(input));
			} else {
				owner.Mounts->QueueMount(std::string(names[0]), names[1], names[2], alternate);
			}
			SquirrelNativeResult result;
			result.Value = std::int32_t(0);

			return result;
		});
	}

	ScriptSystemBindings::~ScriptSystemBindings() = default;

	SquirrelValue ScriptSystemBindings::SystemClass() const {
		return m_Implementation->Class;
	}

	void ScriptSystemBindings::ConfigureLooperTime(core::LooperFrame& actual_looper) {
		auto& owner = *m_Implementation;
		if (owner.Looper != nullptr || &actual_looper.Clock() != &owner.Clock) {
			throw std::runtime_error("System time requires its exact once-configured containing Looper clock");
		}
		static_cast<void>(actual_looper.AccumulatedTicks());
		owner.Looper = &actual_looper;
	}

	void ScriptSystemBindings::ConfigureApplicationClose(core::ApplicationEventControl& actual_events) {
		auto& owner = *m_Implementation;
		actual_events.RequireCanFlush();
		if (owner.ApplicationEvents != nullptr || owner.Vm.IsClosed()) {
			throw std::runtime_error("System application close service requires one live containing event owner");
		}

		owner.ApplicationEvents = &actual_events;
	}

	void ScriptSystemBindings::ConfigureProcessTermination(ScriptProcessTerminationServices services) {
		const auto owner = m_Implementation;
		if (std::this_thread::get_id() != owner->OwnerThread || owner->Vm.IsClosed()) {
			throw std::runtime_error("System process termination configuration requires its live serialized binding owner");
		}
		const SystemTerminationCall call(owner->TerminationOperating);
		if (owner->ProcessTermination || !services.RequireLive || !services.TerminateProcess) {
			throw std::runtime_error("System process termination configuration requires one live owner and both actual services");
		}
		services.RequireLive();
		if (owner->Vm.IsClosed()) {
			throw std::runtime_error("System process termination preflight retired its actual VM");
		}

		owner->ProcessTermination.emplace(std::move(services));
	}

	void ScriptSystemBindings::ConfigureRenderTarget(std::uint32_t width, std::uint32_t height) {
		if (width == 0 || height == 0 || width > 16384 || height > 16384) {
			throw std::runtime_error("System render-target extent requires bounded positive dimensions");
		}

		m_Implementation->Width = width;
		m_Implementation->Height = height;
	}

	void ScriptSystemBindings::ConfigureMaskMode(std::function<void(std::uint32_t)> write_mode, std::function<std::uint32_t()> read_mode) {
		auto& state = *m_Implementation;
		if (!write_mode || !read_mode || state.WriteMaskMode || state.ReadMaskMode) {
			throw std::runtime_error("System mask-mode service requires one complete actual graphics owner");
		}

		state.WriteMaskMode = std::move(write_mode);
		state.ReadMaskMode = std::move(read_mode);
	}

	void ScriptSystemBindings::ConfigureResourceCache(resource::ResourceCache& cache) {
		m_Implementation->Resources = &cache;
	}

	void ScriptSystemBindings::ConfigureDiskFileExistence(DiskFileExistence service) {
		auto& owner = *m_Implementation;
		if (std::this_thread::get_id() != owner.OwnerThread || owner.Vm.IsClosed() || owner.DiskFileExists || !service) {
			throw std::runtime_error("native disk file existence configuration requires one live serialized service");
		}

		owner.DiskFileExists = std::move(service);
	}

	void ScriptSystemBindings::ConfigureArchiveMounts(resource::ArchiveMounts& mounts) {
		m_Implementation->Mounts = &mounts;
		m_Implementation->ResolveArchiveRead = {};
	}

	void ScriptSystemBindings::ConfigureArchiveReadMounts(resource::ArchiveMounts& mounts, ArchiveReadMountResolver resolver) {
		if (!resolver) {
			throw std::runtime_error("System actual archive read mount configuration requires a prepared-input resolver");
		}

		m_Implementation->ResolveArchiveRead = std::move(resolver);
		m_Implementation->Mounts = &mounts;
	}

	void ScriptSystemBindings::ConfigureLocale(core::OriginalLocale locale) {
		m_Implementation->Locale = core::ResolveOriginalHostLocale(locale.Language, locale.Country);
	}

} // namespace sgre::script
