#include "MpvVideoItem.h"

#include "Logging.h"
#include "MpvTarget.h"
#include "Session.h"

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <mpv/render_gl.h>

#include <algorithm>
#include <memory>

#ifdef Q_OS_MACOS
#include "MetalFxUpscaler.h"
#endif

namespace cj {
namespace {

void* getProcAddress(void*, const char* name)
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void*>(ctx->getProcAddress(QByteArray(name))) : nullptr;
}

// Lives on Qt Quick's render thread, where the OpenGL context is current.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    ~MpvRenderer() override
    {
        if (m_ctx)
            mpv_render_context_free(m_ctx);
    }

    // Runs on the render thread while the GUI thread is blocked, so reading the
    // item is safe here. The render context is created once a player is set.
    void synchronize(QQuickFramebufferObject* item) override
    {
        auto* video = static_cast<MpvVideoItem*>(item);
        if (!m_ctx && video->target())
            createContext(video, video->target());
        MpvTarget* t = video->target();
        m_target = t;
        m_metalFx = t && t->upscaler() == Upscaler::MetalFx;
        m_upscaleHeight = t ? t->upscaleHeight() : 0;
        m_videoSize = t ? QSize(t->videoWidth(), t->videoHeight()) : QSize();
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        return new QOpenGLFramebufferObject(size, format);
    }

    void render() override
    {
        if (!m_ctx)
            return;
        QOpenGLFramebufferObject* target = framebufferObject();
#ifdef Q_OS_MACOS
        if (m_metalFx && renderWithMetalFx(target))
            return;
        if (!m_metalFx)
            m_upscaler.reset();  // off: give back its textures
#endif
        mpv_opengl_fbo fbo{static_cast<int>(target->handle()), target->width(), target->height(), 0};
        int flipY = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        // mpv expects default GL state, and Qt Quick expects its own back.
        QQuickOpenGLUtils::resetOpenGLState();
        mpv_render_context_render(m_ctx, params);
        QQuickOpenGLUtils::resetOpenGLState();
    }

private:
#ifdef Q_OS_MACOS
    // mpv draws the frame small, about at the video's own size, and MetalFX
    // scales it up to the surface (or to the chosen height, then stretched).
    // False when there is nothing to scale up: draw as usual.
    bool renderWithMetalFx(QOpenGLFramebufferObject* target)
    {
        const QSize out = target->size();
        if (m_videoSize.width() <= 0 || m_videoSize.height() <= 0 || out.isEmpty())
            return false;
        // Height the video is shown at inside the surface (letterboxed).
        const double shown = std::min<double>(out.height(),
                                              double(out.width()) * m_videoSize.height() / m_videoSize.width());
        const double goal = m_upscaleHeight > 0 ? std::min<double>(m_upscaleHeight, shown) : shown;
        const double factor = std::clamp(goal / m_videoSize.height(), 1.0, 3.0);
        if (factor < 1.05)
            return false;
        const QSize made = (QSizeF(out) * (goal / shown)).toSize();
        const QSize drawn = (QSizeF(made) / factor).toSize().expandedTo(QSize(1, 1));

        if (!m_upscaler)
            m_upscaler = std::make_unique<MetalFxUpscaler>();
        bool ok = m_upscaler->prepare(drawn, made);
        if (ok) {
            mpv_opengl_fbo fbo{static_cast<int>(m_upscaler->inputFramebuffer()), drawn.width(), drawn.height(), 0};
            int flipY = 0;
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            QQuickOpenGLUtils::resetOpenGLState();
            mpv_render_context_render(m_ctx, params);
            ok = m_upscaler->upscaleInto(target->handle(), out);
            QQuickOpenGLUtils::resetOpenGLState();
        }
        if (!ok) {
            m_upscaler.reset();
            m_metalFx = false;
            if (m_target)
                QMetaObject::invokeMethod(m_target.data(), "renderUpscalerFailed", Qt::QueuedConnection);
            return false;
        }
        return true;
    }
#endif

    void createContext(MpvVideoItem* item, MpvTarget* player)
    {
        mpv_opengl_init_params gl{getProcAddress, nullptr};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (const int err = mpv_render_context_create(&m_ctx, player->handle(), params); err < 0) {
            qCCritical(lcTargets) << "Could not create the mpv render context:" << mpv_error_string(err);
            m_ctx = nullptr;
            return;
        }
        // mpv calls this from its own thread when a new frame is ready.
        mpv_render_context_set_update_callback(m_ctx, [](void* ctx) {
            QMetaObject::invokeMethod(static_cast<MpvVideoItem*>(ctx), &QQuickItem::update,
                                      Qt::QueuedConnection);
        }, item);
        qCInfo(lcTargets) << "mpv render context ready";
        QMetaObject::invokeMethod(player, "markRenderReady", Qt::QueuedConnection);
    }

    mpv_render_context* m_ctx = nullptr;
    // Copied from the target in synchronize().
    QPointer<MpvTarget> m_target;
    bool m_metalFx = false;
    int m_upscaleHeight = 0;
    QSize m_videoSize;
#ifdef Q_OS_MACOS
    std::unique_ptr<MetalFxUpscaler> m_upscaler;  // made the first time it is used
#endif
};

} // namespace

MpvVideoItem::MpvVideoItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
}

void MpvVideoItem::setSession(Session* session)
{
    if (session == m_session)
        return;
    m_session = session;
    emit sessionChanged();
    update();
}

MpvTarget* MpvVideoItem::target() const
{
    return m_session ? qobject_cast<MpvTarget*>(m_session->target()) : nullptr;
}

QQuickFramebufferObject::Renderer* MpvVideoItem::createRenderer() const
{
    return new MpvRenderer;
}

void MpvVideoItem::useOpenGL()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#ifdef Q_OS_MACOS
    // macOS gives a legacy 2.1 context unless asked; mpv wants 3.2 core or newer.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
#endif
}

} // namespace cj
