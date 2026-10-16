#include "sgre/audio/sound_configuration_loads.hpp"

#include "sgre/core/background_calls.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
using sgre::audio::SoundConfigurationDiagnostic;
using sgre::audio::SoundConfigurationLoads;

static void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Operation>
static void RequireFailure(Operation operation) {
	try {
		operation();
	} catch (const std::runtime_error&) {
		return;
	}

	throw std::runtime_error("sound configuration fixture expected a supported-domain rejection");
}

static void WriteWord(Bytes& bytes, std::size_t offset, std::uint32_t value) {
	for (std::uint32_t index = 0; index < 4; ++index) {
		bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
	}
}

static Bytes ConfigurationPsb(float version) {
	Bytes bytes(44, 0);
	bytes[0] = 'P';
	bytes[1] = 'S';
	bytes[2] = 'B';
	bytes[4] = 3;
	WriteWord(bytes, 8, 44);
	WriteWord(bytes, 12, 44);
	// Independent PSB3 trie paths id/spec/version. The version path uses base128 to avoid spec's e/s nodes.
	std::array<std::uint8_t, 255> bases{};
	std::array<std::uint8_t, 255> parents{};
	bases[100] = 121;
	bases[99] = 122;
	bases[118] = 128;
	bases[229] = 128;
	bases[242] = 128;
	bases[243] = 128;
	bases[233] = 128;
	bases[239] = 128;
	bases[238] = 254;
	parents[100] = 105;
	parents[112] = 115;
	parents[101] = 112;
	parents[99] = 101;
	parents[121] = 100;
	parents[122] = 99;
	parents[229] = 118;
	parents[242] = 229;
	parents[243] = 242;
	parents[233] = 243;
	parents[239] = 233;
	parents[238] = 239;
	parents[254] = 238;
	bytes.insert(bytes.end(), {13, 255, 13});
	bytes.insert(bytes.end(), bases.begin(), bases.end());
	bytes.insert(bytes.end(), {13, 255, 13});
	bytes.insert(bytes.end(), parents.begin(), parents.end());
	bytes.insert(bytes.end(), {13, 3, 13, 121, 122, 254});
	WriteWord(bytes, 36, static_cast<std::uint32_t>(bytes.size()));

	// Object key indices [0,1,2], offsets [0,2,4], string atoms sound/win and an explicit little-endian float32 atom.
	bytes.insert(bytes.end(), {33, 13, 3, 13, 0, 1, 2, 13, 3, 13, 0, 2, 4, 21, 0, 21, 1, 30});
	const auto float_offset = bytes.size();
	bytes.resize(bytes.size() + 4);
	WriteWord(bytes, float_offset, std::bit_cast<std::uint32_t>(version));
	WriteWord(bytes, 16, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 2, 13, 0, 6});
	WriteWord(bytes, 20, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {'s', 'o', 'u', 'n', 'd', 0, 'w', 'i', 'n', 0});
	WriteWord(bytes, 24, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 1, 13, 0});
	WriteWord(bytes, 28, static_cast<std::uint32_t>(bytes.size()));
	bytes.insert(bytes.end(), {13, 1, 13, 4});
	WriteWord(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
	std::uint32_t first = 1;
	std::uint32_t second = 0;
	for (std::size_t index = 8; index < 40; ++index) {
		first += bytes[index];
		second += first;
	}
	WriteWord(bytes, 40, (second << 16) | first);
	bytes.insert(bytes.end(), {10, 20, 30, 40});
	const auto document = sgre::resource::ReadPsbMetadata(bytes, bytes.size());
	Require(document.Names == std::vector<std::string>{"id", "spec", "version"} && std::get<double>(document.Root.Find("version")->Data) == static_cast<double>(version) && document.Chunks.size() == 1 && document.Chunks[0].Offset == bytes.size() - 4, "independent configuration PSB differs from its stated identity/version/chunk graph");

	return bytes;
}

