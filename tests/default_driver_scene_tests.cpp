#include "sgre/scene/default_driver_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

template <class Operation>
static void RequireError(Operation operation, std::string_view expected) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(expected) != std::string_view::npos, error.what());

		return;
	}

	throw std::runtime_error("expected actual default controller graph rejection");
}

static constexpr std::array<sgre::scene::DefaultDriverTaskGroup, 4> kGroups{
    sgre::scene::DefaultDriverTaskGroup::Priority,
    sgre::scene::DefaultDriverTaskGroup::BeforeLayersFirst,
    sgre::scene::DefaultDriverTaskGroup::BeforeLayersSecond,
    sgre::scene::DefaultDriverTaskGroup::AfterResourceMeter};

struct Operations {
	sgre::scene::LayerTree& Tree;
	std::vector<int> Calls;
	bool FailResourceRetire = false;
	std::size_t ResourcesRetired = 0;
	std::size_t MetersRetired = 0;

	explicit Operations(sgre::scene::LayerTree& tree)
	    : Tree(tree) {
	}

	sgre::scene::DefaultDriverSceneServices Make() {
		sgre::scene::DefaultDriverSceneServices services;
		services.ResourceManager.Update = [this] {
			Calls.push_back(1);
		};
		services.ResourceManager.Draw = [this](std::uint32_t argument) {
			Require(argument == 0xfedcba98, "resource task receives native full DWORD");
			Calls.push_back(11);
		};
		services.ResourceManager.Retire = [this] {
			++ResourcesRetired;
			Calls.push_back(101);
			if (FailResourceRetire) {
				throw std::runtime_error("actual resource retirement sentinel");
			}
		};
		services.ResourceMeter.Update = [this] {
			Calls.push_back(6);
		};
		services.ResourceMeter.Draw = [this](auto) {
			Calls.push_back(16);
		};
		services.ResourceMeter.Retire = [this] {
			Require(Tree.ChildCount() == 0, "actual root LayerManager clears before resource-meter retirement");
			++MetersRetired;
			Calls.push_back(106);
		};
		services.RootLayers.State = [this](auto layer) {
			Require(Tree.Contains(layer), "root manager state resolves actual retained Layer records");

			return sgre::scene::LayerFrameProcessState{};
		};
		services.RootLayers.Update = [this](auto) {
			Calls.push_back(5);
		};
		services.RootLayers.Draw = [this](auto, std::uint32_t argument) {
			Require(argument == 0xfedcba98, "actual root child draw receives native full DWORD");
			Calls.push_back(15);
		};
		services.RootLayers.PrepareDraw = [this] {
			Calls.push_back(21);
		};

		return services;
	}

	void AddGroups(sgre::scene::DefaultDriverScene& graph) {
		for (std::size_t index = 0; index != kGroups.size(); ++index) {
			const auto number = index == 3 ? 7 : static_cast<int>(index) + 2;
			sgre::scene::ParallelTaskCallbacks task;
			task.Update = [this, number](auto) {
				Calls.push_back(number);
			};
			task.Draw = [this, number](auto, std::uint32_t argument) {
				Require(argument == 0xfedcba98, "actual heap task group preserves native draw argument");
				Calls.push_back(number + 10);
			};
			task.Release = [this, number](auto) {
				Calls.push_back(number + 100);
			};
			static_cast<void>(graph.Group(kGroups[index]).Append(std::move(task)));
		}
	}
};

