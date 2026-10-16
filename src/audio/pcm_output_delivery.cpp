#include "sgre/audio/pcm_output_delivery.hpp"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace sgre::audio {
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
	static_assert(std::is_trivially_copyable_v<PcmOutputDelivery>);

	PcmOutputDeliveryQueue::PcmOutputDeliveryQueue(std::size_t capacity) {
		constexpr std::size_t kMaximumRecords = 65536;
		if (capacity == 0 || capacity > kMaximumRecords) {
			throw std::runtime_error("PCM delivery queue requires an explicit bounded record capacity");
		}
		m_Records.resize(capacity);
	}

	PcmDeliveryPublishStatus PcmOutputDeliveryQueue::RequirePublishSpace() const noexcept {
		const auto write = m_Write.load(std::memory_order_relaxed);
		const auto read = m_Read.load(std::memory_order_acquire);
		if (write == std::numeric_limits<std::uint64_t>::max()) {
			return PcmDeliveryPublishStatus::CounterOverflow;
		}
		if (write - read == m_Records.size()) {
			return PcmDeliveryPublishStatus::Full;
		}

		return PcmDeliveryPublishStatus::Ready;
	}

	PcmDeliveryPublishStatus PcmOutputDeliveryQueue::Publish(const PcmOutputDelivery& record) noexcept {
		if (record.Sequence == 0 || record.CopiedFrames > record.RequestedFrames || record.SilentFrames != record.RequestedFrames - record.CopiedFrames || record.CopiedFrames > std::numeric_limits<std::uint64_t>::max() - record.FirstMasterFrame) {
			return PcmDeliveryPublishStatus::InvalidRecord;
		}
		const auto space = RequirePublishSpace();
		if (space != PcmDeliveryPublishStatus::Ready) {
			return space;
		}
		const auto write = m_Write.load(std::memory_order_relaxed);
		m_Records[static_cast<std::size_t>(write % m_Records.size())] = record;
		m_Write.store(write + 1, std::memory_order_release);

		return PcmDeliveryPublishStatus::Ready;
	}

	bool PcmOutputDeliveryQueue::TryRead(PcmOutputDelivery& record) noexcept {
		const auto read = m_Read.load(std::memory_order_relaxed);
		const auto write = m_Write.load(std::memory_order_acquire);
		if (read == write) {
			return false;
		}
		record = m_Records[static_cast<std::size_t>(read % m_Records.size())];
		m_Read.store(read + 1, std::memory_order_release);

		return true;
	}

	std::int64_t PcmContinuousTime100ns(PcmContinuousClockTicks counter, std::int64_t actual_frequency) {
		constexpr std::uint64_t kTicksPerSecond = 10000000;
		const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
		if (actual_frequency <= 0 || counter.Value > maximum) {
			throw std::runtime_error("PCM callback clock requires its nonnegative signed continuous sample and positive actual frequency");
		}
		const auto divisor = static_cast<std::uint64_t>(actual_frequency);
		const auto whole = counter.Value / divisor;
		const auto remainder = counter.Value % divisor;
		if (whole > maximum / kTicksPerSecond || remainder > maximum / kTicksPerSecond) {
			throw std::runtime_error("PCM continuous clock exceeds the actual host signed100ns projection");
		}
		const auto integral = whole * kTicksPerSecond;
		const auto fraction = remainder * kTicksPerSecond / divisor;
		if (fraction > maximum - integral) {
			throw std::runtime_error("PCM continuous clock exceeds the actual host signed100ns result");
		}

		return static_cast<std::int64_t>(integral + fraction);
	}
} // namespace sgre::audio
