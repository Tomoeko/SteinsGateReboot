#include "sgre/script/affine_layer_links.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using SquirrelValue = sgre::script::SquirrelValue;
using LinkState = sgre::script::ScriptAffineLayerLinks;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Graph {
	sgre::script::SquirrelVm Vm;
	SquirrelValue Class;
	std::array<SquirrelValue, 8> Instances;
	std::array<sgre::script::SquirrelObjectIdentity, 8> Identities;
	std::array<std::unique_ptr<LinkState>, 8> Links;
	std::array<bool, 8> Dirty{};
	std::array<std::size_t, 8> Invalidations{};
	bool SortDirty = false;

	static sgre::script::SquirrelVmLimits Limits(bool reclaim) {
		sgre::script::SquirrelVmLimits limits;
		limits.ReclaimObjects = reclaim;

		return limits;
	}

	explicit Graph(bool reclaim = false) : Vm(Limits(reclaim)), Class(Vm.CreateClass()) {
		for (std::size_t index = 0; index < Instances.size(); ++index) {
			Instances[index] = Vm.CreateInstance(Class);
			Identities[index] = Vm.BorrowObjectIdentity(Instances[index]);
			sgre::script::ScriptAffineLayerLinkCallbacks callbacks;
			callbacks.NativeClass = Vm.BorrowObjectIdentity(Class);
			callbacks.FindOwner = [this](const SquirrelValue& instance) -> LinkState* {
				if (Vm.Type(instance) != sgre::script::SquirrelValueType::Instance) {
					return nullptr;
				}

				for (std::size_t candidate = 0; candidate < Instances.size(); ++candidate) {
					if (Vm.BorrowObjectIdentity(instance) == Identities[candidate]) {
						return Links[candidate].get();
					}
				}

				return nullptr;
			};
			callbacks.MarkMatrixDirty = [this, index] {
				Dirty[index] = true;
				++Invalidations[index];
			};
			callbacks.MarkSortDirty = [this] {
				SortDirty = true;
			};
			Links[index] = std::make_unique<LinkState>(Vm, Instances[index], std::move(callbacks));
		}
	}

	~Graph() {
		try {
			Vm.Close();
		} catch (...) {
		}
	}

	std::int32_t Index(const SquirrelValue& instance) const {
		for (std::size_t index = 0; index < Instances.size(); ++index) {
			if (instance == Instances[index]) {
				return static_cast<std::int32_t>(index);
			}
		}

		return -1;
	}

	std::vector<std::uint32_t> Snapshot() {
		std::vector<std::uint32_t> result;
		for (std::size_t index = 0; index < Links.size(); ++index) {
			const auto& link = *Links[index];
			const auto& parent = link.ParentLink();
			const auto weak = Vm.Type(parent) == sgre::script::SquirrelValueType::WeakReference;
			const auto resolved = weak ? Vm.ResolveWeakReference(parent) : parent;
			result.push_back(static_cast<std::uint32_t>(Index(resolved)));
			result.push_back(weak ? 0x8010000u : 0x1000001u);
			result.push_back(Dirty[index] ? 1u : 0u);
			const auto count = Vm.Length(link.Children());
			Require(count <= 8, "native fixture child list exceeds its prepared bound");
			result.push_back(static_cast<std::uint32_t>(count));
			for (std::size_t slot = 0; slot < 8; ++slot) {
				result.push_back(slot < count ? static_cast<std::uint32_t>(Index(Vm.GetSlot(link.Children(), static_cast<std::int32_t>(slot)))) : 0xffffffffu);
			}
		}

		result.push_back(SortDirty ? 1u : 0u);
		const auto error = Vm.LastErrorInContext(Vm.RootContext());
		const auto* text = sgre::script::TryStringText(error);
		result.push_back(text && *text == "the object is not a class instance" ? 91u : text && *text == "invalid type tag" ? 92u
		        : text && *text == "the index doesn't exist"                                                               ? 93u
		                                                                                                                   : 0u);

		return result;
	}
};

