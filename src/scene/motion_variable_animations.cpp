#include "sgre/scene/motion_variable_animations.hpp"

#include "sgre/scene/motion_manager_runtime.hpp"
#include "sgre/scene/motion_player_task.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sgre::scene {
	static constexpr std::size_t kMaximumVariableAnimations = 1024;
	static constexpr std::int32_t kMaximumAnimationCurve = 64;
	static constexpr std::int32_t kMaximumAnimationDuration = std::numeric_limits<std::int32_t>::max() / 16;

	static std::uint32_t AnimationPower(std::uint32_t value, std::uint32_t factor, std::uint32_t denominator, std::uint32_t iterations) {
		if (denominator == 0) {
			throw std::runtime_error("motion variable animation curve has no actual integer interval");
		}
		for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
			// Native5358a0 IMUL EAX truncates to32bits, zero-extends RAX, then performs signed64 IDIV.
			const std::uint32_t product = value * factor;
			value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) / denominator);
		}

		return value;
	}

	static std::uint32_t AnimationOrdinal(std::uint32_t ordinal, std::uint32_t count, std::int32_t curve) {
		if (curve == 0) {
			return ordinal;
		}
		const auto last = count - 1;
		if (curve >= -10 && curve <= 10) {
			if (curve > 0) {
				return AnimationPower(ordinal, ordinal, last, static_cast<std::uint32_t>(curve));
			}
			const auto complement = last - ordinal;

			return last - AnimationPower(complement, complement, last, static_cast<std::uint32_t>(-curve));
		}
		const auto half = count >> 1;
		if (curve > 10) {
			const auto power = static_cast<std::uint32_t>(curve - 10);
			if (ordinal < half) {
				return AnimationPower(ordinal, ordinal, half - 1, power);
			}
			const auto complement = last - ordinal;

			return last - AnimationPower(complement, complement, count - half - 1, power);
		}
		const auto power = static_cast<std::uint32_t>(-10 - curve);
		if (ordinal < half) {
			const auto complement = half - 1 - ordinal;

			return half - 1 - AnimationPower(complement, complement, half - 1, power);
		}
		const auto relative = ordinal - half;

		return half + AnimationPower(relative, relative, count - half - 1, power);
	}

	struct MotionVariableAnimation {
		std::string Name;
		std::optional<MotionChildBorrow> Child;
		float Initial = 0;
		float Target = 0;
		std::uint32_t Duration = 0;
		std::uint32_t Count = 1;
		std::int32_t Curve = 0;
		std::uint64_t Elapsed = 0;
		std::optional<std::uint32_t> LastOrdinal;
		ParallelTaskHandle Handle;
	};

	class MotionVariableAnimations::Implementation {
	public:
		Implementation(const MotionPlayerTask& task, MotionManagerRuntime& runtime, std::optional<MotionChildBorrow> child)
		    : Target(task), Runtime(runtime), ChildTarget(std::move(child)), Children(kMaximumVariableAnimations) {
			Entries.reserve(kMaximumVariableAnimations);
		}

		MotionPlayerTask Target;
		MotionManagerRuntime& Runtime;
		std::optional<MotionChildBorrow> ChildTarget;
		ParallelTaskSet Children;
		std::vector<std::weak_ptr<MotionVariableAnimation>> Entries;
		bool Updating = false;
		bool Closed = false;

		void RequireOpen() const {
			if (Closed || !Target.IsLive()) {
				throw std::runtime_error("motion variable animation requires its actual live containing player");
			}
		}

		void Step(MotionVariableAnimation& animation, ParallelTaskHandle handle) {
			RequireOpen();
			if (animation.Child && !animation.Child->IsLive()) {
				Children.SetStatus(handle, TaskStatus::Retire);
				return;
			}
			std::uint32_t ordinal = 0;
			if (animation.Duration == 0) {
				ordinal = animation.Count - 1;
			} else {
				const auto delta = Runtime.ReadFrameDelta();
				if (!std::isfinite(delta) || delta < 1 || delta > 4294967296.0f) {
					throw std::runtime_error("motion variable animation requires the bounded actual driver frame step");
				}
				animation.Elapsed += static_cast<std::uint64_t>(delta);
				if (animation.Elapsed >= animation.Duration) {
					animation.Elapsed = animation.Duration;
					Children.SetStatus(handle, TaskStatus::Retire);
				}
				ordinal = static_cast<std::uint32_t>((static_cast<std::uint64_t>(animation.Count - 1) * animation.Elapsed) / animation.Duration);
				ordinal = AnimationOrdinal(ordinal, animation.Count, animation.Curve);
			}
			if (!animation.LastOrdinal || *animation.LastOrdinal != ordinal) {
				animation.LastOrdinal = ordinal;
				float value = animation.Target;
				if (ordinal != animation.Count - 1) {
					const auto ratio = static_cast<float>(ordinal) / static_cast<float>(animation.Count - 1);
					value = (1.0f - ratio) * animation.Initial + ratio * animation.Target;
				}
				// Native52aee0 publishes the actual sampled value with controlword0, never the easing selector.
				if (animation.Child) {
					Target.SetChildVariable(*animation.Child, animation.Name, value, 0);
				} else {
					Target.SetVariable(animation.Name, value, 0);
				}
			}
			if (animation.Duration == 0) {
				Children.SetStatus(handle, TaskStatus::Retire);
			}
		}

		void Update() {
			RequireOpen();
			if (Updating) {
				throw std::runtime_error("motion variable animation cannot reenter its actual nested task traversal");
			}
			Updating = true;
			try {
				Children.Update();
			} catch (...) {
				Updating = false;
				throw;
			}
			Updating = false;
		}

		void Close() {
			if (Updating || !Children.CanClear()) {
				throw std::runtime_error("motion variable animation retirement requires its actual idle nested task set");
			}
			Children.Clear();
			Entries.clear();
			Closed = true;
		}
	};

	MotionVariableAnimations::MotionVariableAnimations(std::weak_ptr<Implementation> implementation, ParallelTaskSet& parent, ParallelTaskHandle handle)
	    : m_Implementation(std::move(implementation)), m_Parent(&parent), m_Handle(handle) {
	}

	void MotionVariableAnimations::Validate(std::string_view name, float initial, float target, std::int32_t duration, std::int32_t curve) {
		if (name.size() > kMaximumMotionCommandNameBytes || !std::isfinite(initial) || !std::isfinite(target)) {
			throw std::runtime_error("motion variable animation requires bounded finite values");
		}
		if (duration < 0 || duration > kMaximumAnimationDuration || curve < -kMaximumAnimationCurve || curve > kMaximumAnimationCurve) {
			throw std::runtime_error("motion variable animation duration/curve exceeds its bounded native integer arithmetic domain");
		}
	}

	MotionVariableAnimations MotionVariableAnimations::Attach(const MotionPlayerTask& actual_task, MotionManagerRuntime& actual_runtime, ParallelTaskSet& actual_common_first, std::optional<MotionChildBorrow> child) {
		auto state = std::make_shared<Implementation>(actual_task, actual_runtime, std::move(child));
		ParallelTaskCallbacks callbacks;
		callbacks.Update = [state, &actual_common_first](auto handle) {
			if (state->ChildTarget && !state->ChildTarget->IsLive()) {
				actual_common_first.SetStatus(handle, TaskStatus::Retire);
				return;
			}
			state->Update();
		};
		callbacks.Draw = [state](auto, auto argument) {
			state->Children.Draw(argument);
		};
		callbacks.CanRelease = [state](auto) {
			return !state->Updating && state->Children.CanClear();
		};
		callbacks.Release = [state](auto) {
			state->Close();
		};
		const auto handle = actual_common_first.Append(std::move(callbacks));

		return MotionVariableAnimations(state, actual_common_first, handle);
	}

	bool MotionVariableAnimations::IsLive() const noexcept {
		const auto state = m_Implementation.lock();

		return state && !state->Closed && m_Parent != nullptr && m_Parent->Contains(m_Handle);
	}

	void MotionVariableAnimations::Append(std::string_view name, float initial, float target, std::int32_t duration, std::int32_t curve, std::optional<MotionChildBorrow> child) {
		Validate(name, initial, target, duration, curve);
		const auto state = m_Implementation.lock();
		if (!IsLive() || !state || state->Updating || m_Parent->State(m_Handle).Status != TaskStatus::Active) {
			throw std::runtime_error("motion variable animation requires its live idle actual nested task owner");
		}
		state->RequireOpen();
		if (state->Children.Count() == kMaximumVariableAnimations) {
			throw std::runtime_error("motion variable animation task count exceeds its bounded actual owner");
		}
		auto animation = std::make_shared<MotionVariableAnimation>();
		animation->Name = name;
		animation->Child = std::move(child);
		animation->Initial = initial;
		animation->Target = target;
		animation->Duration = static_cast<std::uint32_t>(duration);
		animation->Count = duration == 0 ? 1 : static_cast<std::uint32_t>(duration) * 16;
		animation->Curve = curve;
		ParallelTaskCallbacks callbacks;
		const std::weak_ptr<Implementation> weak = state;
		callbacks.Update = [weak, animation](auto handle) {
			const auto state = weak.lock();
			if (!state) {
				throw std::runtime_error("motion variable animation handler lost its actual containing task set");
			}
			try {
				state->Step(*animation, handle);
			} catch (...) {
				state->Children.SetStatus(handle, TaskStatus::Retire);
				throw;
			}
		};
		callbacks.Draw = [](auto, auto) {
			// NativeSQAnimateHandler and MMotionVariableAnime Draw slots are genuine030f20 returns.
		};
		callbacks.Release = [](auto) {
		};
		std::erase_if(state->Entries, [](const auto& entry) {
			return entry.expired();
		});
		for (const auto& entry : state->Entries) {
			if (const auto previous = entry.lock(); previous && previous->Name == name && previous->Child.has_value() == animation->Child.has_value() && (!previous->Child || previous->Child->SameTarget(*animation->Child)) && state->Children.Contains(previous->Handle)) {
				// Native269e50 marks every same-type/name predecessor2 before appending the replacement to the tail.
				state->Children.SetStatus(previous->Handle, TaskStatus::Retire);
			}
		}
		animation->Handle = state->Children.Append(std::move(callbacks));
		state->Entries.push_back(animation);
	}

	void MotionVariableAnimations::RequestRetire() {
		if (IsLive()) {
			m_Parent->SetStatus(m_Handle, TaskStatus::Retire);
		}
	}
} // namespace sgre::scene