static void TestSetupInsideActualProcessAndPhysicalDispatch() {
	sgre::scene::LayerTree tree(8);
	Operations operations{tree};
	sgre::scene::CommonProcessTasks controller;
	std::unique_ptr<sgre::scene::DefaultDriverScene> graph;
	sgre::scene::LayerHandle child;
	controller.Update([&] {
		operations.Calls.push_back(0);
		graph = std::make_unique<sgre::scene::DefaultDriverScene>(controller, tree, operations.Make(), 8);
		operations.AddGroups(*graph);
		child = tree.CreateMotion();
	});
	Require(operations.Calls == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7} && controller.Parallel().Count() == 7, "native setup publishes exact seven-node graph during onProcess, then that same actual MProcess update traverses it");
	Require(&graph->Group(kGroups[0]) != &graph->Group(kGroups[1]) && &graph->Group(kGroups[1]) != &graph->Group(kGroups[2]) && &graph->Group(kGroups[2]) != &graph->Group(kGroups[3]), "actual controller owns four distinct physical heap groups");
	std::array<sgre::scene::ParallelTaskHandle, 7> roots;
	static_cast<void>(controller.Parallel().WriteOrder(roots));
	Require(controller.Parallel().State(roots[5]).DrawPaused && !controller.Parallel().State(roots[4]).DrawPaused, "resource meter alone starts paused; root manager renderer prelude remains distinct");
	operations.Calls.clear();
	graph->Draw(0xfedcba98);
	Require(operations.Calls == std::vector<int>{11, 12, 13, 14, 21, 15, 17}, "actual default graph draws root reset before children and skips only the paused native resource meter");
	sgre::scene::SerialTaskCallbacks serial;
	serial.Update = [&](auto&) {
		operations.Calls.push_back(8);
		Require(!graph->CanClose(), "active controller dispatch cannot destroy the actual graph");
	};
	serial.Draw = [&](auto&, std::uint32_t argument) {
		Require(argument == 0xfedcba98, "actual embedded serial receives the same DWORD after parallel drawing");
		operations.Calls.push_back(18);
	};
	serial.Release = [&] {
		operations.Calls.push_back(108);
	};
	static_cast<void>(controller.Serial().Append(std::make_unique<sgre::scene::SerialTask>(std::move(serial))));
	operations.Calls.clear();
	graph->Update([] {
		throw std::runtime_error("blocked actual serial unexpectedly invoked driver phase");
	});
	Require(operations.Calls == std::vector<int>{8, 1, 2, 3, 4, 5, 6, 7}, "native pending serial blocks onProcess without blocking the actual parallel scene graph");
	operations.Calls.clear();
	graph->Draw(0xfedcba98);
	Require(operations.Calls.back() == 18, "actual controller draws parallel graph before embedded serial");
	operations.Calls.clear();
	graph->Close();
	Require(operations.Calls == std::vector<int>{101, 102, 103, 104, 106, 107} && tree.Contains(child) && !tree.State(child).Attached && controller.Serial().Count() == 1, "actual graph retirement uses physical root order while retaining child objects and leaving driver serial to its concrete owner");
	controller.ClearSerial();
	Require(operations.Calls.back() == 108 && operations.ResourcesRetired == 1 && operations.MetersRetired == 1, "concrete controller serial retirement follows once-only scene owners");
	tree.Release(child);
}

static void TestTransferredRootsAndFailureCompletion() {
	sgre::scene::LayerTree tree(4);
	Operations operations{tree};
	sgre::scene::CommonProcessTasks controller;
	sgre::scene::DefaultDriverScene graph(controller, tree, operations.Make(), 4);
	operations.AddGroups(graph);
	const auto child = tree.CreateFolder();
	std::array<sgre::scene::ParallelTaskHandle, 7> roots;
	static_cast<void>(controller.Parallel().WriteOrder(roots));
	auto transferred = controller.Parallel().Extract(roots[1]);
	Require(!graph.CanClose(), "actual detached adapter lease still owns its root callback");
	RequireError([&] {
		graph.Close();
	},
	    "transferred root ownership");
	transferred.Release();
	Require(graph.CanClose(), "actual transferred root release records safe callback retirement");
	operations.Calls.clear();
	operations.FailResourceRetire = true;
	RequireError([&] {
		graph.Close();
	},
	    "actual resource retirement sentinel");
	Require(operations.Calls == std::vector<int>{101, 103, 104, 106, 107} && controller.Parallel().Count() == 0 && tree.Contains(child) && !tree.State(child).Attached, "first native-owner failure still retires later graph owners and preserves detached Layers");
	graph.Close();
	RequireError([&] {
		graph.Draw(0);
	},
	    "graph has retired");
	tree.Release(child);
}

static void TestRequiredServicesAndActualRootClearPreflight() {
	sgre::scene::LayerTree tree(4);
	Operations operations{tree};
	sgre::scene::CommonProcessTasks controller;
	auto services = operations.Make();
	services.ResourceManager.Update = {};
	RequireError([&] {
		sgre::scene::DefaultDriverScene graph(controller, tree, std::move(services));
	},
	    "concrete ResourceManager/ResourceMeter services");
	Require(controller.Parallel().Count() == 0 && operations.ResourcesRetired == 0, "missing real native service rejects before owner publication");
	const auto child = tree.CreateMotion();
	tree.VisitChildren({}, [&](auto) {
		RequireError([&] {
			tree.ClearRoot();
		},
		    "child traversal");
		Require(tree.State(child).Attached, "root clear preflight preserves its actual active native list");
	});
	tree.ClearRoot();
	Require(tree.Contains(child) && !tree.State(child).Attached && tree.ChildCount() == 0, "actual default root-manager clear detaches retained native children without another registry");
	tree.Release(child);
}

int main() {
	try {
		TestSetupInsideActualProcessAndPhysicalDispatch();
		TestTransferredRootsAndFailureCompletion();
		TestRequiredServicesAndActualRootClearPreflight();
		std::cout << "3 actual SQDriver scene graph groups passed\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