static void CheckStrongChildrenAndWeakParent() {
	Graph graph(true);
	std::size_t released = 0;
	graph.Vm.SetInstanceReleaseHook(graph.Instances[1], [&](auto&, const auto&) {
		++released;
	});
	const auto weak_child = graph.Vm.CreateWeakReference(graph.Instances[1]);
	const auto weak_parent = graph.Vm.CreateWeakReference(graph.Instances[0]);
	graph.Links[1]->SetLink(graph.Instances[0]);
	Require(graph.Links[1]->ParentLink() == weak_parent && graph.Vm.RetainedReferenceCount(weak_parent) == 1, "parent link retains exactly one canonical weak external root");
	Require(graph.Vm.Type(graph.Vm.GetSlot(graph.Links[0]->Children(), std::int32_t{0})) == sgre::script::SquirrelValueType::Instance, "native children array contains resolved strong instances");
	graph.Instances[1] = {};
	Require(released == 0 && graph.Vm.Type(graph.Vm.ResolveWeakReference(weak_child)) == sgre::script::SquirrelValueType::Instance, "parent array keeps the actual child alive after external strong values retire");
	graph.Links[0]->DetachChildren();
	Require(released == 1 && graph.Vm.Type(graph.Vm.ResolveWeakReference(weak_child)) == sgre::script::SquirrelValueType::Null, "native clear retires the last child after ordered back-link handling");
	graph.Vm.RethrowReleaseFailure();
}

static void CheckLiveAssignmentAndInvalidation() {
	Graph graph;
	for (const auto child : {4u, 5u, 6u, 7u}) {
		graph.Links[child]->SetLink(graph.Instances[3]);
	}
	graph.Links[2]->AssignLinks(*graph.Links[3]);
	Require(graph.Vm.Length(graph.Links[2]->Children()) == 2 && graph.Vm.GetSlot(graph.Links[2]->Children(), std::int32_t{0}) == graph.Instances[4] && graph.Vm.GetSlot(graph.Links[2]->Children(), std::int32_t{1}) == graph.Instances[6], "native indexed assignment transfers alternating elements as the source shrinks");
	Require(graph.Vm.Length(graph.Links[3]->Children()) == 2 && graph.Vm.GetSlot(graph.Links[3]->Children(), std::int32_t{0}) == graph.Instances[5] && graph.Vm.GetSlot(graph.Links[3]->Children(), std::int32_t{1}) == graph.Instances[7], "shifted source elements retain their old parent in native order");
	const auto before = graph.Invalidations;
	graph.Links[2]->AssignLinks(*graph.Links[2]);
	Require(graph.Invalidations == before, "same resolved parent and self-assignment remain inert");
	graph.Links[2]->Invalidate();
	Require(graph.Invalidations[2] == before[2] + 1 && graph.Invalidations[4] == before[4] + 1 && graph.Invalidations[6] == before[6] + 1 && graph.Invalidations[5] == before[5] && graph.Invalidations[7] == before[7], "recursive invalidation visits the actual strong descendant array only");
	graph.Links[4]->Unlink();
	Require(graph.Vm.Length(graph.Links[2]->Children()) == 1 && graph.Vm.GetSlot(graph.Links[2]->Children(), std::int32_t{0}) == graph.Instances[6], "unlink preserves survivor order and removes its first matching instance");
}

static void CheckExpiredParentAndNativeErrors() {
	Graph graph(true);
	graph.Links[1]->SetLink(graph.Instances[0]);
	const auto stored_weak = graph.Links[1]->ParentLink();
	// Prepared absent native parent: its script instance has already lost its payload/array owner.
	graph.Links[0].reset();
	graph.Instances[0] = {};
	Require(graph.Vm.Type(graph.Vm.ResolveWeakReference(stored_weak)) == sgre::script::SquirrelValueType::Null, "prepared parent payload/instance retirement expires the retained weak reference");
	graph.SortDirty = false;
	const auto before = graph.Invalidations;
	graph.Links[1]->SetLink(SquirrelValue{});
	Require(graph.Links[1]->ParentLink() == stored_weak && !graph.SortDirty && graph.Invalidations == before, "expired stored parent and proposed null resolve identically and remain inert");
	graph.Links[1]->Unlink();
	Require(graph.Links[1]->ParentLink() == stored_weak, "native unlink leaves an expired stored weak parent intact");
	const auto wrong = graph.Vm.CreateInstance(graph.Vm.CreateClass());
	graph.Links[1]->SetLink(wrong);
	Require(sgre::script::StringText(graph.Vm.LastErrorInContext(graph.Vm.RootContext())) == "the object is not a class instance", "proposed wrong-tag error is overwritten by the later expired stored-parent type lookup");
	graph.Links[1]->Retire();
	Require(graph.Vm.RetainedReferenceCount(stored_weak) == 0, "final native owner retirement drops the previously retained expired weak root");
}

