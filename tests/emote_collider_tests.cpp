#include "sgre/scene/emote_collider.hpp"
#include "sgre/scene/emote_soft_pendulum.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static void Require(bool condition, std::string_view message) {
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

static void RequireNear(float actual, float expected, std::string_view message) {
	if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00002f) {
		throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
	}
}

template <class Operation>
static void RequireFailure(Operation operation, std::string_view message) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		Require(std::string_view(error.what()).find(message) != std::string_view::npos, error.what());
		return;
	}
	throw std::runtime_error("collider accepted an unsupported physical input");
}

static void PrepareStatic(sgre::scene::EmoteCollider& collider, const sgre::scene::EmoteColliderPosture& posture = {}) {
	collider.SetEnabled(true);
	collider.SetPosture(posture);
	collider.ResetMotion();
	collider.PrepareSubsteps(1);
	collider.AdvanceSubstep();
}

static void TestThreeActualShapesAndSweptContact() {
	sgre::scene::EmoteColliderDefinition definition;
	definition.First = {0, 0};
	definition.Second = {4, 0};
	sgre::scene::EmoteCollider plane(definition);
	PrepareStatic(plane);
	sgre::resource::MotionPoint point{2, 3};
	Require(plane.Project(point, {}, 0.25f), "directed half-plane contacts its authored solid side");
	RequireNear(point.X, 2, "half-plane keeps its tangent coordinate");
	RequireNear(point.Y, -0.25f, "particle radius offsets the actual directed boundary");
	point = {2, -1};
	Require(!plane.Project(point, {}, 0.25f), "half-plane leaves its free side untouched");
	plane.ClearContact();
	Require(!plane.Contacted(), "actual frame contact-reset clears the retained touched byte");
	plane.SetEnabled(false);
	point = {2, 3};
	Require(!plane.Project(point, {}, 0) && point.Y == 3, "inactive collider retains the unprojected actual point");

	definition.Kind = sgre::scene::EmoteColliderKind::SolidEllipse;
	definition.Radius = {2, 1};
	sgre::scene::EmoteCollider solid(definition);
	PrepareStatic(solid);
	point = {1, 0};
	Require(solid.Project(point, {}, 0.25f), "solid ellipse projects an interior particle");
	RequireNear(point.X, 2.25f, "ellipse horizontal radius expands by actual particle radius");
	RequireNear(point.Y, 0, "axis contact has no invented transverse displacement");
	point = {0, 0.5f};
	Require(solid.Project(point, {}, 0), "solid ellipse uses actual second radius");
	RequireNear(point.Y, 1, "vertical ellipse contact");

	definition.Kind = sgre::scene::EmoteColliderKind::InteriorEllipse;
	sgre::scene::EmoteCollider interior(definition);
	PrepareStatic(interior);
	point = {3, 0};
	Require(interior.Project(point, {}, 0.25f), "interior ellipse confines a particle outside its usable boundary");
	RequireNear(point.X, 1.75f, "interior ellipse contracts instead of expanding");
	point = {0.25f, 0.25f};
	Require(!interior.Project(point, {}, 0), "interior ellipse does not displace a contained particle");
	RequireFailure([&] {
		static_cast<void>(interior.Project(point, {}, 2));
	},
	    "oversized interior-particle");

	definition.Kind = sgre::scene::EmoteColliderKind::SolidEllipse;
	definition.Radius = {1, 1};
	sgre::scene::EmoteCollider swept(definition);
	PrepareStatic(swept);
	point = {2, 0};
	Require(swept.Project(point, {3, 0}, 0), "native sweep detects a traversed collider even when the final point is outside");
	RequireNear(point.X, -1, "sweep selects its first sampled collision boundary");
}

