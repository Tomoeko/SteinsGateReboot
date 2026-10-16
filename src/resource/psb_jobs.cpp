#include "sgre/resource/psb_jobs.hpp"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace sgre::resource {
	struct PsbJobInput {
		CachedResourceHandle Handle;
		std::string Name;
		PsbResourceJobs::ReadOperation Read;
	};

	struct PsbJobDocument {
		CachedResourceHandle Handle;
		std::string Name;
		std::shared_ptr<const PsbDocument> Document;
	};

	struct PsbJobCompletion {
		PsbJobDocument Result;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
	};

	class PsbResourceJobs::Implementation {
	public:
		Implementation(ResourceCache& cache, PsbJobLimits limits) : Cache(cache), Limits(limits) {
			if (limits.MaxOutstandingJobs == 0 || limits.MaxOutstandingJobs > 1024 || limits.MaxDocumentSlots == 0 || limits.MaxDocumentSlots > 65536 || limits.MaxMetadataBytes == 0 || limits.MaxMetadataBytes > limits.MaxDecodedBytes || limits.MaxDecodedBytes > std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("PSB jobs require bounded positive queue/document/decoding limits");
			}

			Work.reserve(limits.MaxOutstandingJobs);
			Outstanding.reserve(limits.MaxOutstandingJobs);
			Documents.resize(limits.MaxDocumentSlots);
			DocumentIndices.reserve(limits.MaxDocumentSlots);
			Worker = std::thread([this] {
				Run();
			});
		}

		~Implementation() {
			{
				std::lock_guard lock(Mutex);
				Stopping = true;
			}
			Changed.notify_all();
			Worker.join();
			for (const auto handle : Outstanding) {
				if (Cache.Contains(handle) && Cache.Info(handle).Completion == ResourceCompletion::Pending) {
					Cache.Fail(handle, "PSB loading service shut down before publication");
				}
			}
			Cache.Advance();
		}

		ResourceCache& Cache;
		PsbJobLimits Limits;
		std::vector<PsbJobInput> Work;
		std::vector<CachedResourceHandle> Outstanding;
		std::vector<std::optional<PsbJobDocument>> Documents;
		std::vector<std::uint32_t> DocumentIndices;
		std::optional<PsbJobCompletion> Completed;
		std::mutex Mutex;
		std::condition_variable Changed;
		bool Stopping = false;
		std::thread Worker;

		PsbJobCompletion Decode(PsbJobInput input) const {
			PsbJobCompletion result;
			result.Result.Handle = input.Handle;
			result.Result.Name = std::move(input.Name);
			try {
				result.Bytes = input.Read();
				if (result.Bytes.size() > Limits.MaxDecodedBytes) {
					throw std::runtime_error("decoded PSB exceeds its job allocation limit");
				}

				const auto header = ReadPsbHeader(result.Bytes);
				if (header.DataOffset > result.Bytes.size() || header.DataOffset > Limits.MaxMetadataBytes) {
					throw std::runtime_error("decoded PSB metadata exceeds its job extent/allocation limit");
				}

				const auto metadata = std::span<const std::uint8_t>(result.Bytes).first(header.DataOffset);
				result.Result.Document = std::make_shared<const PsbDocument>(ReadPsbMetadata(metadata, result.Bytes.size()));
			} catch (const std::exception& error) {
				result.Error = std::string(std::string_view(error.what()).substr(0, 4096));
				if (result.Error.empty()) {
					result.Error = "PSB worker failed without diagnostic text";
				}
			} catch (...) {
				result.Error = "PSB worker encountered a non-standard exception";
			}

			if (!result.Error.empty()) {
				result.Bytes.clear();
				result.Result.Document.reset();
			}

			return result;
		}

		void Run() {
			while (true) {
				PsbJobInput input;
				{
					std::unique_lock lock(Mutex);
					Changed.wait(lock, [&] {
						return Stopping || !Work.empty();
					});
					if (Stopping) {
						return;
					}

					input = std::move(Work.front());
					Work.erase(Work.begin());
				}

				auto result = Decode(std::move(input));
				{
					std::unique_lock lock(Mutex);
					Changed.wait(lock, [&] {
						return Stopping || !Completed;
					});
					if (Stopping) {
						return;
					}

					Completed = std::move(result);
				}
			}
		}
	};

	PsbResourceJobs::PsbResourceJobs(ResourceCache& cache, PsbJobLimits limits) : m_Implementation(std::make_unique<Implementation>(cache, limits)) {
	}

	PsbResourceJobs::~PsbResourceJobs() = default;

	void PsbResourceJobs::Queue(CachedResourceHandle resource, ReadOperation read) {
		auto& state = *m_Implementation;
		const auto info = state.Cache.Info(resource);
		if (!read || info.Residency != ResourceResidency::Loading || info.Completion != ResourceCompletion::Pending || resource.Index >= state.Limits.MaxDocumentSlots) {
			throw std::runtime_error("PSB job requires a bounded pending cache handle and an owned reader");
		}

		if (state.Outstanding.size() == state.Limits.MaxOutstandingJobs || std::find(state.Outstanding.begin(), state.Outstanding.end(), resource) != state.Outstanding.end()) {
			throw std::runtime_error("PSB job queue is full or the handle is already queued");
		}

		PsbJobInput input;
		input.Handle = resource;
		input.Name = std::string(state.Cache.Name(resource));
		input.Read = std::move(read);
		state.Outstanding.push_back(resource);
		{
			std::lock_guard lock(state.Mutex);
			state.Work.push_back(std::move(input));
		}
		state.Changed.notify_one();
	}

	bool PsbResourceJobs::PublishCompleted() {
		auto& state = *m_Implementation;
		std::optional<PsbJobCompletion> completed;
		{
			std::lock_guard lock(state.Mutex);
			if (!state.Completed) {
				return false;
			}

			completed = std::move(state.Completed);
			state.Completed.reset();
		}
		state.Changed.notify_one();
		const auto handle = completed->Result.Handle;
		std::erase(state.Outstanding, handle);
		if (!completed->Error.empty()) {
			state.Cache.Fail(handle, std::move(completed->Error));
		} else {
			const auto charge = static_cast<std::uint32_t>(completed->Bytes.size());
			try {
				state.Cache.Complete(handle, std::move(completed->Bytes), charge);
			} catch (const std::exception& error) {
				state.Cache.Fail(handle, std::string(std::string_view(error.what()).substr(0, 4096)));
				state.Cache.Advance();

				return true;
			}
			auto& document = state.Documents[handle.Index];
			if (!document) {
				state.DocumentIndices.push_back(handle.Index);
			}

			document = std::move(completed->Result);
		}
		state.Cache.Advance();

		return true;
	}

	std::size_t PsbResourceJobs::OutstandingJobs() const {
		return m_Implementation->Outstanding.size();
	}

	std::shared_ptr<const PsbDocument> PsbResourceJobs::Document(CachedResourceHandle resource) const {
		const auto& state = *m_Implementation;
		const auto info = state.Cache.Info(resource);
		if (info.Residency != ResourceResidency::Active || info.Completion != ResourceCompletion::Succeeded || info.References == 0 || resource.Index >= state.Documents.size()) {
			throw std::runtime_error("PSB document requires an acquired successful active job result");
		}

		const auto& document = state.Documents[resource.Index];
		if (!document || document->Handle != resource || !document->Document) {
			throw std::runtime_error("PSB document belongs to a missing or superseded cache generation");
		}

		return document->Document;
	}

	void PsbResourceJobs::CollectEvictedDocuments() {
		auto& state = *m_Implementation;
		std::erase_if(state.DocumentIndices, [&](auto index) {
			auto& document = state.Documents[index];
			const bool retained = state.Cache.Contains(document->Handle);
			if (!retained) {
				document.reset();
			}

			return !retained;
		});
	}
} // namespace sgre::resource
