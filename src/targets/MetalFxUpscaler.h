#pragma once
// Apple MetalFX spatial upscaling for the mpv picture (macOS only).
//
// mpv draws through OpenGL. To let MetalFX (which works on Metal textures)
// see that picture, mpv draws into a texture backed by an IOSurface, which
// OpenGL and Metal can both use. MetalFX scales it into a second IOSurface,
// and that is drawn into Qt Quick's framebuffer. Use on the render thread,
// with Qt Quick's OpenGL context current.

#include <QSize>
#include <memory>

namespace cj {

class MetalFxUpscaler
{
public:
    // True on macOS 13 or later with a GPU MetalFX supports.
    static bool available();

    MetalFxUpscaler();
    ~MetalFxUpscaler();
    MetalFxUpscaler(const MetalFxUpscaler&) = delete;
    MetalFxUpscaler& operator=(const MetalFxUpscaler&) = delete;

    // Sets the sizes: mpv draws 'input', MetalFX makes 'output'. Cheap when
    // they did not change. False if the textures or the scaler cannot be made.
    bool prepare(QSize input, QSize output);
    // The OpenGL framebuffer mpv draws into, input-sized.
    unsigned inputFramebuffer() const;
    // Runs MetalFX and draws the result into 'drawFramebuffer', stretched to
    // 'drawSize'. False on failure.
    bool upscaleInto(unsigned drawFramebuffer, QSize drawSize);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace cj