static Bytes WrappedConfiguration(const Bytes& decoded, std::string_view read_filename) {
	Require(decoded.size() >= 256 && decoded.size() < 65536, "configuration fixture exceeds its independent raw Zstandard frame bound");
	const auto extent = decoded.size() - 256;
	Bytes frame{0x28, 0xb5, 0x2f, 0xfd, 0x60, static_cast<std::uint8_t>(extent), static_cast<std::uint8_t>(extent >> 8)};
	const auto block = static_cast<std::uint32_t>((decoded.size() << 3) | 1);
	frame.insert(frame.end(), {static_cast<std::uint8_t>(block), static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block >> 16)});
	frame.insert(frame.end(), decoded.begin(), decoded.end());
	sgre::resource::ResourceCipher cipher(read_filename, "synthetic-seed", 131);
	cipher.Transform(frame);
	Bytes bytes{'m', 'z', 's', 0, 0, 0, 0, 0};
	WriteWord(bytes, 4, static_cast<std::uint32_t>(decoded.size()));
	bytes.insert(bytes.end(), frame.begin(), frame.end());

	return bytes;
}

static sgre::io::PreparedResourceFileRead Prepared(const std::filesystem::path& path, const Bytes& bytes, std::string read_filename, bool wrapped = false) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	for (const auto byte : bytes) {
		file.put(static_cast<char>(byte));
	}
	file.close();
	Require(static_cast<bool>(file), "configuration fixture could not write its bounded physical input");
	sgre::io::PreparedResourceFileRead source;
	source.Region.Filename = path;
	source.Region.Length = static_cast<std::uint32_t>(bytes.size());
	source.Region.MaxBytes = 4096;
	source.Format.Kind = sgre::io::ResourceFileKind::Psb;
	source.Format.Wrapped = wrapped;
	source.OriginalFilename = std::move(read_filename);
	source.Seed = "synthetic-seed";
	source.MaxDecodedBytes = 4096;
	source.MaxMetadataBytes = 4096;

	return source;
}

static void FinishReader(sgre::io::FileReadProcessor& processor, sgre::core::BackgroundCallManager& background) {
	processor.Update();
	std::promise<void> barrier;
	auto done = barrier.get_future();
	sgre::core::BackgroundCallTask after(background, sgre::core::BackgroundCallLane::BelowNormal, [&]() {
		barrier.set_value();
	});
	after.Update();
	background.PostLoop();
	done.get();
	after.Close();
}

static void TestMetadataBoundaries() {
	auto document = sgre::resource::ReadPsbMetadata(ConfigurationPsb(1.02f), 4096);
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == SoundConfigurationDiagnostic::Compatible, "compatible native float version must be accepted exactly");
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "another-spec") == SoundConfigurationDiagnostic::WrongSpec, "actual expected spec cannot be inferred from the host OS");
	for (const auto& [version, expected] : std::array<std::pair<float, SoundConfigurationDiagnostic>, 2>{{{std::nextafter(1.02f, 0.0f), SoundConfigurationDiagnostic::OldVersion}, {std::nextafter(1.02f, 2.0f), SoundConfigurationDiagnostic::NewVersion}}}) {
		document = sgre::resource::ReadPsbMetadata(ConfigurationPsb(version), 4096);
		Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == expected, "configuration version checks must preserve both adjacent float32 boundaries");
	}

	auto& fields = std::get<sgre::resource::PsbObject>(document.Root.Data);
	fields[2].second.Data = std::numeric_limits<double>::quiet_NaN();
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == SoundConfigurationDiagnostic::InvalidMetadata, "unsafe nonfinite metadata must remain a distinct safe rejection");
	fields[2].second.Data = true;
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == SoundConfigurationDiagnostic::OldVersion, "native boolean-to-float version conversion differs");
	fields[0].second.Data = std::string("sound_archive");
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == SoundConfigurationDiagnostic::IgnoredIdentity, "sound_archive is distinct from the sound configuration identity");
	fields.erase(fields.begin());
	Require(sgre::audio::InspectSoundConfigurationDocument(document, "win") == SoundConfigurationDiagnostic::IgnoredIdentity, "missing id follows the native completion callback's early return");
	RequireFailure([&]() {
		static_cast<void>(sgre::audio::InspectSoundConfigurationDocument(document, ""));
	});
}

