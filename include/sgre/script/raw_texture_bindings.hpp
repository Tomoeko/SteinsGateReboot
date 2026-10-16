#pragma once

#include "sgre/scene/layer_manager_frame.hpp"
#include "sgre/scene/raw_layer_frame.hpp"
#include "sgre/script/object_bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace sgre::script {

	class ScriptBinaryBindings;
	class ScriptLayerBindings;
	class ScriptRawImageBindings;

	struct ScriptRawTextureLimits {
		std::size_t MaxInstances = 1024;
		std::size_t MaxShadowBytes = 256 * 1024 * 1024;
		std::size_t MaxBackgroundTasks = 64;
		std::uint64_t OnProcessInstructionBudget = 1000000;
	};

	class ScriptRawTextureBindings {
	public:
		using PriorityTaskProvider = std::function<scene::ParallelTaskSet&()>;
		using FrameOperation = std::function<void(const SquirrelValue&, scene::RawLayerFrame&)>;
		using DrawTexture = std::function<void(const SquirrelValue&, scene::RawTextureTask&, const scene::RawTextureDrawPacket&, std::uint32_t)>;

		// Actual VM/Object/common/image/binary/texture/driver services remain live through VM Close.
		ScriptRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptBinaryBindings& binary, scene::LayerTree& tree, scene::MutableTextureBackend& backend, scene::ParallelTaskSet& driver, ScriptRawTextureLimits limits = {});
		// Native290e30 resolves current SQDriver+118 only during RawTex construction, after Setup.
		// The nonmutating provider returns that live actual Priority owner or throws before mutation.
		// It and the returned group outlive every wrapper and its pending retired background entry.
		ScriptRawTextureBindings(SquirrelVm& vm, ScriptObjectBindings& objects, ScriptLayerBindings& layers, ScriptRawImageBindings& images, ScriptBinaryBindings& binary, scene::LayerTree& tree, scene::MutableTextureBackend& backend, PriorityTaskProvider priority, ScriptRawTextureLimits limits = {});
		~ScriptRawTextureBindings();
		ScriptRawTextureBindings(const ScriptRawTextureBindings&) = delete;
		ScriptRawTextureBindings& operator=(const ScriptRawTextureBindings&) = delete;
		ScriptRawTextureBindings(ScriptRawTextureBindings&&) = delete;
		ScriptRawTextureBindings& operator=(ScriptRawTextureBindings&&) = delete;

		[[nodiscard]] SquirrelValue RawTextureClass() const;
		[[nodiscard]] scene::RawLayerOwner& Owner(const SquirrelValue& instance);
		void ReleaseInstance(const SquirrelValue& instance);
		// Configure before the first RawTex construction; services enclose every actual wrapper/frame.
		void ConfigureFrameOwners(scene::LayerCameraDrawScope& renderer, DrawTexture drawing);
		void AttachFrame(const SquirrelValue& instance, scene::LayerCameraDrawScope& renderer, DrawTexture drawing);
		[[nodiscard]] scene::RawLayerFrameState FrameState(const SquirrelValue& receiver) const;
		[[nodiscard]] scene::LayerFrameProcessState ProcessState(scene::LayerHandle handle) const;
		void SetProcessState(const SquirrelValue& instance, scene::LayerFrameProcessState state);
		// Handle dispatch lends one checked VM receiver without a manager self-root.
		// Borrowed-receiver dispatch consumes an existing containing scope without redispatching it.
		void DispatchFrame(scene::LayerHandle handle, const FrameOperation& operation);
		void DispatchFrame(const SquirrelValue& receiver, const FrameOperation& operation);
		void Update(scene::LayerHandle handle);
		void Update(const SquirrelValue& receiver);
		void Draw(scene::LayerHandle handle, std::uint32_t argument);
		void Draw(const SquirrelValue& receiver, std::uint32_t argument);

	private:
		class Implementation;
		std::shared_ptr<Implementation> m_Implementation;
	};

} // namespace sgre::script