static void TestRealPostureInterpolationAndRotation() {
	sgre::scene::EmoteColliderDefinition definition;
	definition.Kind = sgre::scene::EmoteColliderKind::SolidEllipse;
	definition.Radius = {2, 1};
	sgre::scene::EmoteCollider collider(definition);
	sgre::scene::EmoteColliderPosture posture;
	posture.Coordinate = {4, 6};
	posture.Rotation = 1.57079637050628662109375f;
	posture.Cosine = 0;
	posture.Sine = 1;
	PrepareStatic(collider, posture);
	sgre::resource::MotionPoint point{4, 7};
	Require(collider.Project(point, {}, 0), "translated and rotated ellipse contacts in its actual local axes");
	RequireNear(point.X, 4, "rotated ellipse uses actual world X");
	RequireNear(point.Y, 8, "rotated horizontal radius becomes world vertical");
	posture.Coordinate = {6, 10};
	collider.SetPosture(posture);
	collider.PrepareSubsteps(2);
	collider.AdvanceSubstep();
	RequireNear(collider.InterpolatedPosture().Coordinate.X, 5, "first real translation substep");
	RequireNear(collider.InterpolatedPosture().Coordinate.Y, 8, "first real translation substep Y");
	// A different entity restarts the shared collider from the same owner frame, not the preceding entity's end.
	collider.PrepareSubsteps(2);
	collider.AdvanceSubstep();
	RequireNear(collider.InterpolatedPosture().Coordinate.X, 5, "shared collider interpolation restarts per actual entity");
	collider.AdvanceSubstep();
	RequireNear(collider.InterpolatedPosture().Coordinate.X, 6, "last real translation substep");
	RequireNear(collider.InterpolatedPosture().Coordinate.Y, 10, "last real translation substep Y");
}

static void TestConnectedSoftResponseModes() {
	for (std::uint32_t hardness = 0; hardness != 2; ++hardness) {
		sgre::scene::EmoteColliderDefinition boundary;
		boundary.First = {-4, 0.5f};
		boundary.Second = {4, 0.5f};
		sgre::scene::EmoteCollider collider(boundary);
		PrepareStatic(collider);
		sgre::scene::EmoteSoftPendulumDefinition definition;
		definition.Acceleration = {0, 1, 0};
		definition.ParticleCount = 3;
		definition.Length = 2;
		definition.CollisionHardness = hardness;
		definition.CollisionRepulsion = 100;
		sgre::scene::EmoteSoftPendulum baseline(definition);
		sgre::scene::EmoteSoftPendulum model(definition);
		model.AttachCollider(collider);
		for (auto* owner : {&baseline, &model}) {
			owner->SetPosture({});
			owner->Initialize();
			owner->ClearForcesAndMeasureBounds();
			owner->Step(0.125f);
		}
		Require(collider.Contacted(), "the real physical-model step must call its actual attached collider");
		bool differs = false;
		bool collided = false;
		for (std::size_t index = 0; index != model.Particles().size(); ++index) {
			const auto& particle = model.Particles()[index];
			collided = collided || particle.Collided;
			for (std::size_t axis = 0; axis != 3; ++axis) {
				differs = differs || particle.Position[axis] != baseline.Particles()[index].Position[axis];
				Require(std::isfinite(particle.Position[axis]), "actual constrained particles remain finite");
			}
		}
		Require(differs, "collision attachment must change genuine solved particle positions");
		Require(collided == (hardness == 0), "projection mode marks native collision flags; velocity-response mode has no such write");
	}
}

static void TestPhysicalDomainGuards() {
	sgre::scene::EmoteColliderDefinition definition;
	RequireFailure([&] {
		sgre::scene::EmoteCollider invalid(definition);
	},
	    "distinct authored endpoints");
	definition.Kind = sgre::scene::EmoteColliderKind::SolidEllipse;
	definition.Radius.X = -1;
	RequireFailure([&] {
		sgre::scene::EmoteCollider invalid(definition);
	},
	    "positive ellipse radii");
	definition.Radius = {1, 1};
	sgre::scene::EmoteCollider collider(definition);
	RequireFailure([&] {
		collider.PrepareSubsteps(0);
	},
	    "bounded positive native substeps");
	PrepareStatic(collider);
	sgre::resource::MotionPoint point{std::numeric_limits<float>::quiet_NaN(), 0};
	RequireFailure([&] {
		static_cast<void>(collider.Project(point, {}, 0));
	},
	    "finite actual points");
}

class NativeVectorReader {
public:
	explicit NativeVectorReader(const char* path) {
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		const auto size = input.tellg();
		Require(input && size > 0 && size <= 1024 * 1024, "native collider vector file must be bounded and readable");
		m_Bytes.resize(static_cast<std::size_t>(size));
		input.seekg(0);
		input.read(m_Bytes.data(), static_cast<std::streamsize>(m_Bytes.size()));
		Require(static_cast<bool>(input), "native collider vector file is truncated");
	}

