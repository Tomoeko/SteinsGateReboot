#pragma once

#include "prepared_driver_close_fixture.hpp"

#include "sgre/scene/motion_assets.hpp"
#include "sgre/scene/motion_player_task.hpp"
#include "sgre/script/motion_resource_owner.hpp"
#include "sgre/script/psb_bindings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

static sgre::resource::PsbValue LeafFrame(std::uint32_t index) {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	const auto content = Atom(PsbObject{{"mask", Atom(std::int64_t{2 | 1 | 0x400 | 0x200 | 0x20000 | 0x40000})}, {"src", Atom(std::string("atlas"))}, {"icon", Atom(std::string("tile"))}, {"coord", Atom(PsbArray{Atom(7.0 + index * 2), Atom(9.0), Atom(4.0)})}, {"ox", Atom(2.0)}, {"oy", Atom(static_cast<double>(index))}, {"opa", Atom(std::int64_t{255})}, {"color", Atom(std::int64_t{0x808080ff})}, {"bm", Atom(std::int64_t{16})}, {"act", Atom("act" + std::to_string(index))}});

	return Atom(PsbObject{{"time", Atom(static_cast<double>(index * 4))}, {"type", Atom(std::int64_t{index == 2 ? 0 : 2})}, {"content", content}});
}

static std::shared_ptr<sgre::resource::PsbDocument> LeafDocument() {
	using sgre::resource::PsbArray;
	using sgre::resource::PsbObject;
	sgre::resource::PsbChunkReference chunk;
	auto texture = Atom(PsbObject{{"type", Atom(std::string("A8"))}, {"width", Atom(std::int64_t{256})}, {"height", Atom(std::int64_t{256})}, {"truncated_width", Atom(std::int64_t{256})}, {"truncated_height", Atom(std::int64_t{256})}, {"pixel", Atom(chunk)}});
	texture.SerializedOffset = 64;
	const auto icon = Atom(PsbObject{{"left", Atom(8.0)}, {"top", Atom(16.0)}, {"width", Atom(32.0)}, {"height", Atom(64.0)}, {"originX", Atom(3.0)}, {"originY", Atom(5.0)}, {"attr", Atom(std::int64_t{2})}});
	const auto source = Atom(PsbObject{{"texture", std::move(texture)}, {"icon", Atom(PsbObject{{"tile", icon}})}});
	const auto layer = Atom(PsbObject{{"label", Atom(std::string("sprite"))}, {"type", Atom(std::int64_t{0})}, {"inheritMask", Atom(std::int64_t{4})}, {"coordinate", Atom(std::int64_t{0})}, {"frameList", Atom(PsbArray{LeafFrame(0), LeafFrame(1), LeafFrame(2)})}, {"transformOrder", Atom(PsbArray{Atom(std::int64_t{0}), Atom(std::int64_t{3}), Atom(std::int64_t{2}), Atom(std::int64_t{1})})}, {"children", Atom(PsbArray{})}, {"stencilType", Atom(std::int64_t{0})}, {"meshTransform", Atom(std::int64_t{0})}, {"objTriPriority", Atom(std::int64_t{7})}, {"parameterize", sgre::resource::PsbValue{}}, {"joinTarget", Atom(std::int64_t{0})}, {"groundCorrection", Atom(std::int64_t{0})}});
	const auto priority = Atom(PsbObject{{"time", Atom(0.0)}, {"type", Atom(std::int64_t{1})}, {"content", Atom(PsbArray{Atom(std::int64_t{0})})}});
	const auto priority_terminal = Atom(PsbObject{{"time", Atom(8.0)}, {"type", Atom(std::int64_t{1})}, {"content", sgre::resource::PsbValue{}}});
	const auto timeline = Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"bounds", Atom(PsbObject{{"left", Atom(-2.0)}, {"top", Atom(-3.0)}, {"right", Atom(12.0)}, {"bottom", Atom(21.0)}})}, {"lastTime", Atom(8.0)}, {"loopTime", Atom(-1.0)}, {"layer", Atom(PsbArray{layer})}, {"priority", Atom(PsbArray{priority, priority_terminal})}, {"tag", Atom(PsbArray{})}, {"parameter", sgre::resource::PsbValue{}}, {"variable", sgre::resource::PsbValue{}}, {"parameterize", sgre::resource::PsbValue{}}});
	auto document = std::make_shared<sgre::resource::PsbDocument>();
	document->Header.Version = 4;
	document->Header.RootOffset = 44;
	document->Header.DataOffset = 128;
	document->SerializedSize = 65664;
	document->Chunks = {{128, 65536}};
	document->Root = Atom(PsbObject{{"id", Atom(std::string("motion"))}, {"spec", Atom(std::string("win"))}, {"version", Atom(3.03)}, {"label", Atom(std::string("Independent actual leaf owner"))}, {"easing", Atom(PsbArray{})}, {"source", Atom(PsbObject{{"atlas", source}})}, {"object", Atom(PsbObject{{"character", Atom(PsbObject{{"type", Atom(std::int64_t{0})}, {"motion", Atom(PsbObject{{"main", timeline}})}})}})}, {"sourceIconRenameMap", Atom(PsbObject{})}});
	document->Root.SerializedOffset = 44;

	return document;
}

