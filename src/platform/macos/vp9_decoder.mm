#include "sgre/media/video.hpp"

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace sgre::media {

	static void CheckStatus(OSStatus status, const char* operation) {
		if (status != noErr) {
			throw std::runtime_error(std::string(operation) + " failed with OSStatus " + std::to_string(status));
		}
	}

	template <typename T>
	class CfOwner {
	public:
		explicit CfOwner(T value = nullptr) : m_Value(value) {
		}
		~CfOwner() {
			if (m_Value != nullptr) {
				CFRelease(m_Value);
			}
		}

		CfOwner(const CfOwner&) = delete;
		CfOwner& operator=(const CfOwner&) = delete;
		T* Receive() {
			if (m_Value != nullptr) {
				throw std::logic_error("Core Foundation owner already holds an object");
			}

			return &m_Value;
		}

		T Get() const noexcept {
			return m_Value;
		}

	private:
		T m_Value = nullptr;
	};

	class SessionOwner {
	public:
		~SessionOwner() {
			if (m_Value != nullptr) {
				VTDecompressionSessionInvalidate(m_Value);
				CFRelease(m_Value);
			}
		}

		SessionOwner() = default;
		SessionOwner(const SessionOwner&) = delete;
		SessionOwner& operator=(const SessionOwner&) = delete;
		VTDecompressionSessionRef Get() const noexcept {
			return m_Value;
		}

		VTDecompressionSessionRef* Receive() {
			return &m_Value;
		}

	private:
		VTDecompressionSessionRef m_Value = nullptr;
	};

	class LockedPixelBuffer {
	public:
		explicit LockedPixelBuffer(CVPixelBufferRef buffer) : m_Buffer(buffer) {
			CheckStatus(CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly), "lock VP9 pixels");
		}

		~LockedPixelBuffer() {
			CVPixelBufferUnlockBaseAddress(m_Buffer, kCVPixelBufferLock_ReadOnly);
		}

		LockedPixelBuffer(const LockedPixelBuffer&) = delete;
		LockedPixelBuffer& operator=(const LockedPixelBuffer&) = delete;

	private:
		CVPixelBufferRef m_Buffer = nullptr;
	};

	static VideoColorMatrix ReadColorMatrix(CVPixelBufferRef buffer) {
		const CfOwner<CFTypeRef> attachment(CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr));
		const auto value = attachment.Get();
		if (value != nullptr && CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) {
			return VideoColorMatrix::Bt601;
		}

		if (value != nullptr && CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) {
			return VideoColorMatrix::Bt709;
		}

		if (value != nullptr && CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_2020)) {
			return VideoColorMatrix::Bt2020;
		}

		return VideoColorMatrix::Unspecified;
	}

	static std::vector<std::uint8_t> CopyPlane(CVPixelBufferRef buffer, std::size_t plane, std::size_t width, std::size_t height) {
		const auto stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, plane);
		const auto* source = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, plane));
		if (source == nullptr || stride < width || CVPixelBufferGetHeightOfPlane(buffer, plane) < height) {
			throw std::runtime_error("VP9 decoder returned an invalid pixel plane");
		}

		std::vector<std::uint8_t> pixels(width * height);
		for (std::size_t row = 0; row < height; ++row) {
			std::memcpy(pixels.data() + row * width, source + row * stride, width);
		}

		return pixels;
	}

	class Vp9VideoDecoder::Implementation {
	public:
		Implementation(Vp9Description description, std::size_t max_frame_bytes) : m_MaxFrameBytes(max_frame_bytes) {
			if (description.Width == 0 || description.Height == 0 || description.Width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || description.Height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || description.Profile != 0 || description.BitDepth != 8 || description.ChromaSubsampling > 1) {
				throw std::runtime_error("macOS VP9 recovery currently supports 8-bit profile-zero 4:2:0 video");
			}

			ValidateFrameSize(description.Width, description.Height);
			VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_VP9);
			const std::array<std::uint8_t, 12> record{1, 0, 0, 0, description.Profile, description.Level, static_cast<std::uint8_t>((description.BitDepth << 4) | (description.ChromaSubsampling << 1) | (description.FullRange ? 1 : 0)), description.ColorPrimaries, description.TransferCharacteristics, description.MatrixCoefficients, 0, 0};
			const CfOwner<CFDataRef> codec_record(CFDataCreate(kCFAllocatorDefault, record.data(), static_cast<CFIndex>(record.size())));
			const void* atom_keys[] = {CFSTR("vpcC")};
			const void* atom_values[] = {codec_record.Get()};
			const CfOwner<CFDictionaryRef> atoms(CFDictionaryCreate(kCFAllocatorDefault, atom_keys, atom_values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
			const void* extension_keys[] = {kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms};
			const void* extension_values[] = {atoms.Get()};
			const CfOwner<CFDictionaryRef> extensions(CFDictionaryCreate(kCFAllocatorDefault, extension_keys, extension_values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
			CheckStatus(CMVideoFormatDescriptionCreate(kCFAllocatorDefault, kCMVideoCodecType_VP9, static_cast<std::int32_t>(description.Width), static_cast<std::int32_t>(description.Height), extensions.Get(), m_Format.Receive()), "create VP9 format");

			const std::int32_t pixel_format = static_cast<std::int32_t>(description.FullRange ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
			const CfOwner<CFNumberRef> format_number(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel_format));
			const void* attribute_keys[] = {kCVPixelBufferPixelFormatTypeKey};
			const void* attribute_values[] = {format_number.Get()};
			const CfOwner<CFDictionaryRef> attributes(CFDictionaryCreate(kCFAllocatorDefault, attribute_keys, attribute_values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
			VTDecompressionOutputCallbackRecord callback{};
			callback.decompressionOutputCallback = Output;
			callback.decompressionOutputRefCon = this;
			CheckStatus(VTDecompressionSessionCreate(kCFAllocatorDefault, m_Format.Get(), nullptr, attributes.Get(), &callback, m_Session.Receive()), "create VP9 decoding session");
		}

		std::vector<Nv12VideoFrame> Decode(std::span<const std::uint8_t> bytes, std::int64_t timestamp_nanoseconds) {
			if (bytes.empty() || bytes.size() > 16 * 1024 * 1024) {
				throw std::runtime_error("VP9 packet must contain between one byte and 16 MiB");
			}

			CMBlockBufferRef block = nullptr;
			// The synchronous call and explicit wait finish before this borrowed packet expires.
			CheckStatus(CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, const_cast<std::uint8_t*>(bytes.data()), bytes.size(), kCFAllocatorNull, nullptr, 0, bytes.size(), 0, &block), "create VP9 packet buffer");
			const CfOwner<CMBlockBufferRef> block_owner(block);
			CMSampleTimingInfo timing{};
			timing.duration = kCMTimeInvalid;
			timing.presentationTimeStamp = CMTimeMake(timestamp_nanoseconds, 1000000000);
			timing.decodeTimeStamp = kCMTimeInvalid;
			const auto size = bytes.size();
			CMSampleBufferRef sample = nullptr;
			CheckStatus(CMSampleBufferCreateReady(kCFAllocatorDefault, block, m_Format.Get(), 1, 1, &timing, 1, &size, &sample), "create VP9 sample");
			const CfOwner<CMSampleBufferRef> sample_owner(sample);
			const auto decode_status = VTDecompressionSessionDecodeFrame(m_Session.Get(), sample, 0, nullptr, nullptr);
			const auto wait_status = VTDecompressionSessionWaitForAsynchronousFrames(m_Session.Get());
			CheckStatus(decode_status, "decode VP9 packet");
			CheckStatus(wait_status, "wait for VP9 decoding");

			return TakeFrames();
		}

		std::vector<Nv12VideoFrame> Drain() {
			CheckStatus(VTDecompressionSessionFinishDelayedFrames(m_Session.Get()), "finish VP9 frames");
			CheckStatus(VTDecompressionSessionWaitForAsynchronousFrames(m_Session.Get()), "wait for final VP9 frames");

			return TakeFrames();
		}

	private:
		void ValidateFrameSize(std::size_t width, std::size_t height) const {
			if (width == 0 || height == 0 || width > m_MaxFrameBytes / height) {
				throw std::runtime_error("VP9 frame exceeds pixel allocation limit");
			}

			const auto luma_size = width * height;
			const auto chroma_width = ((width + 1) / 2) * 2;
			const auto chroma_height = (height + 1) / 2;
			if (chroma_width > (m_MaxFrameBytes - luma_size) / chroma_height) {
				throw std::runtime_error("VP9 chroma exceeds pixel allocation limit");
			}
		}

		void Receive(OSStatus status, CVImageBufferRef image, CMTime timestamp) {
			CheckStatus(status, "VP9 output callback");
			if (image == nullptr) {
				return;
			}

			const auto width = CVPixelBufferGetWidth(image);
			const auto height = CVPixelBufferGetHeight(image);
			ValidateFrameSize(width, height);
			const auto format = CVPixelBufferGetPixelFormatType(image);
			if ((format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange && format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) || CVPixelBufferGetPlaneCount(image) != 2 || width > std::numeric_limits<std::uint32_t>::max() || height > std::numeric_limits<std::uint32_t>::max() || !CMTIME_IS_NUMERIC(timestamp)) {
				throw std::runtime_error("VP9 output format/timestamp is unsupported");
			}

			const LockedPixelBuffer lock(image);
			Nv12VideoFrame frame;
			frame.Width = static_cast<std::uint32_t>(width);
			frame.Height = static_cast<std::uint32_t>(height);
			frame.TimestampNanoseconds = CMTimeConvertScale(timestamp, 1000000000, kCMTimeRoundingMethod_Default).value;
			frame.FullRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
			frame.Matrix = ReadColorMatrix(image);
			frame.Luma = CopyPlane(image, 0, width, height);
			frame.Chroma = CopyPlane(image, 1, ((width + 1) / 2) * 2, (height + 1) / 2);
			if (m_Frames.size() >= 8) {
				throw std::runtime_error("VP9 callback queue exceeds eight-frame limit");
			}

			m_Frames.push_back(std::move(frame));
		}

		static void Output(void* context, void*, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef image, CMTime timestamp, CMTime) noexcept {
			auto& decoder = *static_cast<Implementation*>(context);
			const std::lock_guard<std::mutex> lock(decoder.m_Mutex);
			try {
				decoder.Receive(status, image, timestamp);
			} catch (...) {
				decoder.m_Error = std::current_exception();
			}
		}

		std::vector<Nv12VideoFrame> TakeFrames() {
			const std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_Error) {
				const auto error = std::exchange(m_Error, nullptr);
				std::rethrow_exception(error);
			}

			return std::exchange(m_Frames, {});
		}

		std::mutex m_Mutex;
		std::size_t m_MaxFrameBytes = 0;
		std::vector<Nv12VideoFrame> m_Frames;
		std::exception_ptr m_Error;
		CfOwner<CMVideoFormatDescriptionRef> m_Format;
		SessionOwner m_Session;
	};

	Vp9VideoDecoder::Vp9VideoDecoder(Vp9Description description, std::size_t max_frame_bytes) : m_Implementation(std::make_unique<Implementation>(description, max_frame_bytes)) {
	}
	Vp9VideoDecoder::~Vp9VideoDecoder() = default;
	Vp9VideoDecoder::Vp9VideoDecoder(Vp9VideoDecoder&&) noexcept = default;
	Vp9VideoDecoder& Vp9VideoDecoder::operator=(Vp9VideoDecoder&&) noexcept = default;

	std::vector<Nv12VideoFrame> Vp9VideoDecoder::Decode(std::span<const std::uint8_t> packet, std::int64_t timestamp_nanoseconds) {
		if (!m_Implementation) {
			throw std::logic_error("VP9 decoder has been moved from");
		}

		return m_Implementation->Decode(packet, timestamp_nanoseconds);
	}

	std::vector<Nv12VideoFrame> Vp9VideoDecoder::Drain() {
		if (!m_Implementation) {
			throw std::logic_error("VP9 decoder has been moved from");
		}

		return m_Implementation->Drain();
	}

} // namespace sgre::media