static void CheckSafetyAndRetirement() {
	Graph graph;
	graph.Links[1]->SetLink(graph.Instances[0]);
	try {
		graph.Links[0]->SetLink(graph.Instances[1]);
	} catch (const std::runtime_error& error) {
		Require(std::string(error.what()).find("cycle") != std::string::npos && graph.Vm.Type(graph.Links[0]->ParentLink()) == sgre::script::SquirrelValueType::Null, "unsafe native cycle is rejected without mutating the proposed parent");
		graph.Links[1]->Retire();
		graph.Links[1]->Retire();
		Require(graph.Links[1]->IsRetired() && graph.Vm.Length(graph.Links[0]->Children()) == 0, "ordered native retirement is idempotent and unlinks before dropping child roots");
		graph.Vm.Close();
		graph.Links[0]->Retire();
		Require(graph.Links[0]->IsRetired(), "external owner roots retire safely after VM Close");

		return;
	}

	throw std::runtime_error("unsafe affine cycle was accepted");
}

static std::uint32_t ReadWord(std::ifstream& stream) {
	std::uint32_t result = 0;
	for (std::uint32_t shift = 0; shift < 32; shift += 8) {
		const auto byte = stream.get();
		if (byte == std::char_traits<char>::eof()) {
			throw std::runtime_error("truncated affine link native vector");
		}

		result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(byte)) << shift;
	}

	return result;
}

static void CompareNative(const char* vector_path) {
	std::ifstream input(vector_path, std::ios::binary);
	Require(ReadWord(input) == 0x4b4c4753u, "invalid native affine link corpus magic");
	const auto records = ReadWord(input);
	Require(records <= 65536, "native affine link corpus exceeds its record bound");
	std::unique_ptr<Graph> graph;
	std::uint32_t previous_case = 0xffffffffu;
	std::size_t words = 0;
	for (std::uint32_t record = 0; record < records; ++record) {
		const auto case_id = ReadWord(input);
		const auto operation = ReadWord(input);
		const auto index = ReadWord(input);
		const auto other = ReadWord(input);
		Require(index < 8 && (other < 8 || other == 0xffffffffu), "native affine vector has invalid owner indices");
		if (case_id != previous_case) {
			graph = std::make_unique<Graph>();
			previous_case = case_id;
			if (operation == 4) {
				for (const auto child : {4u, 5u, 6u, 7u}) {
					graph->Links[child]->SetLink(graph->Instances[3]);
				}
			}
		}

		const auto compare = [&](const auto& snapshot, const char* phase) {
			for (std::size_t word = 0; word < snapshot.size(); ++word) {
				const auto expected = ReadWord(input);
				if (snapshot[word] != expected) {
					throw std::runtime_error("native affine link difference at record " + std::to_string(record) + ", " + phase + " word " + std::to_string(word) + ": expected " + std::to_string(expected) + ", got " + std::to_string(snapshot[word]));
				}
				++words;
			}
		};
		compare(graph->Snapshot(), "before");
		switch (operation) {
		case 0:
			graph->Links[index]->SetLink(other == 0xffffffffu ? SquirrelValue{} : graph->Instances[other]);
			break;
		case 1:
			graph->Links[index]->Unlink();
			break;
		case 2:
			graph->Links[index]->DetachChildren();
			break;
		case 3:
			graph->Links[index]->Invalidate();
			break;
		case 4:
			Require(other < 8, "assign vector requires a source owner");
			graph->Links[index]->AssignLinks(*graph->Links[other]);
			break;
		default:
			throw std::runtime_error("unsupported affine link corpus operation");
		}

		compare(graph->Snapshot(), "after");
	}

	Require(input.get() == std::char_traits<char>::eof(), "affine link corpus has trailing bytes");
	std::cout << "Native affine links: " << records << " state records, " << words << " words, zero differences\n";
}

int main(int argc, char** argv) {
	try {
		CheckStrongChildrenAndWeakParent();
		CheckLiveAssignmentAndInvalidation();
		CheckExpiredParentAndNativeErrors();
		CheckSafetyAndRetirement();
		std::cout << "Four affine link fixture groups passed\n";
		if (argc == 2) {
			CompareNative(argv[1]);
		} else if (argc != 1) {
			throw std::runtime_error("affine_layer_links_tests [NATIVE_VECTOR]");
		}
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
