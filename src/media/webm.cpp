#include "sgre/media/webm.hpp"

#include "sgre/io/file_reader.hpp"
#include "sgre/resource/resource_cipher.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace sgre::media {

	struct EbmlInteger {
		std::uint64_t Value = 0;
		std::size_t Length = 0;
		bool Unknown = false;
	};

	static EbmlInteger ReadVint(std::span<const std::uint8_t> bytes, std::size_t offset, bool identifier = false) {
		if (offset >= bytes.size() || bytes[offset] == 0) {
			throw std::runtime_error("invalid or truncated EBML variable integer");
		}

		std::uint8_t marker = 0x80;
		std::size_t length = 1;
		while ((bytes[offset] & marker) == 0) {
			marker >>= 1;
			++length;
		}

		if (length > (identifier ? 4U : 8U) || length > bytes.size() - offset) {
			throw std::runtime_error("EBML variable integer exceeds length limit");
		}

		EbmlInteger result;
		result.Length = length;
		result.Value = identifier ? bytes[offset] : bytes[offset] & (marker - 1);
		for (std::size_t index = 1; index < length; ++index) {
			result.Value = (result.Value << 8) | bytes[offset + index];
		}

		result.Unknown = !identifier && result.Value == ((std::uint64_t{1} << (7 * length)) - 1);

		return result;
	}

	static std::uint64_t ReadBigEndian(std::span<const std::uint8_t> bytes) {
		if (bytes.empty() || bytes.size() > 8) {
			throw std::runtime_error("EBML integer must contain one to eight bytes");
		}

		std::uint64_t value = 0;
		for (const auto byte : bytes) {
			value = (value << 8) | byte;
		}

		return value;
	}

	static double ReadFloat(std::span<const std::uint8_t> bytes) {
		const auto bits = ReadBigEndian(bytes);
		double value = 0;
		if (bytes.size() == 4) {
			value = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
		} else if (bytes.size() == 8) {
			value = std::bit_cast<double>(bits);
		} else {
			throw std::runtime_error("EBML float must contain four or eight bytes");
		}

		if (!std::isfinite(value) || value < 0) {
			throw std::runtime_error("EBML media property has an invalid floating value");
		}

		return value;
	}

	static std::int64_t ReadSigned(std::span<const std::uint8_t> bytes) {
		std::uint64_t value = ReadBigEndian(bytes);
		if (bytes.size() < 8 && (bytes.front() & 0x80) != 0) {
			value |= std::numeric_limits<std::uint64_t>::max() << (bytes.size() * 8);
		}

		return std::bit_cast<std::int64_t>(value);
	}

	struct EbmlElement {
		std::uint64_t Id = 0;
		std::uint64_t Data = 0;
		std::uint64_t End = 0;
		bool UnknownSize = false;
	};

	class WebmReader::Implementation {
	public:
		Implementation(const std::filesystem::path& path, std::optional<std::string_view> seed, WebmLimits limits) : m_Input(path), m_Limits(limits) {
			if (seed) {
				m_Cipher.emplace(path.filename().string(), *seed, 4096);
			}

			if (limits.MaxMetadataBytes == 0 || limits.MaxPacketBytes == 0 || limits.MaxTracks == 0 || limits.MaxElements == 0) {
				throw std::runtime_error("WebM limits must be positive");
			}

			ReadHeader();
		}

		std::vector<WebmTrack> Tracks;
		std::uint64_t TimestampScale = 1000000;
		double Duration = 0;

		void Rewind() {
			m_Position = m_FirstCluster;
			m_ClusterEnd = m_FirstCluster;
			m_ClusterTime = 0;
			m_UnknownCluster = false;
			m_HasClusterTimestamp = false;
			m_ElementCount = 0;
		}

		std::optional<WebmPacket> NextPacket() {
			while (m_Position < m_SegmentEnd) {
				const auto element = ReadElement(m_Position, m_ClusterEnd > m_Position ? m_ClusterEnd : m_SegmentEnd);
				if (element.Id == 0x1f43b675) {
					m_Position = element.Data;
					m_ClusterEnd = element.End;
					m_UnknownCluster = element.UnknownSize;
					m_ClusterTime = 0;
					m_HasClusterTimestamp = false;
					continue;
				}

				m_Position = element.End;
				if (element.Id == 0xe7 && m_ClusterEnd >= element.End) {
					m_ClusterTime = ReadUnsigned(element);
					m_HasClusterTimestamp = true;
				} else if (element.Id == 0xa3) {
					return ReadBlock(element, true);
				} else if (element.Id == 0xa0) {
					return ReadBlockGroup(element);
				}

				if (!m_UnknownCluster && m_Position == m_ClusterEnd) {
					m_ClusterEnd = 0;
				}
			}

			return std::nullopt;
		}

	private:
		std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t size) {
			auto bytes = m_Input.Read(offset, size);
			if (m_Cipher) {
				m_Cipher->Transform(bytes, offset);
			}

			return bytes;
		}

		EbmlElement ReadElement(std::uint64_t offset, std::uint64_t parent_end) {
			if (offset >= parent_end || parent_end > m_Input.Size() || ++m_ElementCount > m_Limits.MaxElements) {
				throw std::runtime_error("WebM element exceeds its parent or the work limit at offset " + std::to_string(offset));
			}

			const auto bytes = Read(offset, static_cast<std::size_t>(std::min<std::uint64_t>(12, parent_end - offset)));
			const auto id = ReadVint(bytes, 0, true);
			const auto size = ReadVint(bytes, id.Length);
			EbmlElement element;
			element.Id = id.Value;
			element.Data = offset + id.Length + size.Length;
			element.UnknownSize = size.Unknown;
			if (size.Unknown && element.Id != 0x18538067 && element.Id != 0x1f43b675) {
				throw std::runtime_error("unknown EBML size is unsupported for this element at offset " + std::to_string(offset));
			}

			if (!size.Unknown && size.Value > parent_end - element.Data) {
				throw std::runtime_error("WebM element length exceeds its parent at offset " + std::to_string(offset));
			}

			element.End = size.Unknown ? parent_end : element.Data + size.Value;

			return element;
		}

		std::vector<std::uint8_t> ReadPayload(const EbmlElement& element, std::size_t limit) {
			if (element.End - element.Data > limit) {
				throw std::runtime_error("WebM payload exceeds its allocation limit at offset " + std::to_string(element.Data));
			}

			return Read(element.Data, static_cast<std::size_t>(element.End - element.Data));
		}

		std::uint64_t ReadUnsigned(const EbmlElement& element) {
			return ReadBigEndian(ReadPayload(element, 8));
		}

		std::uint32_t ReadDimension(const EbmlElement& element) {
			const auto value = ReadUnsigned(element);
			if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
				throw std::runtime_error("WebM dimension/channel count exceeds its range");
			}

			return static_cast<std::uint32_t>(value);
		}

		void ReadHeader() {
			const auto header = ReadElement(0, m_Input.Size());
			if (header.Id != 0x1a45dfa3 || header.End - header.Data > m_Limits.MaxMetadataBytes) {
				throw std::runtime_error("movie is missing a bounded EBML header");
			}

			bool webm = false;
			for (auto offset = header.Data; offset < header.End;) {
				const auto element = ReadElement(offset, header.End);
				if (element.Id == 0x4282) {
					const auto bytes = ReadPayload(element, 16);
					webm = std::string(bytes.begin(), bytes.end()) == "webm";
				} else if ((element.Id == 0x42f2 && ReadUnsigned(element) > 4) || (element.Id == 0x42f3 && ReadUnsigned(element) > 8) || (element.Id == 0x42f7 && ReadUnsigned(element) > 1) || (element.Id == 0x4285 && ReadUnsigned(element) > 4)) {
					throw std::runtime_error("EBML/WebM version or integer widths are unsupported");
				}

				offset = element.End;
			}

			if (!webm) {
				throw std::runtime_error("EBML document type must be webm");
			}

			auto offset = header.End;
			EbmlElement segment;
			do {
				segment = ReadElement(offset, m_Input.Size());
				offset = segment.End;
			} while (segment.Id == 0xec || segment.Id == 0xbf);

			if (segment.Id != 0x18538067) {
				throw std::runtime_error("WebM header is not followed by a Segment");
			}

			m_SegmentEnd = segment.End;
			m_FirstCluster = segment.End;
			for (offset = segment.Data; offset < segment.End;) {
				const auto element = ReadElement(offset, segment.End);
				if (element.Id == 0x1f43b675) {
					m_FirstCluster = offset;
					break;
				}

				if (element.Id == 0x1549a966) {
					ReadInfo(element);
				} else if (element.Id == 0x1654ae6b) {
					ReadTracks(element);
				}

				offset = element.End;
			}

			if (Tracks.empty()) {
				throw std::runtime_error("WebM requires track metadata before its first cluster");
			}

			if (!std::isfinite(Duration * static_cast<double>(TimestampScale))) {
				throw std::runtime_error("WebM duration exceeds its range");
			}

			Duration *= static_cast<double>(TimestampScale);
			Rewind();
		}

		void ReadInfo(const EbmlElement& info) {
			if (info.End - info.Data > m_Limits.MaxMetadataBytes) {
				throw std::runtime_error("WebM Info exceeds metadata limit");
			}

			for (auto offset = info.Data; offset < info.End;) {
				const auto element = ReadElement(offset, info.End);
				if (element.Id == 0x2ad7b1) {
					TimestampScale = ReadUnsigned(element);
					if (TimestampScale == 0 || TimestampScale > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
						throw std::runtime_error("WebM TimestampScale is outside supported range");
					}
				} else if (element.Id == 0x4489) {
					Duration = ReadFloat(ReadPayload(element, 8));
				}

				offset = element.End;
			}
		}

		void ReadTracks(const EbmlElement& tracks) {
			if (tracks.End - tracks.Data > m_Limits.MaxMetadataBytes) {
				throw std::runtime_error("WebM Tracks exceeds metadata limit");
			}

			for (auto offset = tracks.Data; offset < tracks.End;) {
				const auto element = ReadElement(offset, tracks.End);
				if (element.Id == 0xae) {
					if (Tracks.size() >= m_Limits.MaxTracks) {
						throw std::runtime_error("WebM track count exceeds limit");
					}

					WebmTrack track;
					ReadTrackFields(element, track, 0);
					if (track.Number == 0 || track.Codec.empty() || (track.Type == 1 && (track.Width == 0 || track.Height == 0))) {
						throw std::runtime_error("WebM track is missing required identity/codec/dimensions");
					}

					if (std::any_of(Tracks.begin(), Tracks.end(), [&](const WebmTrack& previous) {
						    return previous.Number == track.Number;
					    })) {
						throw std::runtime_error("WebM track number is duplicated");
					}

					Tracks.push_back(std::move(track));
				}

				offset = element.End;
			}
		}

		void ReadTrackFields(const EbmlElement& parent, WebmTrack& track, std::size_t depth) {
			if (depth > 2) {
				throw std::runtime_error("WebM track fields exceed recursion limit");
			}

			for (auto offset = parent.Data; offset < parent.End;) {
				const auto element = ReadElement(offset, parent.End);
				if (element.Id == 0xd7) {
					track.Number = ReadUnsigned(element);
				} else if (element.Id == 0x83) {
					track.Type = ReadUnsigned(element);
				} else if (element.Id == 0x86) {
					const auto bytes = ReadPayload(element, 64);
					track.Codec.assign(bytes.begin(), bytes.end());
				} else if (element.Id == 0x63a2) {
					track.CodecPrivate = ReadPayload(element, m_Limits.MaxMetadataBytes);
				} else if (element.Id == 0x23e383) {
					track.DefaultDurationNanoseconds = ReadUnsigned(element);
				} else if (element.Id == 0x56aa) {
					track.CodecDelayNanoseconds = ReadUnsigned(element);
				} else if (element.Id == 0x56bb) {
					track.SeekPreRollNanoseconds = ReadUnsigned(element);
				} else if (element.Id == 0xe0 || element.Id == 0xe1) {
					ReadTrackFields(element, track, depth + 1);
				} else if (element.Id == 0xb0) {
					track.Width = ReadDimension(element);
				} else if (element.Id == 0xba) {
					track.Height = ReadDimension(element);
				} else if (element.Id == 0x9f) {
					track.Channels = ReadDimension(element);
				} else if (element.Id == 0xb5) {
					track.SamplingFrequency = ReadFloat(ReadPayload(element, 8));
				} else if (element.Id == 0x53c0) {
					track.Alpha = ReadUnsigned(element) != 0;
				} else if (element.Id == 0x23314f && ReadFloat(ReadPayload(element, 8)) != 1) {
					throw std::runtime_error("WebM non-unit TrackTimestampScale is unsupported");
				} else if (element.Id == 0x6d80) {
					throw std::runtime_error("WebM track content encodings are unsupported");
				}

				offset = element.End;
			}
		}

		std::int64_t Timestamp(std::int32_t relative) const {
			const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
			if (m_ClusterTime > limit || (relative > 0 && m_ClusterTime > limit - static_cast<std::uint32_t>(relative))) {
				throw std::runtime_error("WebM timestamp overflows signed ticks");
			}

			const auto ticks = static_cast<std::int64_t>(m_ClusterTime) + relative;
			const auto scale = static_cast<std::int64_t>(TimestampScale);
			if (ticks > std::numeric_limits<std::int64_t>::max() / scale || ticks < std::numeric_limits<std::int64_t>::min() / scale) {
				throw std::runtime_error("WebM timestamp overflows nanoseconds");
			}

			return ticks * scale;
		}

		WebmPacket ReadBlock(const EbmlElement& element, bool simple) {
			if (!m_HasClusterTimestamp || element.End > m_ClusterEnd) {
				throw std::runtime_error("WebM block is outside a timestamped cluster");
			}

			const auto bytes = ReadPayload(element, m_Limits.MaxPacketBytes);
			const auto track = ReadVint(bytes, 0);
			if (track.Value == 0 || track.Unknown || bytes.size() - track.Length < 3 || std::none_of(Tracks.begin(), Tracks.end(), [&](const WebmTrack& entry) {
				    return entry.Number == track.Value;
			    })) {
				throw std::runtime_error("WebM block has an invalid track/header");
			}

			const auto timestamp_bits = (static_cast<std::uint32_t>(bytes[track.Length]) << 8) | bytes[track.Length + 1];
			const auto relative = static_cast<std::int32_t>(timestamp_bits) - ((timestamp_bits & 0x8000) != 0 ? 65536 : 0);
			const auto flags = bytes[track.Length + 2];
			WebmPacket packet;
			packet.TrackNumber = track.Value;
			packet.TimestampNanoseconds = Timestamp(relative);
			packet.KeyFrame = simple && (flags & 0x80) != 0;
			packet.Invisible = (flags & 8) != 0;
			packet.Discardable = simple && (flags & 1) != 0;
			ReadLacing(bytes, track.Length + 3, static_cast<std::uint8_t>((flags >> 1) & 3), packet);

			return packet;
		}

		void ReadLacing(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint8_t mode, WebmPacket& packet) {
			if (mode == 0) {
				packet.Frames.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());

				return;
			}

			if (offset >= bytes.size()) {
				throw std::runtime_error("WebM lacing count is missing");
			}

			const auto count = static_cast<std::size_t>(bytes[offset++]) + 1;
			std::vector<std::size_t> lengths(count, 0);
			if (mode == 2) {
				if ((bytes.size() - offset) % count != 0) {
					throw std::runtime_error("WebM fixed lace has unequal frame lengths");
				}

				std::fill(lengths.begin(), lengths.end(), (bytes.size() - offset) / count);
			} else {
				std::size_t total = 0;
				for (std::size_t index = 0; index + 1 < count; ++index) {
					if (mode == 1) {
						std::uint8_t byte = 255;
						while (byte == 255) {
							if (offset >= bytes.size() || bytes[offset] > bytes.size() - lengths[index]) {
								throw std::runtime_error("WebM Xiph lace length is invalid");
							}

							byte = bytes[offset++];
							lengths[index] += byte;
						}
					} else {
						const auto value = ReadVint(bytes, offset);
						offset += value.Length;
						std::int64_t length = static_cast<std::int64_t>(value.Value);
						if (index > 0) {
							const auto bias = (std::int64_t{1} << (7 * value.Length - 1)) - 1;
							length = static_cast<std::int64_t>(lengths[index - 1]) + length - bias;
						}

						if (length < 0 || static_cast<std::uint64_t>(length) > bytes.size()) {
							throw std::runtime_error("WebM EBML lace length is invalid");
						}

						lengths[index] = static_cast<std::size_t>(length);
					}

					if (lengths[index] > bytes.size() - total) {
						throw std::runtime_error("WebM lace lengths exceed block");
					}

					total += lengths[index];
				}

				if (total > bytes.size() - offset) {
					throw std::runtime_error("WebM lace payload is truncated");
				}

				lengths.back() = bytes.size() - offset - total;
			}

			for (const auto length : lengths) {
				packet.Frames.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
				offset += length;
			}
		}

		WebmPacket ReadBlockGroup(const EbmlElement& group) {
			std::optional<WebmPacket> packet;
			std::uint64_t duration = 0;
			std::int64_t padding = 0;
			bool key_frame = true;
			std::vector<WebmAddition> additions;
			for (auto offset = group.Data; offset < group.End;) {
				const auto element = ReadElement(offset, group.End);
				if (element.Id == 0xa1) {
					if (packet) {
						throw std::runtime_error("WebM BlockGroup contains duplicate blocks");
					}

					packet = ReadBlock(element, false);
				} else if (element.Id == 0x9b) {
					duration = ReadUnsigned(element);
					if (duration > std::numeric_limits<std::uint64_t>::max() / TimestampScale) {
						throw std::runtime_error("WebM block duration exceeds range");
					}

					duration *= TimestampScale;
				} else if (element.Id == 0xfb) {
					key_frame = false;
				} else if (element.Id == 0x75a2) {
					padding = ReadSigned(ReadPayload(element, 8));
				} else if (element.Id == 0x75a1) {
					ReadAdditions(element, additions);
				}

				offset = element.End;
			}

			if (!packet) {
				throw std::runtime_error("WebM BlockGroup is missing its Block");
			}

			packet->DurationNanoseconds = duration;
			packet->DiscardPaddingNanoseconds = padding;
			packet->KeyFrame = key_frame;
			packet->Additions = std::move(additions);

			return std::move(*packet);
		}

		void ReadAdditions(const EbmlElement& parent, std::vector<WebmAddition>& additions) {
			if (parent.End - parent.Data > m_Limits.MaxPacketBytes) {
				throw std::runtime_error("WebM block additions exceed packet limit");
			}

			for (auto offset = parent.Data; offset < parent.End;) {
				const auto more = ReadElement(offset, parent.End);
				if (more.Id == 0xa6) {
					WebmAddition addition;
					bool data_seen = false;
					for (auto child_offset = more.Data; child_offset < more.End;) {
						const auto child = ReadElement(child_offset, more.End);
						if (child.Id == 0xee) {
							addition.Id = ReadUnsigned(child);
						} else if (child.Id == 0xa5) {
							if (data_seen) {
								throw std::runtime_error("WebM block addition data is duplicated");
							}

							addition.Bytes = ReadPayload(child, m_Limits.MaxPacketBytes);
							data_seen = true;
						}

						child_offset = child.End;
					}

					if (!data_seen || addition.Id == 0) {
						throw std::runtime_error("WebM block addition is incomplete");
					}

					additions.push_back(std::move(addition));
				}

				offset = more.End;
			}
		}

		io::FileReader m_Input;
		std::optional<resource::ResourceCipher> m_Cipher;
		WebmLimits m_Limits;
		std::uint64_t m_SegmentEnd = 0;
		std::uint64_t m_FirstCluster = 0;
		std::uint64_t m_Position = 0;
		std::uint64_t m_ClusterEnd = 0;
		std::uint64_t m_ClusterTime = 0;
		std::size_t m_ElementCount = 0;
		bool m_UnknownCluster = false;
		bool m_HasClusterTimestamp = false;
	};

	WebmReader::WebmReader(const std::filesystem::path& path, WebmLimits limits) : m_Implementation(std::make_unique<Implementation>(path, std::nullopt, limits)) {
	}

	WebmReader::WebmReader(const std::filesystem::path& path, std::string_view seed, WebmLimits limits) : m_Implementation(std::make_unique<Implementation>(path, seed, limits)) {
	}

	WebmReader::~WebmReader() = default;
	WebmReader::WebmReader(WebmReader&&) noexcept = default;
	WebmReader& WebmReader::operator=(WebmReader&&) noexcept = default;

	const std::vector<WebmTrack>& WebmReader::Tracks() const {
		return m_Implementation->Tracks;
	}

	std::uint64_t WebmReader::TimestampScaleNanoseconds() const {
		return m_Implementation->TimestampScale;
	}

	double WebmReader::DurationNanoseconds() const {
		return m_Implementation->Duration;
	}

	std::optional<WebmPacket> WebmReader::NextPacket() {
		return m_Implementation->NextPacket();
	}

	void WebmReader::Rewind() {
		m_Implementation->Rewind();
	}

} // namespace sgre::media
