#include "sgre/platform/macos_pcm_output.hpp"

#include "sgre/platform/host_performance_counter.hpp"

#include "pcm_output_callback.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace sgre::platform {
	static void RequirePcmAudioStatus(OSStatus status, const char* operation) {
		if (status != noErr) {
			throw std::runtime_error(std::string(operation) + " failed with CoreAudio status " + std::to_string(status));
		}
	}

	static MacPcmOutputDescription ValidatePcmOutputDescription(MacPcmOutputDescription description) {
		if (description.SamplesPerSecond < 8000 || description.SamplesPerSecond > 192000 || description.Channels == 0 || description.Channels > 8 || description.CapacityFrames == 0 || description.CapacityFrames > 262144 || description.TargetBufferedFrames == 0 || description.TargetBufferedFrames > description.CapacityFrames || description.MaximumCallbackFrames == 0 || description.MaximumCallbackFrames > description.CapacityFrames || description.TargetBufferedFrames < description.MaximumCallbackFrames) {
			throw std::runtime_error("macOS PCM output requires explicit bounded sample/channel/ring geometry");
		}
		if ((description.DeliveryMode != MacPcmDeliveryMode::None && description.DeliveryMode != MacPcmDeliveryMode::CallbackEvents) || (description.DeliveryMode == MacPcmDeliveryMode::CallbackEvents && (description.DeliveryCapacity == 0 || description.DeliveryCapacity > 65536))) {
			throw std::runtime_error("macOS PCM callback receipt mode/capacity is unsupported");
		}
		return description;
	}

	static audio::PcmOutputRingDescription PcmDeviceRingDescription(const MacPcmOutputDescription& description) {
		audio::PcmOutputRingDescription ring;
		ring.CapacityFrames = description.CapacityFrames;
		ring.MaximumTransferFrames = description.MaximumCallbackFrames;
		ring.Channels = description.Channels;
		return ring;
	}

	class MacPcmOutput::Implementation {
	public:
		explicit Implementation(MacPcmOutputDescription description) : Description(ValidatePcmOutputDescription(description)), Callback(PcmDeviceRingDescription(Description), Description.DeliveryMode == MacPcmDeliveryMode::CallbackEvents, Description.DeliveryCapacity) {
			try {
				if (Description.DeliveryMode == MacPcmDeliveryMode::CallbackEvents) {
					ContinuousFrequency = HostPerformanceCounter().Frequency();
				}
				Submissions.emplace(Callback.Ring, Callback.Deliveries ? &*Callback.Deliveries : nullptr, Callback.Enabled, Callback.ActiveCallbacks, ContinuousFrequency);
				Prepare();
			} catch (...) {
				const auto failure = std::current_exception();
				try {
					Release();
				} catch (...) {
					if (!Closed) {
						std::terminate();
					}
				}
				std::rethrow_exception(failure);
			}
		}
		~Implementation() noexcept {
			if (!Closed) {
				try {
					Release();
				} catch (...) {
					// A failed disposal cannot release a callback refcon still owned by the system.
					if (!Closed) {
						std::terminate();
					}
				}
			}
		}

		void RequireOwner() const {
			if (Closed || std::this_thread::get_id() != Owner) {
				throw std::runtime_error("macOS PCM output requires its live serialized producer owner");
			}
		}

		static void PropertyChanged(void* refcon, AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement) noexcept {
			auto& owner = *static_cast<Implementation*>(refcon);
			owner.Callback.ActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
			owner.PropertiesChanged.store(true, std::memory_order_release);
			owner.Callback.ActiveCallbacks.fetch_sub(1, std::memory_order_release);
		}

		AudioStreamBasicDescription ClientFormat() const {
			AudioStreamBasicDescription format{};
			format.mSampleRate = Description.SamplesPerSecond;
			format.mFormatID = kAudioFormatLinearPCM;
			format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
			format.mBytesPerPacket = static_cast<UInt32>(Description.Channels * sizeof(float));
			format.mFramesPerPacket = 1;
			format.mBytesPerFrame = static_cast<UInt32>(Description.Channels * sizeof(float));
			format.mChannelsPerFrame = Description.Channels;
			format.mBitsPerChannel = 32;
			return format;
		}

		void ReadFormatAndDevice() {
			AudioStreamBasicDescription input{};
			UInt32 size = sizeof(input);
			RequirePcmAudioStatus(AudioUnitGetProperty(Unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &input, &size), "read PCM output client format");
			const auto requested = ClientFormat();
			if (size != sizeof(input) || input.mSampleRate != requested.mSampleRate || input.mFormatID != requested.mFormatID || input.mFormatFlags != requested.mFormatFlags || input.mBytesPerPacket != requested.mBytesPerPacket || input.mFramesPerPacket != 1 || input.mBytesPerFrame != requested.mBytesPerFrame || input.mChannelsPerFrame != requested.mChannelsPerFrame || input.mBitsPerChannel != 32) {
				throw std::runtime_error("macOS output unit changed the explicit Float32 interleaved client format");
			}
			AudioStreamBasicDescription hardware{};
			size = sizeof(hardware);
			RequirePcmAudioStatus(AudioUnitGetProperty(Unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &hardware, &size), "read PCM output device format");
			AudioObjectID device = kAudioObjectUnknown;
			UInt32 device_size = sizeof(device);
			RequirePcmAudioStatus(AudioUnitGetProperty(Unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, &device_size), "read PCM output device identity");
			if (size != sizeof(hardware) || hardware.mFormatID != kAudioFormatLinearPCM || !std::isfinite(hardware.mSampleRate) || !(hardware.mSampleRate > 0) || hardware.mChannelsPerFrame == 0 || device_size != sizeof(device) || device == kAudioObjectUnknown) {
				throw std::runtime_error("macOS default output has no valid linear PCM hardware route");
			}
			DeviceIdentity = device;
			DeviceSamplesPerSecond = hardware.mSampleRate;
			DeviceChannels = hardware.mChannelsPerFrame;
		}

		void ReadMaximumFrames() {
			UInt32 maximum = 0;
			UInt32 size = sizeof(maximum);
			RequirePcmAudioStatus(AudioUnitGetProperty(Unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, &size), "read PCM output callback bound");
			if (size != sizeof(maximum) || maximum == 0 || maximum > Description.MaximumCallbackFrames || maximum > Description.CapacityFrames || maximum > Description.TargetBufferedFrames) {
				throw std::runtime_error("macOS output unit callback bound exceeds its preallocated ring, target or transfer extent");
			}
		}

		void Prepare() {
			AudioComponentDescription component_description{};
			component_description.componentType = kAudioUnitType_Output;
			component_description.componentSubType = kAudioUnitSubType_DefaultOutput;
			component_description.componentManufacturer = kAudioUnitManufacturer_Apple;
			const auto component = AudioComponentFindNext(nullptr, &component_description);
			if (component == nullptr) {
				throw std::runtime_error("macOS default output AudioUnit is unavailable");
			}
			RequirePcmAudioStatus(AudioComponentInstanceNew(component, &Unit), "create default PCM output unit");
			const auto format = ClientFormat();
			RequirePcmAudioStatus(AudioUnitSetProperty(Unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "configure PCM output client format");
			const UInt32 maximum = Description.MaximumCallbackFrames;
			RequirePcmAudioStatus(AudioUnitSetProperty(Unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, sizeof(maximum)), "configure PCM output callback bound");
			AURenderCallbackStruct callback{};
			callback.inputProc = RenderPcmDeviceOutput;
			callback.inputProcRefCon = &Callback;
			RequirePcmAudioStatus(AudioUnitSetProperty(Unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "install PCM device callback");
			CallbackInstalled = true;
			RequirePcmAudioStatus(AudioUnitAddPropertyListener(Unit, kAudioUnitProperty_StreamFormat, PropertyChanged, this), "observe PCM device format");
			FormatListener = true;
			RequirePcmAudioStatus(AudioUnitAddPropertyListener(Unit, kAudioOutputUnitProperty_CurrentDevice, PropertyChanged, this), "observe PCM default device");
			DeviceListener = true;
			RequirePcmAudioStatus(AudioUnitAddPropertyListener(Unit, kAudioUnitProperty_MaximumFramesPerSlice, PropertyChanged, this), "observe PCM callback bound");
			MaximumFramesListener = true;
			RequirePcmAudioStatus(AudioUnitInitialize(Unit), "initialize PCM default output");
			Initialized = true;
			RequirePcmAudioStatus(AudioUnitAddPropertyListener(Unit, kAudioUnitProperty_LastRenderError, PropertyChanged, this), "observe PCM render errors");
			RenderErrorListener = true;
			ReadMaximumFrames();
			ReadFormatAndDevice();
		}

		void RequireHealthy() {
			RequireOwner();
			if (OwnerFailure) {
				std::rethrow_exception(OwnerFailure);
			}
			try {
				const auto failure = Callback.Failure.load(std::memory_order_acquire);
				if (failure != PcmDeviceCallbackFailure::None) {
					throw std::runtime_error("macOS PCM device callback failed, reason " + std::to_string(static_cast<std::uint32_t>(failure)));
				}
				if (PropertiesChanged.exchange(false, std::memory_order_acq_rel)) {
					// The default output unit owns hardware conversion/route following; no device setting is changed here.
					ReadMaximumFrames();
					ReadFormatAndDevice();
					OSStatus render_status = noErr;
					UInt32 status_size = sizeof(render_status);
					RequirePcmAudioStatus(AudioUnitGetProperty(Unit, kAudioUnitProperty_LastRenderError, kAudioUnitScope_Global, 0, &render_status, &status_size), "read PCM last render error");
					if (status_size != sizeof(render_status)) {
						throw std::runtime_error("macOS output render error property has an unexpected extent");
					}
					RequirePcmAudioStatus(render_status, "PCM output render");
				}
			} catch (...) {
				// A diagnostic Snapshot must not consume the only notification and then permit unsafe service.
				OwnerFailure = std::current_exception();
				throw;
			}
		}

		void Release(const media::MoviePcmDelivery* movie_owner = nullptr) {
			if (MovieOwner != movie_owner) {
				throw std::runtime_error("macOS PCM disposal must retain its actual Movie delivery binding");
			}
			if (Closed) {
				return;
			}
			if (std::this_thread::get_id() != Owner) {
				throw std::runtime_error("macOS PCM disposal requires its actual producer thread");
			}
			Callback.Enabled.store(false, std::memory_order_release);
			OSStatus first_failure = noErr;
			const char* first_operation = "PCM output disposal";
			const auto retain_failure = [&](OSStatus status, const char* operation) {
				if (status != noErr && first_failure == noErr) {
					first_failure = status;
					first_operation = operation;
				}
			};
			if (Unit != nullptr) {
				if (Started) {
					const auto status = AudioOutputUnitStop(Unit);
					retain_failure(status, "stop PCM default output");
					Started = status != noErr;
				}
				// SDK reconfiguration order: stop, uninitialize, then change/detach properties.
				if (Initialized) {
					const auto status = AudioUnitUninitialize(Unit);
					retain_failure(status, "uninitialize PCM default output");
					Initialized = status != noErr;
				}
				if (FormatListener) {
					retain_failure(AudioUnitRemovePropertyListenerWithUserData(Unit, kAudioUnitProperty_StreamFormat, PropertyChanged, this), "remove PCM format listener");
				}
				if (DeviceListener) {
					retain_failure(AudioUnitRemovePropertyListenerWithUserData(Unit, kAudioOutputUnitProperty_CurrentDevice, PropertyChanged, this), "remove PCM device listener");
				}
				if (MaximumFramesListener) {
					retain_failure(AudioUnitRemovePropertyListenerWithUserData(Unit, kAudioUnitProperty_MaximumFramesPerSlice, PropertyChanged, this), "remove PCM callback bound listener");
				}
				if (RenderErrorListener) {
					retain_failure(AudioUnitRemovePropertyListenerWithUserData(Unit, kAudioUnitProperty_LastRenderError, PropertyChanged, this), "remove PCM render error listener");
				}
				if (CallbackInstalled) {
					AURenderCallbackStruct empty{};
					retain_failure(AudioUnitSetProperty(Unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &empty, sizeof(empty)), "detach PCM device callback");
				}
				const auto disposed = AudioComponentInstanceDispose(Unit);
				retain_failure(disposed, "dispose PCM default output");
				if (disposed != noErr) {
					RequirePcmAudioStatus(first_failure, first_operation);
				}
				Unit = nullptr;
				Started = false;
				Initialized = false;
			}
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			while (Callback.ActiveCallbacks.load(std::memory_order_acquire) != 0) {
				if (std::chrono::steady_clock::now() >= deadline) {
					throw std::runtime_error("disposed PCM device retained an active callback; refcon cannot retire");
				}
				std::this_thread::yield();
			}
			if (Submissions) {
				if (movie_owner != nullptr) {
					Submissions->CloseMovie(movie_owner);
				} else {
					Submissions->Close();
				}
			}
			Closed = true;
			RequirePcmAudioStatus(first_failure, first_operation);
		}

		const media::MoviePcmDelivery* MovieOwner = nullptr;
		const MacPcmOutputDescription Description;
		PcmDeviceCallbackState Callback;
		const std::thread::id Owner = std::this_thread::get_id();
		AudioUnit Unit = nullptr;
		std::atomic<bool> PropertiesChanged = false;
		std::exception_ptr OwnerFailure;
		std::optional<audio::PcmOutputSubmissionOwner> Submissions;
		std::int64_t ContinuousFrequency = 0;
		std::uint32_t DeviceIdentity = 0;
		double DeviceSamplesPerSecond = 0;
		std::uint32_t DeviceChannels = 0;
		bool CallbackInstalled = false;
		bool FormatListener = false;
		bool DeviceListener = false;
		bool MaximumFramesListener = false;
		bool RenderErrorListener = false;
		bool Initialized = false;
		bool Started = false;
		bool InputFinished = false;
		bool Closed = false;
	};

	MacPcmOutput::MacPcmOutput(MacPcmOutputDescription description) : m_Implementation(std::make_unique<Implementation>(description)) {
	}
	MacPcmOutput::~MacPcmOutput() noexcept = default;
	MacPcmOutputDescription MacPcmOutput::Description() const noexcept {
		return m_Implementation->Description;
	}
	std::size_t MacPcmOutput::FramesToTarget() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		const auto buffered = owner.Callback.Ring.BufferedFrames();
		return owner.InputFinished || buffered >= owner.Description.TargetBufferedFrames ? 0 : owner.Description.TargetBufferedFrames - buffered;
	}
	audio::PcmOutputSubmission MacPcmOutput::Submit(std::span<const float> interleaved) {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();

		return owner.Submissions->Submit(interleaved);
	}
	void MacPcmOutput::Start() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		if (owner.Started || owner.InputFinished || owner.Callback.Ring.BufferedFrames() < owner.Description.TargetBufferedFrames) {
			throw std::runtime_error("macOS PCM start requires one actual prefilled live ring");
		}
		owner.Callback.Enabled.store(true, std::memory_order_release);
		const auto status = AudioOutputUnitStart(owner.Unit);
		if (status != noErr) {
			owner.Callback.Enabled.store(false, std::memory_order_release);
			RequirePcmAudioStatus(status, "start PCM default output");
		}
		owner.Started = true;
	}
	void MacPcmOutput::FinishInput() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		owner.Submissions->FinishInput();
		owner.InputFinished = true;
	}
	MacPcmOutputSnapshot MacPcmOutput::Snapshot() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		MacPcmOutputSnapshot result;
		result.SubmittedFrames = owner.Submissions->SubmittedFrames();
		result.RequestedFrames = owner.Callback.RequestedFrames.load(std::memory_order_relaxed);
		result.CopiedFrames = owner.Callback.CopiedFrames.load(std::memory_order_relaxed);
		result.SilentFrames = owner.Callback.SilentFrames.load(std::memory_order_relaxed);
		result.BufferedFrames = owner.Callback.Ring.BufferedFrames();
		result.DeviceIdentity = owner.DeviceIdentity;
		result.DeviceSamplesPerSecond = owner.DeviceSamplesPerSecond;
		result.DeviceChannels = owner.DeviceChannels;
		result.Started = owner.Started;
		result.InputFinished = owner.InputFinished;
		return result;
	}
	std::optional<audio::PcmOutputDeliveryReceipt> MacPcmOutput::ReadDelivery() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();

		return owner.Submissions->ReadDelivery();
	}
	void MacPcmOutput::RequireSubmission(const audio::PcmOutputSubmission& submission) {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		owner.Submissions->RequireSubmission(submission);
	}
	void MacPcmOutput::RequireDelivery(const audio::PcmOutputDeliveryReceipt& delivery) {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		owner.Submissions->RequireDelivery(delivery);
	}
	std::int64_t MacPcmOutput::ContinuousClockFrequency() {
		auto& owner = *m_Implementation;
		owner.RequireHealthy();
		if (!owner.Callback.Deliveries || owner.ContinuousFrequency <= 0) {
			throw std::runtime_error("macOS PCM delivery clock lacks its actual continuous-counter setup");
		}

		return owner.ContinuousFrequency;
	}
	void MacPcmOutput::RequireMovieAdmission(const media::MoviePcmDelivery* owner) {
		auto& actual = *m_Implementation;
		actual.RequireHealthy();
		if (actual.MovieOwner != nullptr || actual.Description.DeliveryMode != MacPcmDeliveryMode::CallbackEvents || actual.Description.SamplesPerSecond != 48000 || actual.Description.Channels != 2) {
			throw std::runtime_error("Movie hardware binding requires the actual unbound48k stereo receipt-enabled output");
		}
		actual.Submissions->RequireMovieAdmission(owner);
	}
	void MacPcmOutput::BindMovie(const media::MoviePcmDelivery* owner) noexcept {
		m_Implementation->Submissions->BindMovie(owner);
		m_Implementation->MovieOwner = owner;
	}
	audio::PcmOutputSubmission MacPcmOutput::SubmitMovie(const media::MoviePcmDelivery* owner, std::span<const float> interleaved) {
		m_Implementation->RequireHealthy();
		return m_Implementation->Submissions->SubmitMovie(owner, interleaved);
	}
	std::optional<audio::PcmOutputDeliveryReceipt> MacPcmOutput::ReadMovieDelivery(const media::MoviePcmDelivery* owner) {
		m_Implementation->RequireHealthy();
		return m_Implementation->Submissions->ReadMovieDelivery(owner);
	}
	void MacPcmOutput::CloseMovie(const media::MoviePcmDelivery* owner) {
		m_Implementation->Release(owner);
	}
	bool MacPcmOutput::MovieRetired() const noexcept {
		return m_Implementation->Closed;
	}
	void MacPcmOutput::ReleaseMovie(const media::MoviePcmDelivery* owner) noexcept {
		m_Implementation->Submissions->ReleaseMovie(owner);
		m_Implementation->MovieOwner = nullptr;
	}
	void MacPcmOutput::Close() {
		m_Implementation->Release();
	}
} // namespace sgre::platform