static void TestDeliveryAndDeferredUnload(const std::filesystem::path& directory) {
	for (const bool wrapped : {false, true}) {
		const auto decoded = ConfigurationPsb(1.02f);
		const std::string filename = wrapped ? "dir\\configuration.psb" : "dir/configuration.psb";
		const auto read_filename = wrapped ? filename + ".m" : filename;
		const auto source = Prepared(directory / "configuration.bin", wrapped ? WrappedConfiguration(decoded, read_filename) : decoded, read_filename, wrapped);
		sgre::resource::ResourceCache cache(0);
		sgre::resource::ArchiveMounts mounts;
		sgre::core::BackgroundCallManager background;
		sgre::io::FileReadProcessor processor(cache, mounts, background);
		SoundConfigurationLoads loads(cache, mounts, processor, "win");
		Require(loads.LoadPrepared(filename, source) && !loads.LoadPrepared(filename, {}), "native exact-name idempotence must precede another request's admission");
		const auto handle = *cache.FindNamedResource(filename);
		Require(loads.GetLoading(filename) && !loads.GetLoaded(filename) && cache.Info(handle).References == 1 && loads.SerialLoads().Count() == 2 && loads.Info(filename).DirectoryPrefix == "dir", "new configuration must retain its real load/completion pair and exactly one manager reference");
		RequireFailure([&]() {
			loads.Close();
		});
		Require(loads.Unload(filename) && !loads.GetLoading(filename) && !loads.GetLoaded(filename) && loads.RetiringCount() == 1 && cache.Info(handle).References == 1, "map removal must retain the pending configuration, tasks and original reference");
		loads.AdvanceSerialLoads();
		Require(loads.RetiringCount() == 1 && loads.SerialLoads().Count() == 2, "an active load task must block its following completion and deferred configuration deletion");
		Require(loads.LoadPrepared(filename, source) && cache.Info(handle).References == 2 && processor.PendingRawCount() == 1, "new configuration after lookup removal coalesces through a second real reference while the old owner retires");
		FinishReader(processor, background);
		loads.AdvanceSerialLoads();
		Require(loads.GetLoaded(filename) && !loads.GetLoading(filename) && loads.RetiringCount() == 0 && loads.SerialLoads().Count() == 0 && cache.Info(handle).References == 1, "same serial step must deliver, complete, release both old task borrows and retire only the old configuration");
		Require(loads.Info(filename).Diagnostic == SoundConfigurationDiagnostic::Compatible && loads.File(filename).BackingStorage() == decoded && &loads.File(filename) == &cache.PsbFile(handle), "configuration must borrow the actual PSB/backing owner without acquiring a substitute pin");
		Require(loads.Unload(filename) && cache.Info(handle).References == 1 && loads.RetiringCount() == 1, "completed configuration must retain its original reference until postloop retirement");
		loads.AdvanceSerialLoads();
		Require(!cache.Contains(handle) && loads.RetiringCount() == 0 && loads.CanClose(), "zero-limit final config retirement must balance the original reference and preserve no task borrow");
		loads.Close();
		processor.Close();
		background.Close();
	}
}

static void TestDiagnosticsAndReadFailure(const std::filesystem::path& directory) {
	for (const bool malformed : {false, true}) {
		const auto source = Prepared(directory / "diagnostic.bin", malformed ? Bytes{1, 2, 3} : ConfigurationPsb(1.0f), "configuration.psb");
		sgre::resource::ResourceCache cache(0);
		sgre::resource::ArchiveMounts mounts;
		sgre::core::BackgroundCallManager background;
		sgre::io::FileReadProcessor processor(cache, mounts, background);
		SoundConfigurationLoads loads(cache, mounts, processor, "win");
		Require(loads.LoadPrepared("configuration.psb", source), "fixture did not admit its actual configuration request");
		FinishReader(processor, background);
		loads.AdvanceSerialLoads();
		Require(loads.GetLoaded("configuration.psb") == !malformed && loads.GetLoading("configuration.psb") == malformed && loads.Info("configuration.psb").Diagnostic == (malformed ? SoundConfigurationDiagnostic::FailedDelivery : SoundConfigurationDiagnostic::OldVersion), "metadata diagnostics preserve native completion while missing delivery never reports loaded success");
		if (malformed) {
			Require(!loads.Info("configuration.psb").Error.empty(), "actual failed PSB production requires its concrete parse diagnostic");
			RequireFailure([&]() {
				static_cast<void>(loads.File("configuration.psb"));
			});
		}

		Require(loads.Unload("configuration.psb"), "diagnosed configuration lookup must still unload");
		loads.AdvanceSerialLoads();
		Require(loads.RetiringCount() == 0, "safe failed delivery and completed incompatible metadata must both retire without forged playback success");
		loads.Close();
		processor.Close();
		background.Close();
	}
}