	std::uint32_t U32() {
		Require(m_Offset <= m_Bytes.size() && 4 <= m_Bytes.size() - m_Offset, "native collider vector word exceeds file");
		std::uint32_t result = 0;
		for (std::uint32_t byte = 0; byte != 4; ++byte) {
			result |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(m_Bytes[m_Offset++])) << (byte * 8);
		}
		return result;
	}

	std::uint64_t U64() {
		const auto low = U32();
		const auto high = U32();
		return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
	}

	float Float() {
		return std::bit_cast<float>(U32());
	}

	void RequireEnd() const {
		Require(m_Offset == m_Bytes.size(), "native collider vector has trailing or missing fields");
	}

private:
	std::vector<char> m_Bytes;
	std::size_t m_Offset = 0;
};

static sgre::scene::EmoteColliderDefinition ReadNativeDefinition(NativeVectorReader& input) {
	sgre::scene::EmoteColliderDefinition definition;
	definition.Kind = static_cast<sgre::scene::EmoteColliderKind>(input.U32());
	definition.First = {input.Float(), input.Float()};
	definition.Second = {input.Float(), input.Float()};
	definition.Radius = {input.Float(), input.Float()};
	definition.Rotation = input.Float();
	return definition;
}

static sgre::scene::EmoteColliderPosture ReadNativePosture(NativeVectorReader& input) {
	sgre::scene::EmoteColliderPosture posture;
	posture.Coordinate = {input.Float(), input.Float()};
	posture.Rotation = input.Float();
	posture.Cosine = input.Float();
	posture.Sine = input.Float();
	return posture;
}

static void TestOriginalColliderLeaves(const char* path) {
	NativeVectorReader input(path);
	Require(input.U32() == 0x314c4345, "native collider leaf signature differs");
	const auto count = input.U32();
	Require(count > 0 && count <= 256, "native collider case count exceeds bounded witness");
	std::size_t compared = 0;
	for (std::size_t index = 0; index != count; ++index) {
		const auto definition = ReadNativeDefinition(input);
		const auto previous = ReadNativePosture(input);
		const auto current = ReadNativePosture(input);
		sgre::resource::MotionPoint point{input.Float(), input.Float()};
		const sgre::resource::MotionPoint displacement{input.Float(), input.Float()};
		const auto radius = input.Float();
		const auto expected_contact = input.U32();
		const std::array<std::uint32_t, 2> expected{input.U32(), input.U32()};
		std::array<std::uint32_t, 5> expected_posture{};
		for (auto& word : expected_posture) {
			word = input.U32();
		}
		sgre::scene::EmoteCollider collider(definition);
		collider.SetEnabled(true);
		collider.SetPosture(previous);
		collider.ResetMotion();
		collider.SetPosture(current);
		collider.PrepareSubsteps(1);
		collider.AdvanceSubstep();
		const auto& interpolated = collider.InterpolatedPosture();
		const std::array<float, 5> actual_posture{interpolated.Coordinate.X, interpolated.Coordinate.Y, interpolated.Rotation, interpolated.Cosine, interpolated.Sine};
		for (std::size_t word = 0; word != actual_posture.size(); ++word) {
			Require(std::bit_cast<std::uint32_t>(actual_posture[word]) == expected_posture[word], "actual interpolation differs from original instructions at case " + std::to_string(index));
		}
		Require(static_cast<std::uint32_t>(collider.Project(point, displacement, radius)) == expected_contact, "actual contact branch differs from original instructions at case " + std::to_string(index));
		Require(std::bit_cast<std::uint32_t>(point.X) == expected[0] && std::bit_cast<std::uint32_t>(point.Y) == expected[1], "actual projection differs from original instructions at case " + std::to_string(index));
		compared += actual_posture.size() + 2;
	}
	input.RequireEnd();
	std::cout << "Original collider leaves: " << count << " cases, " << compared << " float words bit exact\n";
}

static std::array<double, 19> ParticleState(const sgre::scene::EmoteSoftPendulumParticle& particle) {
	return {particle.Mass, particle.InverseMass, particle.Previous[0], particle.Previous[1], particle.Previous[2], particle.Predicted[0], particle.Predicted[1], particle.Predicted[2], particle.Position[0], particle.Position[1], particle.Position[2], particle.Velocity[0], particle.Velocity[1], particle.Velocity[2], particle.Force[0], particle.Force[1], particle.Force[2], particle.Friction, particle.AccelerationProjection};
}