static std::shared_ptr<std::vector<std::uint8_t>> LeafBacking() {
	return std::make_shared<std::vector<std::uint8_t>>(65664, 128);
}

class PreparedLeafFixture {
public:
	explicit PreparedLeafFixture(const std::filesystem::path& directory) : Driver(directory), Psb(Driver.Vm, Driver.Objects), Assets(Driver.Textures) {
		try {
			Driver.Layers.ConfigureMotionOwners(Psb, Assets, Driver.Textures);
			for (std::size_t index = 0; index < Seed.size(); ++index) {
				Seed[index] = static_cast<std::uint32_t>(index) * 0x9e3779b9u + 17;
			}
			const std::array<sgre::script::SquirrelValue, 1> receiver{Driver.Vm.RootTable()};
			Leaf = Driver.Vm.Invoke(Driver.Layers.LayerClass(), receiver).Value;
			Owner = &Driver.Layers.MotionResources(Leaf);
			const auto script = Psb.WrapDocument(LeafDocument(), LeafBacking());
			WeakPsb = Driver.Vm.CreateWeakReference(script);
			const std::array<sgre::script::SquirrelValue, 2> registration_arguments{Leaf, script};
			Project.Value = std::get<std::int32_t>(Driver.Vm.Invoke(Driver.Vm.GetSlot(Leaf, std::string("registerMotionResource")), registration_arguments).Value);
			Runtime = MakeRuntime(Owner->Secondary());
		} catch (...) {
			const auto failure = std::current_exception();
			try {
				// These PSB/catalog owners still enclose all actual Layer records during constructor unwind.
				Driver.Close->Close();
			} catch (...) {
				std::terminate();
			}

			std::rethrow_exception(failure);
		}
	}

	~PreparedLeafFixture() noexcept {
		try {
			if (!Driver.Vm.IsClosed()) {
				if (Runtime && Runtime->ResourceBorrowsLive()) {
					Runtime->Context().Clear();
				}
			}
			Driver.Close->Close();
		} catch (...) {
			std::terminate();
		}
	}
	PreparedLeafFixture(const PreparedLeafFixture&) = delete;
	PreparedLeafFixture& operator=(const PreparedLeafFixture&) = delete;

	std::unique_ptr<sgre::scene::MotionManagerRuntime> MakeRuntime(const sgre::scene::MotionResourceManager& manager) {
		sgre::scene::MotionManagerRuntimeServices services;
		services.ReadSourceAliases = [this](auto project) {
			Require(project == Project, "alias service must use the actual registered leaf project");
			if (OnAlias) {
				OnAlias();
			}

			return Owner->SourceAliases(project);
		};

		return std::make_unique<sgre::scene::MotionManagerRuntime>(manager, Clock, Seed, std::move(services), 32);
	}

	void Bind() {
		Driver.Layers.AttachMotionRuntime(Leaf, *Runtime);
	}

	sgre::scene::MotionPlayerTask Attach() {
		return sgre::scene::MotionPlayerTask::Attach(*Runtime, Driver.Layers.MotionTasks(Leaf), false, "character", [this](const auto& event) {
			Require(event.Kind == sgre::scene::MotionPlayerEventKind::Action, "leaf fixture emits real selected-layer action events");
			Events.emplace_back(event.Action);
			if (OnAction) {
				OnAction();
			}
		});
	}

	sgre::scene::ParallelTaskSet& Second() {
		return Driver.Layers.MotionTasks(Leaf).Group(sgre::scene::MotionTaskGroup::Second);
	}

	void QueueRows(sgre::scene::MotionPlayerTask& task) {
		task.SetVisible(true);
		task.Play("main");
		Second().Update();
		Second().Draw(0x1234);
		Require(Runtime->Context().Size() == 1 && task.Player().PublishedRows().size() == 1, "actual selected producer must queue its real retained sprite row");
	}

	PreparedCloseFixture Driver;
	sgre::script::ScriptPsbBindings Psb;
	sgre::scene::MotionAssetCatalog Assets;
	std::array<std::uint32_t, sgre::scene::kMotionSharedSeedWords> Seed{};
	sgre::core::FrameClock Clock{0};
	sgre::script::SquirrelValue Leaf;
	sgre::script::SquirrelValue WeakPsb;
	const sgre::script::ScriptMotionResourceOwner* Owner = nullptr;
	sgre::scene::MotionProjectId Project;
	std::unique_ptr<sgre::scene::MotionManagerRuntime> Runtime;
	std::vector<std::string> Events;
	std::function<void()> OnAlias;
	std::function<void()> OnAction;
};