static void TestCacheTaskCancellation(const std::filesystem::path& directory) {
	for (const bool read_finished : {false, true}) {
		const auto source = Prepared(directory / "cancel.bin", ConfigurationPsb(1.02f), "configuration.psb");
		sgre::resource::ResourceCache cache(0);
		sgre::resource::ArchiveMounts mounts;
		sgre::core::BackgroundCallManager background;
		sgre::io::FileReadProcessor processor(cache, mounts, background);
		SoundConfigurationLoads loads(cache, mounts, processor, "win");
		Require(loads.LoadPrepared("configuration.psb", source), "fixture did not admit its actual cancel target");
		const auto handle = *cache.FindNamedResource("configuration.psb");
		if (read_finished) {
			FinishReader(processor, background);
		}

		Require(loads.CancelPendingCacheLoad("configuration.psb") && !loads.CancelPendingCacheLoad("configuration.psb") && cache.Info(handle).References == 0 && loads.SerialLoads().Count() == 2, "native cache-task cancel balances one reference exactly and retains callback borrows until serial unlink");
		if (!read_finished) {
			Require(processor.PendingRawCount() == 1, "cache-task cancellation cannot pretend to remove the native reader's still-queued work");
		}

		cache.Advance();
		loads.AdvanceSerialLoads();
		Require(!loads.GetLoaded("configuration.psb") && loads.GetLoading("configuration.psb") && loads.Info("configuration.psb").Diagnostic == SoundConfigurationDiagnostic::CancelledDelivery && loads.CanClose(), "cancelled null delivery must retain its separate safe terminal state without setting the native loaded latch");
		Require(loads.Unload("configuration.psb"), "cancelled configuration remains a live lookup until real unload");
		loads.AdvanceSerialLoads();
		loads.Close();
		processor.Close();
		background.Close();
		cache.Advance();
		Require(!cache.Contains(handle), "actual reader retirement must finish receiving-cell ownership after configuration task cancellation");
	}
}

static void TestAdmissionRollback(const std::filesystem::path& directory) {
	const auto source = Prepared(directory / "admission.bin", ConfigurationPsb(1.02f), "configuration.psb");
	sgre::resource::ResourceCache cache(0);
	sgre::resource::ArchiveMounts mounts;
	sgre::resource::ArchiveMounts wrong_mounts;
	sgre::core::BackgroundCallManager background;
	sgre::io::FileReadProcessor processor(cache, mounts, background);
	RequireFailure([&]() {
		SoundConfigurationLoads invalid(cache, wrong_mounts, processor, "win");
	});
	sgre::audio::SoundConfigurationLoadLimits limits;
	limits.MaxConfigurations = 1;
	SoundConfigurationLoads loads(cache, mounts, processor, "win", limits);
	auto invalid = source;
	invalid.Region.Filename.clear();
	RequireFailure([&]() {
		static_cast<void>(loads.LoadPrepared("configuration.psb", invalid));
	});
	Require(!cache.Contains("configuration.psb") && loads.LiveCount() == 0 && loads.SerialLoads().Count() == 0 && processor.PendingRawCount() == 0, "failed actual reader admission must retire unlinked callbacks and roll back the fresh acquired receiving slot");
	invalid = source;
	invalid.OriginalFilename = "changed-spelling.psb";
	RequireFailure([&]() {
		static_cast<void>(loads.LoadPrepared("configuration.psb", invalid));
	});
	Require(!cache.Contains("configuration.psb"), "unsupported identity must reject before any manager reference publication");
	Require(loads.LoadPrepared("configuration.psb", source) && loads.Unload("configuration.psb"), "fixture did not retain its real pending owner at the capacity boundary");
	RequireFailure([&]() {
		static_cast<void>(loads.LoadPrepared("configuration.psb", source));
	});
	Require(loads.RetiringCount() == 1 && processor.PendingRawCount() == 1, "retiring configurations must count against the concrete owner bound until their task borrows finish");
	FinishReader(processor, background);
	loads.AdvanceSerialLoads();
	loads.Close();
	processor.Close();
	background.Close();
}

int main(int argc, char** argv) {
	try {
		if (argc != 2) {
			throw std::runtime_error("sound configuration fixture requires an ignored output directory");
		}

		const std::filesystem::path directory(argv[1]);
		std::filesystem::create_directories(directory);
		TestMetadataBoundaries();
		TestDeliveryAndDeferredUnload(directory);
		TestDiagnosticsAndReadFailure(directory);
		TestCacheTaskCancellation(directory);
		TestAdmissionRollback(directory);
		std::cout << "sound configuration load fixtures passed (five groups)\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';

		return 1;
	}

	return 0;
}