static void TestOriginalSoftCollisionPhase(const char* path) {
	NativeVectorReader input(path);
	Require(input.U32() == 0x31534345, "native Soft collision signature differs");
	const auto count = input.U32();
	Require(count > 0 && count <= 256, "native Soft collision case count exceeds bounded witness");
	std::size_t compared = 0;
	std::size_t steps = 0;
	for (std::size_t index = 0; index != count; ++index) {
		const auto particle_count = input.U32();
		const auto hardness = input.U32();
		const auto adhoc = input.U32();
		Require(particle_count >= 2 && particle_count <= 16 && hardness <= 1 && adhoc <= 1, "native Soft collision definition exceeds finite witness domain");
		const auto collider_definition = ReadNativeDefinition(input);
		const auto step_count = input.U32();
		Require(step_count > 0 && step_count <= 64, "native Soft collision step count exceeds bounded witness");
		sgre::scene::EmoteCollider collider(collider_definition);
		collider.SetEnabled(true);
		collider.SetPosture({});
		collider.ResetMotion();
		collider.SetPosture({});
		sgre::scene::EmoteSoftPendulumDefinition definition;
		definition.Acceleration = {0, 1, 0};
		definition.ParticleCount = particle_count;
		definition.Length = static_cast<float>(particle_count - 1);
		definition.ControlRadius = 0.1f;
		definition.CollisionHardness = hardness;
		definition.CollisionRepulsion = 100;
		definition.AdhocSimulation = adhoc != 0;
		sgre::scene::EmoteSoftPendulum model(definition);
		model.AttachCollider(collider);
		model.SetPosture({});
		model.Initialize();
		std::vector<std::array<float, 3>> forces(particle_count);
		std::vector<std::uint32_t> flags(particle_count);
		std::vector<std::array<std::uint64_t, 19>> expected(particle_count);
		for (std::size_t step = 0; step != step_count; ++step) {
			const auto seconds = input.Float();
			const auto expected_contact = input.U32();
			for (std::size_t particle = 0; particle != particle_count; ++particle) {
				for (auto& axis : forces[particle]) {
					axis = input.Float();
				}
				flags[particle] = input.U32();
				for (auto& word : expected[particle]) {
					word = input.U64();
				}
			}
			collider.PrepareSubsteps(1);
			collider.AdvanceSubstep();
			collider.ClearContact();
			model.ClearForcesAndMeasureBounds();
			for (std::size_t particle = 1; particle != particle_count; ++particle) {
				model.AddForce(particle, forces[particle]);
			}
			model.Step(seconds);
			Require(static_cast<std::uint32_t>(collider.Contacted()) == expected_contact, "native Soft contact branch differs at case " + std::to_string(index) + " step " + std::to_string(step));
			for (std::size_t particle = 0; particle != particle_count; ++particle) {
				const auto& actual = model.Particles()[particle];
				Require(static_cast<std::uint32_t>(actual.Collided) == flags[particle], "native Soft per-particle contact flag differs");
				const auto state = ParticleState(actual);
				for (std::size_t word = 0; word != state.size(); ++word) {
					Require(std::bit_cast<std::uint64_t>(state[word]) == expected[particle][word], "native Soft collision state differs at case " + std::to_string(index) + " step " + std::to_string(step) + " particle " + std::to_string(particle) + " word " + std::to_string(word));
					++compared;
				}
			}
			++steps;
		}
	}
	input.RequireEnd();
	std::cout << "Original Soft collision phase: " << count << " sequences, " << steps << " steps, " << compared << " double words bit exact\n";
}

int main(int argc, char** argv) {
	try {
		Require(argc == 1 || argc == 3, "expected optional native leaf and Soft vector paths");
		TestThreeActualShapesAndSweptContact();
		TestRealPostureInterpolationAndRotation();
		TestConnectedSoftResponseModes();
		TestPhysicalDomainGuards();
		if (argc == 3) {
			TestOriginalColliderLeaves(argv[1]);
			TestOriginalSoftCollisionPhase(argv[2]);
		}
		std::cout << "Emote collider shapes, sweep, actual owner interpolation and connected Soft response phases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
