#pragma once

#include "sgre/scene/affine_layer_parameters.hpp"
#include "sgre/scene/layer_camera_draw.hpp"
#include "sgre/scene/parallel_task_set.hpp"
#include "sgre/script/affine_layer_matrices.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::script {

	class ScriptLayerBindings;
	class ScriptObjectBindings;

	struct ScriptAffineLayerTaskState {
		scene::LayerAffineTransform Projected;
		scene::LayerAffineTransform Inverse;
		scene::LayerCameraParameters Camera;
		bool MatrixInvalidated = false;
		bool TransformPrepared = false;
	};

	class ScriptAffineLayerBindings {
	public:
		// The VM, common Layer and matrix services outlive this adapter and VM Close.
		ScriptAffineLayerBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptAffineMatrixBindings& matrices, std::size_t max_instances = 1024);
		// Detach the borrowed priority task before service destruction; unsupported active destruction fails fast.
		~ScriptAffineLayerBindings() noexcept;
		ScriptAffineLayerBindings(const ScriptAffineLayerBindings&) = delete;
		ScriptAffineLayerBindings& operator=(const ScriptAffineLayerBindings&) = delete;
		ScriptAffineLayerBindings(ScriptAffineLayerBindings&&) = delete;
		ScriptAffineLayerBindings& operator=(ScriptAffineLayerBindings&&) = delete;

		[[nodiscard]] SquirrelValue AffineLayerClass() const;
		[[nodiscard]] scene::AffineLayerParameterState Parameters(const SquirrelValue& instance) const;
		// Returns an authored matrix value for scene submission/diagnostics, without adding a script getter.
		[[nodiscard]] scene::AffineMatrix MatrixValue(const SquirrelValue& instance, AffineLayerMatrixSlot slot) const;
		[[nodiscard]] ScriptAffineLayerTaskState TaskState(const SquirrelValue& instance) const;
		[[nodiscard]] bool PriorityOrderDirty() const noexcept;
		// Recovered global manager phase publishes rank+AbsoluteBase to actual Layer owners, then clears its dirty byte.
		// Comparisons use current native state and existing VM lookup effects; no receiver root is added.
		void UpdatePriorityOrder();
		// The actual default SQDriver +0x118 Parallel owner is supplied by its concrete scene context and outlives attachment.
		// This does not construct a substitute default driver or enable full frames. VM/binding owners outlive callbacks.
		void AttachPriorityTask(scene::ParallelTaskSet& actual_driver_group);
		[[nodiscard]] bool CanDetachPriorityTask() const noexcept;
		void DetachPriorityTask();
		// This evaluates the recovered matrix phase. The actual common MProcess boundary is supplied explicitly.
		void Process(const SquirrelValue& instance, const std::function<void()>& common_process);
		void Draw(const SquirrelValue& instance, std::uint32_t argument, const std::function<void(std::uint32_t)>& common_draw);
		void ReleaseInstance(const SquirrelValue& instance);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
