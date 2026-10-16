#include "sgre/script/capture_bindings.hpp"

#include "sgre/scene/layer_base_frame.hpp"
#include "sgre/script/layer_bindings.hpp"
#include "sgre/script/raw_image_bindings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <stdexcept>
#include <utility>
#include <variant>

namespace sgre::script {
	enum class CaptureMethod : std::uint32_t {
		GetWidth = 1,
		GetHeight,
		SetOffset,
		StoreThumbnail
	};

	static std::array<std::uint8_t, 16> CaptureMetadata(CaptureMethod method) {
		const auto word = static_cast<std::uint32_t>(method);
		return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8), static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	}

	static void RequireCaptureMetadata(SquirrelVm& vm, CaptureMethod method) {
		const auto captured = vm.NativeStackValue(-1);
		if (vm.Type(captured) != SquirrelValueType::UserData) {
			throw std::runtime_error("Capture member requires its genuine appended native method capture");
		}
		const auto bytes = vm.UserDataBytes(captured);
		const auto expected = CaptureMetadata(method);
		if (bytes.size() != expected.size() || !std::equal(bytes.begin(), bytes.end(), expected.begin())) {
			throw std::runtime_error("Capture native member capture identity is invalid");
		}
	}

	static float CaptureFloat(const SquirrelValue& value) {
		if (const auto* number = std::get_if<float>(&value)) {
			return *number;
		}
		if (const auto* number = std::get_if<std::int32_t>(&value)) {
			return static_cast<float>(*number);
		}
		if (const auto* boolean = std::get_if<bool>(&value)) {
			return *boolean ? 1.0f : 0.0f;
		}
		return 0;
	}

	struct CaptureRecord {
		SquirrelObjectIdentity Instance;
		std::shared_ptr<scene::CaptureTask> Task;
	};

	static std::uint32_t ThumbnailExtent(std::uint32_t dimension, float scale) {
		const auto scaled = static_cast<float>(dimension) * scale;
		if (!std::isfinite(scale) || scale < 1 || !std::isfinite(scaled) || scaled < 1 || scaled >= 16385) {
			throw std::runtime_error("Capture thumbnail scale requires a bounded nonshrinking intermediate image");
		}
		return static_cast<std::uint32_t>(scaled);
	}

	static std::vector<std::uint8_t> ReduceThumbnail(std::span<const std::uint8_t> pixels, std::uint32_t source_width, std::uint32_t source_height, std::uint32_t width, std::uint32_t height) {
		std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4);
		for (std::uint32_t y = 0; y < height; ++y) {
			const auto first_y = static_cast<std::uint64_t>(y) * source_height / height;
			const auto last_y = static_cast<std::uint64_t>(y + 1) * source_height / height;
			for (std::uint32_t x = 0; x < width; ++x) {
				const auto first_x = static_cast<std::uint64_t>(x) * source_width / width;
				const auto last_x = static_cast<std::uint64_t>(x + 1) * source_width / width;
				std::array<std::uint64_t, 4> sums{};
				for (auto row = first_y; row < last_y; ++row) {
					for (auto column = first_x; column < last_x; ++column) {
						const auto offset = static_cast<std::size_t>((row * source_width + column) * 4);
						for (std::size_t channel = 0; channel < 4; ++channel) {
							sums[channel] += pixels[offset + channel];
						}
					}
				}
				const auto count = (last_y - first_y) * (last_x - first_x);
				const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
				for (std::size_t channel = 0; channel < 4; ++channel) {
					result[offset + channel] = static_cast<std::uint8_t>(sums[channel] / count);
				}
			}
		}
		return result;
	}

	class ScriptCaptureBindings::Implementation {
	public:
		Implementation(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureLimits limits)
		    : Vm(vm), Objects(objects), Layers(layers), Limits(limits) {
			if (limits.MaxInstances == 0 || limits.MaxInstances > 65536) {
				throw std::runtime_error("Capture requires bounded actual script wrapper capacity");
			}
		}

		const CaptureRecord& Record(const SquirrelValue& instance) const {
			const auto identity = Vm.BorrowObjectIdentity(instance);
			const auto found = Records.find(identity.Index);
			if (found == Records.end() || found->second.Instance != identity) {
				throw std::runtime_error("Capture receiver requires its actual live native Layer wrapper");
			}
			return found->second;
		}

		void Construct(std::span<const SquirrelValue> arguments, const std::weak_ptr<Implementation>& weak) {
			if (arguments.empty() || !Vm.IsInstanceOf(arguments[0], Class)) {
				throw std::runtime_error("Capture constructor requires its genuine derived receiver");
			}
			const auto receiver = arguments[0];
			std::size_t first = 1;
			SquirrelValue parent;
			if (arguments.size() > first && Layers.IsLayerOwner(arguments[first])) {
				parent = arguments[first++];
			}
			if (arguments.size() - first != 2) {
				throw std::runtime_error("Capture constructor requires its actual optional owner and two dimensions");
			}
			const auto width = TryNativeInteger(arguments[first]);
			const auto height = TryNativeInteger(arguments[first + 1]);
			if (!width || !height) {
				throw std::runtime_error("Capture dimensions require their actual native integer conversion");
			}
			const auto identity = Vm.BorrowObjectIdentity(receiver);
			if (Backend == nullptr || !Drawing) {
				throw std::runtime_error("Capture constructor requires its actual enclosing graphics frame services");
			}
			if (Records.contains(identity.Index) || Objects.HasInstance(receiver) || Records.size() == Limits.MaxInstances) {
				throw std::runtime_error("Capture receiver is initialized or bounded wrapper capacity is exhausted");
			}
			auto task = std::make_shared<scene::CaptureTask>(std::bit_cast<std::uint32_t>(*width), std::bit_cast<std::uint32_t>(*height), Limits.Tasks);
			const std::array<SquirrelValue, 2> base_arguments{receiver, parent};
			bool attached = false;
			try {
				Layers.ConstructBaseInstance(base_arguments);
				attached = true;
				scene::ParallelTaskCallbacks callbacks;
				callbacks.Draw = [weak, task](scene::ParallelTaskHandle, std::uint32_t) {
					const auto owner = weak.lock();
					if (!owner || owner->Backend == nullptr || !owner->Drawing) {
						throw std::runtime_error("Capture drawing lost its actual enclosing graphics frame owner");
					}
					owner->Drawing(task);
				};
				callbacks.Release = [task](scene::ParallelTaskHandle) {
					task->Close();
				};
				// Native28dc40 attaches MCapture to22c360's actual CommonFirst parallel, before clip/camera.
				static_cast<void>(Layers.BaseTasks(receiver).CommonFirst().Append(std::move(callbacks)));
				Records.emplace(identity.Index, CaptureRecord{identity, task});
				Layers.ConfigureBaseRetirement(
				    receiver, [weak](const auto& actual_receiver) {
					    if (const auto owner = weak.lock(); owner && !owner->Record(actual_receiver).Task->CanClose()) {
						    throw std::runtime_error("Capture release cannot interrupt its actual frame copy or retained resource inputs");
					    }
				    },
				    [weak](const auto& actual_receiver) {
					    if (const auto owner = weak.lock()) {
						    const auto actual = owner->Vm.BorrowObjectIdentity(actual_receiver);
						    owner->Records.erase(actual.Index);
					    }
				    });
				EverConstructed = true;
			} catch (...) {
				const auto failure = std::current_exception();
				if (attached) {
					try {
						Layers.ReleaseInstance(receiver);
					} catch (...) {
					}
				}
				Records.erase(identity.Index);
				std::rethrow_exception(failure);
			}
		}

		void StoreThumbnail(std::span<const SquirrelValue> arguments) {
			if (arguments.size() != 2 && arguments.size() != 3) {
				throw std::runtime_error("Capture.storeThumbnail requires a RawImage and optional scale; typed-array output still requires its recovered mutable owner");
			}
			if (Images == nullptr || !ReadingThumbnail) {
				throw std::runtime_error("Capture.storeThumbnail requires its actual image and staging readback services");
			}
			const auto& image = Images->Image(arguments[1]);
			const auto scale = arguments.size() == 3 ? CaptureFloat(arguments[2]) : 1.0f;
			const auto width = ThumbnailExtent(image.Width(), scale);
			const auto height = ThumbnailExtent(image.Height(), scale);
			static constexpr std::size_t kMaximumThumbnailBytes = 64 * 1024 * 1024;
			if (width > kMaximumThumbnailBytes / 4 / height) {
				throw std::runtime_error("Capture thumbnail intermediate exceeds its bounded pixel storage");
			}
			const auto task = Record(arguments[0]).Task;
			if (task->State().FramePending) {
				throw std::runtime_error("Capture thumbnail requires the preceding frame to be submitted before readback");
			}
			auto pixels = ReadingThumbnail(task->Texture(), width, height);
			if (pixels.size() != static_cast<std::size_t>(width) * height * 4) {
				throw std::runtime_error("Capture thumbnail readback returned a truncated actual image");
			}
			if (scale == 1) {
				Images->StoreThumbnail(arguments[1], pixels);
			} else {
				Images->StoreThumbnail(arguments[1], ReduceThumbnail(pixels, width, height, image.Width(), image.Height()));
			}
		}

		SquirrelNativeResult Invoke(CaptureMethod method, std::span<const SquirrelValue> arguments) {
			const auto task = Record(arguments[0]).Task;
			switch (method) {
			case CaptureMethod::GetWidth:
				return SquirrelNativeResult::ReturnValue(task->State().FixedSize.X);
			case CaptureMethod::GetHeight:
				return SquirrelNativeResult::ReturnValue(task->State().FixedSize.Y);
			case CaptureMethod::SetOffset:
				task->SetOffset({CaptureFloat(Vm.NativeAllocatedStackValue(2)), CaptureFloat(Vm.NativeAllocatedStackValue(3))});
				return SquirrelNativeResult::NoValues();
			case CaptureMethod::StoreThumbnail:
				// Native078ed0 removes the captured member descriptor before the raw-VM target28f170.
				Vm.SetNativeStackSize(Vm.NativeStackSize() - 1);
				StoreThumbnail(arguments.first(arguments.size() - 1));
				return SquirrelNativeResult::NoValues();
			}
			throw std::runtime_error("Capture member has no recovered native dispatch");
		}

		SquirrelVm& Vm;
		ScriptObjectBindings& Objects;
		ScriptLayerBindings& Layers;
		ScriptCaptureLimits Limits;
		SquirrelValue Class;
		SquirrelHostRoot ClassRoot;
		scene::MutableTextureBackend* Backend = nullptr;
		DrawCapture Drawing;
		ScriptRawImageBindings* Images = nullptr;
		ReadThumbnail ReadingThumbnail;
		std::map<std::uint32_t, CaptureRecord> Records;
		bool EverConstructed = false;
	};

	ScriptCaptureBindings::ScriptCaptureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptCaptureLimits limits)
	    : m_Implementation(std::make_shared<Implementation>(vm, objects, layers, limits)) {
		auto& state = *m_Implementation;
		state.Class = vm.CreateClass(layers.LayerBaseClass());
		state.ClassRoot = vm.Retain(state.Class);
		const auto weak = std::weak_ptr<Implementation>(m_Implementation);
		vm.NewSlot(state.Class, std::string("constructor"), vm.CreateNativeClosure("Capture.constructor", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner) {
				throw std::runtime_error("Capture constructor services expired");
			}
			owner->Construct(arguments, weak);
			return SquirrelNativeResult::NoValues();
		}));
		vm.NewSlot(state.Class, std::string("weakref"), vm.CreateNativeClosure("Capture.weakref", [weak](SquirrelVm&, auto arguments) {
			const auto owner = weak.lock();
			if (!owner || arguments.empty()) {
				throw std::runtime_error("Capture weakref requires its live owner and final physical argument");
			}
			return SquirrelNativeResult::ReturnValue(owner->Vm.CreateWeakReference(arguments.back()));
		}));
		const auto method = [&](const char* name, CaptureMethod identity) {
			const std::array<SquirrelValue, 1> capture{vm.CreateUserData(CaptureMetadata(identity))};
			vm.NewSlot(state.Class, std::string(name), vm.CreateNativeClosureWithCaptures(std::string("Capture.") + name, [weak, identity](SquirrelVm& active, auto arguments) {
				const auto owner = weak.lock();
				if (!owner || arguments.empty()) {
					throw std::runtime_error("Capture member requires its actual owner and original receiver");
				}
				RequireCaptureMetadata(active, identity);
				return owner->Invoke(identity, arguments);
			},
			                                               capture));
		};
		method("getWidth", CaptureMethod::GetWidth);
		method("getHeight", CaptureMethod::GetHeight);
		method("setOffset", CaptureMethod::SetOffset);
		method("storeThumbnail", CaptureMethod::StoreThumbnail);
		vm.NewSlot(vm.RootTable(), std::string("Capture"), state.Class);
	}

	ScriptCaptureBindings::~ScriptCaptureBindings() = default;

	void ScriptCaptureBindings::ConfigureFrameOwners(scene::MutableTextureBackend& actual_backend, DrawCapture actual_drawing) {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed() || state.Backend != nullptr || !actual_drawing || state.EverConstructed || !state.Records.empty()) {
			throw std::runtime_error("Capture frame owners require one actual configuration before construction");
		}
		state.Backend = &actual_backend;
		state.Drawing = std::move(actual_drawing);
	}

	SquirrelValue ScriptCaptureBindings::CaptureClass() const {
		return m_Implementation->Class;
	}

	void ScriptCaptureBindings::ConfigureThumbnailReadback(ScriptRawImageBindings& actual_images, ReadThumbnail actual_readback) {
		auto& state = *m_Implementation;
		if (state.Vm.IsClosed() || state.Images != nullptr || !actual_readback || state.EverConstructed) {
			throw std::runtime_error("Capture thumbnail services require one actual configuration before construction");
		}
		state.Images = &actual_images;
		state.ReadingThumbnail = std::move(actual_readback);
	}

	std::shared_ptr<scene::CaptureTask> ScriptCaptureBindings::BorrowTask(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Task;
	}

	scene::CaptureTaskState ScriptCaptureBindings::State(const SquirrelValue& instance) const {
		return m_Implementation->Record(instance).Task->State();
	}

	void ScriptCaptureBindings::ReleaseInstance(const SquirrelValue& instance) {
		static_cast<void>(m_Implementation->Record(instance));
		m_Implementation->Layers.ReleaseInstance(instance);
	}
} // namespace sgre::script
