#include "MetalFxUpscaler.h"

#include "Logging.h"

#define GL_SILENCE_DEPRECATION 1
#include <IOSurface/IOSurface.h>
#include <Metal/Metal.h>
#include <MetalFX/MetalFX.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl3.h>

namespace cj {
namespace {

IOSurfaceRef makeSurface(QSize size)
{
    NSDictionary* props = @{
        (id)kIOSurfaceWidth: @(size.width()),
        (id)kIOSurfaceHeight: @(size.height()),
        (id)kIOSurfaceBytesPerElement: @4,
        (id)kIOSurfacePixelFormat: @(0x42475241u),  // 'BGRA'
    };
    return IOSurfaceCreate((__bridge CFDictionaryRef)props);
}

// A rectangle texture on the IOSurface, and a framebuffer with it attached.
bool makeGlTarget(IOSurfaceRef surface, QSize size, GLuint& tex, GLuint& fbo)
{
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_RECTANGLE, tex);
    const CGLError err = CGLTexImageIOSurface2D(CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE, GL_RGBA,
                                                size.width(), size.height(), GL_BGRA,
                                                GL_UNSIGNED_INT_8_8_8_8_REV, surface, 0);
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    if (err != kCGLNoError)
        return false;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, tex, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

} // namespace

struct MetalFxUpscaler::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id scaler = nil;  // id<MTLFXSpatialScaler>, macOS 13+
    QSize input, output;
    IOSurfaceRef inSurface = nullptr, outSurface = nullptr;
    id<MTLTexture> inTexture = nil, outShared = nil, outPrivate = nil;
    GLuint inTex = 0, inFbo = 0, outTex = 0, outFbo = 0;

    void release()
    {
        if (inFbo) glDeleteFramebuffers(1, &inFbo);
        if (outFbo) glDeleteFramebuffers(1, &outFbo);
        if (inTex) glDeleteTextures(1, &inTex);
        if (outTex) glDeleteTextures(1, &outTex);
        inFbo = outFbo = inTex = outTex = 0;
        inTexture = outShared = outPrivate = nil;
        scaler = nil;
        if (inSurface) CFRelease(inSurface);
        if (outSurface) CFRelease(outSurface);
        inSurface = outSurface = nullptr;
        input = output = QSize();
    }
};

bool MetalFxUpscaler::available()
{
    if (@available(macOS 13.0, *)) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        return device && [MTLFXSpatialScalerDescriptor supportsDevice:device];
    }
    return false;
}

MetalFxUpscaler::MetalFxUpscaler()
    : d(std::make_unique<Impl>())
{
    d->device = MTLCreateSystemDefaultDevice();
    d->queue = [d->device newCommandQueue];
}

MetalFxUpscaler::~MetalFxUpscaler()
{
    d->release();
}

unsigned MetalFxUpscaler::inputFramebuffer() const
{
    return d->inFbo;
}

bool MetalFxUpscaler::prepare(QSize input, QSize output)
{
    if (input == d->input && output == d->output && d->scaler)
        return true;
    d->release();
    if (!d->device || !d->queue || input.isEmpty() || output.isEmpty())
        return false;
    if (@available(macOS 13.0, *)) {
        MTLFXSpatialScalerDescriptor* desc = [MTLFXSpatialScalerDescriptor new];
        desc.inputWidth = NSUInteger(input.width());
        desc.inputHeight = NSUInteger(input.height());
        desc.outputWidth = NSUInteger(output.width());
        desc.outputHeight = NSUInteger(output.height());
        desc.colorTextureFormat = MTLPixelFormatBGRA8Unorm;
        desc.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
        id<MTLFXSpatialScaler> scaler = [desc newSpatialScalerWithDevice:d->device];
        if (!scaler) {
            qCWarning(lcTargets) << "MetalFX could not make a scaler for" << input << "to" << output;
            return false;
        }

        d->inSurface = makeSurface(input);
        d->outSurface = makeSurface(output);
        if (!d->inSurface || !d->outSurface)
            return false;

        MTLTextureDescriptor* in = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                       width:NSUInteger(input.width())
                                                                                      height:NSUInteger(input.height())
                                                                                   mipmapped:NO];
        in.usage = scaler.colorTextureUsage;
        d->inTexture = [d->device newTextureWithDescriptor:in iosurface:d->inSurface plane:0];

        MTLTextureDescriptor* out = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                        width:NSUInteger(output.width())
                                                                                       height:NSUInteger(output.height())
                                                                                    mipmapped:NO];
        out.usage = scaler.outputTextureUsage;
        out.storageMode = MTLStorageModePrivate;
        d->outPrivate = [d->device newTextureWithDescriptor:out];
        out.usage = MTLTextureUsageShaderRead;
        out.storageMode = MTLStorageModeShared;
        d->outShared = [d->device newTextureWithDescriptor:out iosurface:d->outSurface plane:0];
        if (!d->inTexture || !d->outPrivate || !d->outShared)
            return false;

        if (!makeGlTarget(d->inSurface, input, d->inTex, d->inFbo)
            || !makeGlTarget(d->outSurface, output, d->outTex, d->outFbo)) {
            qCWarning(lcTargets) << "MetalFX: OpenGL could not use the shared surfaces";
            return false;
        }
        d->scaler = scaler;
        d->input = input;
        d->output = output;
        return true;
    }
    return false;
}

bool MetalFxUpscaler::upscaleInto(unsigned drawFramebuffer, QSize drawSize)
{
    if (!d->scaler)
        return false;
    if (@available(macOS 13.0, *)) {
        // mpv's OpenGL drawing must reach the surface before Metal reads it.
        glFlush();
        id<MTLFXSpatialScaler> scaler = d->scaler;
        scaler.colorTexture = d->inTexture;
        scaler.outputTexture = d->outPrivate;
        scaler.inputContentWidth = NSUInteger(d->input.width());
        scaler.inputContentHeight = NSUInteger(d->input.height());
        id<MTLCommandBuffer> commands = [d->queue commandBuffer];
        [scaler encodeToCommandBuffer:commands];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:d->outPrivate toTexture:d->outShared];
        [blit endEncoding];
        [commands commit];
        // One frame of work; OpenGL reads the result right after.
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted)
            return false;

        while (glGetError() != GL_NO_ERROR) {}  // not ours
        glBindFramebuffer(GL_READ_FRAMEBUFFER, d->outFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        glBlitFramebuffer(0, 0, d->output.width(), d->output.height(),
                          0, 0, drawSize.width(), drawSize.height(), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return glGetError() == GL_NO_ERROR;
    }
    return false;
}

} // namespace cj
