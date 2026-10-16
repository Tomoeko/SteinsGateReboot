#include "sgre/scene/motion_layer_composition.hpp"
#include "sgre/scene/motion_local_frames.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static std::shared_ptr<sgre::resource::MotionResource> Model() {
	auto model = std::make_shared<sgre::resource::MotionResource>();
	sgre::resource::MotionObject character;
	character.Name = "a";
	sgre::resource::MotionTimeline timeline;
	timeline.Name = "idle";
	timeline.LastTime = 10;
	timeline.LoopTime = -1;
	sgre::resource::MotionFrame priority;
	priority.Type = 1;
	priority.Content.Data = sgre::resource::PsbArray{};
	timeline.Priority.push_back(priority);
	priority.Time = 10;
	priority.Content.Data = std::monostate{};
	timeline.Priority.push_back(std::move(priority));
	character.Timelines.push_back(std::move(timeline));
	model->Objects.push_back(std::move(character));

	return model;
}

static void TestActualClockOwner() {
	sgre::scene::TextureCache cache([](auto, const auto&) -> sgre::render::SpriteTextureHandle {
		throw std::runtime_error("source-free clock fixture attempted texture upload");
	},
	    [](auto) {
		    throw std::runtime_error("source-free clock fixture attempted texture release");
	    });
	sgre::scene::MotionResourceManager manager(cache);
	const auto model = Model();
	sgre::scene::MotionProjectIdentity identity;
	identity.DocumentInstance = 1;
	identity.RootOffset = 128;
	static_cast<void>(manager.Register(identity, model, {}));
	sgre::scene::MotionPlayerCommands commands(manager);
	Require(commands.ClockState().InitialCompositionPending, "actual new command/clock owner starts with native +192 set");
	commands.SetCharacter("a");
	commands.Play("idle");
	commands.SetTickCount(3);
	const auto& timeline = model->Objects[0].Timelines[0];
	const auto prepared = sgre::scene::PrepareMotionTimeline(timeline, *model);
	sgre::scene::MotionLocalFrameServices frame_services;
	frame_services.BindSource = [](auto, const auto&, const auto&) {
		throw std::runtime_error("source-free clock fixture attempted source binding");
	};
	frame_services.EnqueueAction = [](auto, auto) {
		throw std::runtime_error("source-free clock fixture attempted an action");
	};
	sgre::scene::MotionLocalFrames local_frames(timeline, prepared, *model, std::move(frame_services));
	sgre::scene::MotionRootScene root(sgre::scene::MotionRootMatrixMode::SuppliedByParent);
	sgre::scene::MotionLayerComposition composition(prepared, root, false);
	std::size_t preparations = 0;
	std::size_t compositions = 0;
	sgre::scene::MotionPlayerUpdateServices services;
	services.ReadFrameDelta = []() -> float {
		throw std::runtime_error("child clock fixture attempted independent frame timing");
	};
	services.PrepareFrames = [&](auto traversal, const auto& context) {
		Require(context.Timeline == &timeline, "initial clock preparation borrows its actual selected timeline");
		local_frames.Select(traversal, context.Clock.EvaluationTime);
		++preparations;
	};
	services.ComposeLayers = [&](const auto& context) {
		Require(context.Clock.InitialCompositionPending == commands.ClockState().InitialCompositionPending, "mandatory composition observes the same actual clock pending flag");
		composition.Compose(local_frames, context.Clock.TickAssigned);
		local_frames.CompleteComposition();
		++compositions;
	};
	services.PublishDrawPackets = [](const auto&) {
		throw std::runtime_error("child clock fixture attempted independent packet publication");
	};
	services.Draw = [] {
		throw std::runtime_error("child clock fixture attempted independent draw");
	};
	services.DeliverEvent = [](const auto&) {
		throw std::runtime_error("child clock fixture attempted independent event delivery");
	};
	sgre::scene::MotionPlayerUpdate update(commands, std::move(services));
	update.PrepareTimeline(timeline);
	try {
		update.UpdateChild(1);
		throw std::runtime_error("unbound actual child root composition was accepted");
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find("actual parent pose") != std::string_view::npos, error.what());
	}

	Require(commands.ClockState().InitialCompositionPending && commands.ClockState().TickAssigned && !root.Frame().Composed, "failed actual composition cannot clear native initial or assigned flags");
	sgre::scene::MotionChildRootPose parent_pose;
	parent_pose.Overrides.Visible = true;
	root.BindChildPose(parent_pose);
	update.UpdateChild(1);
	Require(root.Frame().Composed && compositions == 1 && preparations == 1 && !commands.ClockState().InitialCompositionPending && !commands.ClockState().TickAssigned, "successful actual frame/root composition clears flags only after the mandatory phase");
	commands.SetTickCount(5);
	Require(!commands.ClockState().InitialCompositionPending && commands.ClockState().TickAssigned, "native numeric tick assignment does not restore initial composition");
	commands.Stop();
	commands.Play("idle", sgre::scene::MotionPlayFlags::Force);
	Require(!commands.ClockState().InitialCompositionPending, "native forced timeline selection does not restore initial composition");
	update.UpdateChild(1);
	Require(compositions == 2 && !commands.ClockState().InitialCompositionPending && !commands.ClockState().TickAssigned, "subsequent real child composition preserves completed initial state");
	sgre::scene::MotionPlayerCommands new_child(manager, commands);
	Require(new_child.ClockState().InitialCompositionPending && new_child.CachedTimeline() == nullptr && new_child.PreferredProject() == commands.PreferredProject(), "real child constructor creates its own initial clock while copying only the parent's preferred project");
}

int main() {
	try {
		TestActualClockOwner();
		std::cout << "motion child initial composition tests passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
