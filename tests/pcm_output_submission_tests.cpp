#include "sgre/audio/pcm_output_submission.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

static_assert(!std::is_default_constructible_v<sgre::audio::PcmOutputSubmission>);
static_assert(!std::is_default_constructible_v<sgre::audio::PcmOutputDeliveryReceipt>);

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static std::string Rejection(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error& error) {
		return error.what();
	}

	throw std::runtime_error("expected exact software-owner rejection did not occur");
}

static sgre::audio::PcmOutputRingDescription Description() {
	sgre::audio::PcmOutputRingDescription description;
	description.CapacityFrames = 4;
	description.MaximumTransferFrames = 4;
	description.Channels = 2;

	return description;
}

struct ActualBuffer {
	ActualBuffer() : Ring(Description()), Queue(8), Owner(Ring, &Queue, Enabled, Active, 24000000) {
	}
	sgre::audio::PcmOutputRing Ring;
	sgre::audio::PcmOutputDeliveryQueue Queue;
	std::atomic<bool> Enabled = false;
	std::atomic<std::uint32_t> Active = 0;
	sgre::audio::PcmOutputSubmissionOwner Owner;
};

// An authored local consumer over the real ring/queue; no device/source readiness is represented.
static void Deliver(ActualBuffer& buffer, std::uint64_t sequence, std::uint64_t first, std::span<float> output, std::uint64_t clock) {
	const auto actual = buffer.Ring.Read(output);
	sgre::audio::PcmOutputDelivery record;
	record.Sequence = sequence;
	record.FirstMasterFrame = first;
	record.RequestedFrames = static_cast<std::uint32_t>(output.size() / 2);
	record.CopiedFrames = static_cast<std::uint32_t>(actual.CopiedFrames);
	record.SilentFrames = static_cast<std::uint32_t>(actual.SilentFrames);
	record.CallbackObserved.Value = clock;
	Require(buffer.Queue.Publish(record) == sgre::audio::PcmDeliveryPublishStatus::Ready, "actual local consumer receipt publication must succeed");
}

static void TestSuccessfulWriteAndNoFalseProgress() {
	ActualBuffer buffer;
	const std::array<float, 6> first{1, -1, .5f, -.5f, .25f, -.25f};
	const auto submission = buffer.Owner.Submit(first);
	buffer.Owner.RequireSubmission(submission);
	Require(submission.Sequence() == 1 && submission.FirstMasterFrame() == 0 && submission.Frames() == 3 && buffer.Owner.SubmittedFrames() == 3 && buffer.Owner.DeliveredFrames() == 0 && !buffer.Owner.ReadDelivery(), "successful write identity cannot pretend actual callback consumption");
	const auto full = Rejection([&] {
		static_cast<void>(buffer.Owner.Submit(first));
	});
	Require(!full.empty() && buffer.Owner.SubmittedFrames() == 3, "failed ring write cannot commit a master interval or submission serial");
	std::array<float, 4> partial{};
	Deliver(buffer, 1, 0, partial, 24);
	Require(partial[0] == first[0] && partial[3] == first[3], "source/master PCM remains unchanged by submission identity");
	const auto receipt = buffer.Owner.ReadDelivery();
	Require(receipt && receipt->Observation().FirstMasterFrame == 0 && receipt->Observation().CopiedFrames == 2 && receipt->CallbackContinuous100ns() == 10 && buffer.Owner.DeliveredFrames() == 2, "only actual partial ring read commits copied master progress");
	buffer.Owner.RequireDelivery(*receipt);
	const std::array<float, 4> second{.125f, -.125f, .0625f, -.0625f};
	const auto next = buffer.Owner.Submit(second);
	Require(next.Sequence() == 2 && next.FirstMasterFrame() == 3 && next.Frames() == 2, "failed write must not create a gap before a real wrapped successful submission");
	std::array<float, 8> tail{};
	Deliver(buffer, 2, 2, tail, 48);
	const auto drained = buffer.Owner.ReadDelivery();
	Require(drained && drained->Observation().CopiedFrames == 3 && drained->Observation().SilentFrames == 1 && buffer.Owner.DeliveredFrames() == 5 && tail[0] == first[4] && tail[2] == second[0] && tail[5] == second[3] && tail[6] == 0 && tail[7] == 0, "copied data and underflow silence retain different master units across real ring wrap");
	Deliver(buffer, 3, 5, partial, 72);
	const auto silent = buffer.Owner.ReadDelivery();
	Require(silent && silent->Observation().CopiedFrames == 0 && silent->Observation().SilentFrames == 2 && buffer.Owner.DeliveredFrames() == 5, "pure silent delivery cannot invent more submitted source frames or EOF");
	buffer.Owner.Close();
}

