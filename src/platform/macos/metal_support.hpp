#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace sgre::render {

	[[nodiscard]] std::runtime_error MetalOperationError(std::string_view operation, NSError* error);
	[[nodiscard]] id<MTLDevice> FindMetalDevice();
	[[nodiscard]] id<MTLLibrary> CompileMetalLibrary(id<MTLDevice> device, const std::filesystem::path& source_path);
	void WaitForMetalCommand(id<MTLCommandBuffer> command, std::string_view operation);

} // namespace sgre::render
