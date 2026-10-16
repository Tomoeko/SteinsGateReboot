#include "metal_support.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace sgre::render {

	std::runtime_error MetalOperationError(std::string_view operation, NSError* error) {
		const auto* reason = error == nil ? nullptr : error.localizedDescription.UTF8String;

		return std::runtime_error(std::string(operation) + (reason == nullptr ? " failed" : ": " + std::string(reason)));
	}

	id<MTLDevice> FindMetalDevice() {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) {
			NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
			if (devices.count == 1) {
				// A headless launch can enumerate a GPU without selecting a default display device.
				device = devices.firstObject;
			}
		}

		if (device == nil) {
			throw std::runtime_error("Metal default device is unavailable and no unique GPU can be selected");
		}

		return device;
	}

	static NSString* ReadMetalSource(const std::filesystem::path& path) {
		static constexpr std::size_t kMaximumShaderBytes = 1024 * 1024;
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		const auto size = input.tellg();
		if (!input || size <= 0 || static_cast<std::uint64_t>(size) > kMaximumShaderBytes) {
			throw std::runtime_error("Metal shader source is missing, empty, or exceeds its allocation limit");
		}

		std::string bytes(static_cast<std::size_t>(size), '\0');
		input.seekg(0);
		if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
			throw std::runtime_error("Metal shader source read failed");
		}

		NSString* source = [[NSString alloc] initWithBytes:bytes.data() length:bytes.size() encoding:NSUTF8StringEncoding];
		if (source == nil) {
			throw std::runtime_error("Metal shader source is not UTF-8");
		}

		return source;
	}

	id<MTLLibrary> CompileMetalLibrary(id<MTLDevice> device, const std::filesystem::path& source_path) {
		MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
		if (@available(macOS 15.0, *)) {
			options.mathMode = MTLMathModeSafe;
		} else {
			options.fastMathEnabled = NO;
		}

		NSError* error = nil;
		id<MTLLibrary> library = [device newLibraryWithSource:ReadMetalSource(source_path) options:options error:&error];
		if (library == nil) {
			throw MetalOperationError("compile Metal shader", error);
		}

		return library;
	}

	void WaitForMetalCommand(id<MTLCommandBuffer> command, std::string_view operation) {
		if (command == nil) {
			throw std::runtime_error(std::string(operation) + " has no command buffer");
		}

		[command waitUntilCompleted];
		if (command.status == MTLCommandBufferStatusError) {
			throw MetalOperationError(operation, command.error);
		}
	}

} // namespace sgre::render