static void TestOwnerIdentityAndRetirement() {
	ActualBuffer first;
	ActualBuffer second;
	const std::array<float, 2> source{1, -1};
	const auto written = first.Owner.Submit(source);
	Require(!Rejection([&] {
		second.Owner.RequireSubmission(written);
	}).empty(),
	    "foreign real output cannot authenticate another buffer's submission");
	std::array<float, 2> output{};
	Deliver(first, 1, 0, output, 0);
	const auto delivered = first.Owner.ReadDelivery();
	Require(delivered && !Rejection([&] {
		second.Owner.RequireDelivery(*delivered);
	}).empty(),
	    "a copied callback value remains qualified by its actual originating owner");
	first.Enabled.store(true);
	Require(!Rejection([&] {
		first.Owner.Close();
	}).empty(),
	    "software identity cannot retire while actual callback consumption remains enabled");
	first.Enabled.store(false);
	first.Active.store(1);
	Require(!Rejection([&] {
		first.Owner.Close();
	}).empty(),
	    "actual executing callback observation must block retirement");
	first.Active.store(0);
	first.Owner.Close();
	Require(!Rejection([&] {
		first.Owner.RequireSubmission(written);
	}).empty() &&
	        !Rejection([&] {
		        first.Owner.RequireDelivery(*delivered);
	        }).empty(),
	    "closed buffer cannot authenticate an old submission or delivery to a recycled owner");
	first.Owner.Close();
	second.Owner.Close();
}

static void TestFirstFailureAndIndependentClose() {
	ActualBuffer buffer;
	const std::array<float, 2> source{1, -1};
	static_cast<void>(buffer.Owner.Submit(source));
	std::array<float, 2> output{};
	Deliver(buffer, 1, 7, output, 24);
	const auto first = Rejection([&] {
		static_cast<void>(buffer.Owner.ReadDelivery());
	});
	Require(Rejection([&] {
		static_cast<void>(buffer.Owner.Submit(source));
	}) == first,
	    "unexpected receipt mapping must preserve its first failure through later operations");
	buffer.Owner.Close();
	ActualBuffer regression;
	static_cast<void>(regression.Owner.Submit(source));
	Deliver(regression, 1, 0, output, 48);
	Require(regression.Owner.ReadDelivery().has_value(), "first true delivery sets actual continuous epoch");
	Deliver(regression, 2, 1, output, 24);
	Require(!Rejection([&] {
		static_cast<void>(regression.Owner.ReadDelivery());
	}).empty(),
	    "callback continuous regression cannot become a source-clock correction");
	regression.Owner.Close();
}

static void TestThreadAndInputFinish() {
	ActualBuffer buffer;
	std::atomic<bool> rejected = false;
	const std::array<float, 2> source{1, -1};
	std::thread foreign([&] {
		try {
			static_cast<void>(buffer.Owner.Submit(source));
		} catch (const std::runtime_error&) {
			rejected.store(true);
		}
	});
	foreign.join();
	Require(rejected.load() && buffer.Owner.SubmittedFrames() == 0, "foreign producer must fail before actual ring mutation");
	const std::array<float, 2> invalid{std::numeric_limits<float>::quiet_NaN(), 0};
	Require(!Rejection([&] {
		static_cast<void>(buffer.Owner.Submit(invalid));
	}).empty() &&
	        buffer.Owner.SubmittedFrames() == 0,
	    "nonfinite input cannot publish a successful PCM identity");
	const auto written = buffer.Owner.Submit(source);
	buffer.Owner.FinishInput();
	Require(!Rejection([&] {
		static_cast<void>(buffer.Owner.Submit(source));
	}).empty(),
	    "finished submission input stays closed to ordinary producer writes");
	std::array<float, 4> output{};
	Deliver(buffer, 1, 0, output, 24);
	const auto receipt = buffer.Owner.ReadDelivery();
	Require(receipt && receipt->Observation().CopiedFrames == written.Frames() && receipt->Observation().SilentFrames == 1 && buffer.Owner.DeliveredFrames() == 1, "actual queued samples remain deliverable after input finish without turning silent tail into source progress");
	buffer.Owner.Close();
}

int main() {
	try {
		TestSuccessfulWriteAndNoFalseProgress();
		TestOwnerIdentityAndRetirement();
		TestFirstFailureAndIndependentClose();
		TestThreadAndInputFinish();
		std::cout << "four actual software submission/delivery identity groups passed; no device or fake source readiness\n";

		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}
}
