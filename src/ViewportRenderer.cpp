#include "ViewportRenderer.h"
#include "Viewport.h"
#include "core/RenderSupport.h"
#include "core/SkyImage.h"
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <rhi/qrhi.h>

namespace {
constexpr int UniformFloats = 141, ShadowSize = 2048;
using Uniforms = std::array<float, UniformFloats>;

QShader shader(const char *name) {
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("Missing shader %s", name);
    return QShader::fromSerialized(f.readAll());
}
struct Target {
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    void create(QRhi *rhi, QSize size, QRhiTexture::Format format, bool mipmapped = false) {
        target.reset();
        pass.reset();
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size));
        auto flags = QRhiTexture::Flags(QRhiTexture::RenderTarget);
        if (mipmapped)
            flags |= QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
        color.reset(rhi->newTexture(format, size, 1, flags));
        if (!depth->create() || !color->create())
            qFatal("Unable to allocate preview effect target");
        target.reset(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get()), depth.get()}));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create())
            qFatal("Unable to create preview effect target");
    }
};
struct GpuChunk {
    std::unique_ptr<QRhiBuffer> opaque, transparent, water, indices;
    std::unique_ptr<QRhiTexture> colors;
    std::unique_ptr<QRhiShaderResourceBindings> mainBindings, reflectionBindings;
    size_t colorBytes = 0;
    int opaqueCount = 0, transparentCount = 0, waterCount = 0, indexCount = 0;
};
class Renderer final : public QQuickRhiItemRenderer {
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_uniform, m_reflectionUniform;
    std::array<std::unique_ptr<QRhiBuffer>, 2> m_shadowUniform;
    std::unique_ptr<QRhiTexture> m_atlas, m_white, m_skyTexture;
    std::unique_ptr<QRhiSampler> m_nearest, m_surfaceSampler, m_linear, m_skySampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_mainBindings, m_reflectionBindings, m_skyBindings,
        m_reflectionSkyBindings, m_waterBindings;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> m_shadowBindings, m_shadowSurfaceBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_opaque, m_alpha, m_water, m_sky, m_reflectOpaque, m_reflectSky,
        m_shadow;
    std::array<Target, 2> m_shadows;
    Target m_reflection;
    std::map<wb::MeshPtr, GpuChunk, std::owner_less<wb::MeshPtr>> m_chunks;
    std::shared_ptr<const wb::AssetLibrary> m_uploadedAssets;
    bool m_textureReady = false, m_whiteReady = false, m_skyTextureReady = false, m_skyTextureValid = false;
    QString m_skyTexturePath, m_uploadedSkyTexturePath;
    wb::Environment m_environment, m_reflectedEnvironment;
    std::shared_ptr<RenderMetrics> m_metrics;
    std::shared_ptr<const wb::MeshScene> m_scene, m_shadowScene, m_reflectedScene;
    std::shared_ptr<std::atomic_int> m_frames;
    QMatrix4x4 m_matrix, m_reflectedView, m_reflectionSample;
    std::array<QMatrix4x4, 2> m_shadowView, m_shadowSample;
    QVector3D m_camera, m_shadowSun;
    wb::Hit m_hit;
    QRect m_selection;
    int m_radius = 4, m_tool = 0, m_brushShape = 0;
    double m_decay = 0;
    QElapsedTimer m_clock;
    bool m_shadowReady = false, m_reflectionReady = false;
    size_t m_uploadBytes = 0;
    int m_draws = 0;

    std::unique_ptr<QRhiShaderResourceBindings> bindings(QRhiBuffer *uniform, QRhiTexture *atlas,
                                                         QRhiSampler *sampler, bool shadows = true) {
        auto b = std::unique_ptr<QRhiShaderResourceBindings>(rhi()->newShaderResourceBindings());
        std::vector<QRhiShaderResourceBinding> list{QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniform)};
        if (atlas)
            list.push_back(QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, atlas, sampler));
        if (shadows)
            for (int i = 0; i < 2; ++i)
                list.push_back(
                    QRhiShaderResourceBinding::sampledTexture(2 + i, QRhiShaderResourceBinding::FragmentStage,
                                                              m_shadows[i].color.get(), m_nearest.get()));
        b->setBindings(list.begin(), list.end());
        if (!b->create())
            qFatal("Unable to create preview shader bindings");
        return b;
    }
    std::unique_ptr<QRhiShaderResourceBindings> skyBindings(QRhiBuffer *uniform) {
        auto b = std::unique_ptr<QRhiShaderResourceBindings>(rhi()->newShaderResourceBindings());
        b->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,
                                                                 QRhiShaderResourceBinding::VertexStage |
                                                                     QRhiShaderResourceBinding::FragmentStage,
                                                                 uniform),
                        QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage,
                                                                  m_skyTexture.get(), m_skySampler.get())});
        if (!b->create())
            qFatal("Unable to create sky shader bindings");
        return b;
    }
    void atlasBindings() {
        m_mainBindings = bindings(m_uniform.get(), m_atlas.get(), m_nearest.get());
        m_reflectionBindings = bindings(m_reflectionUniform.get(), m_atlas.get(), m_nearest.get());
        for (int i = 0; i < 2; ++i)
            m_shadowBindings[i] = bindings(m_shadowUniform[i].get(), m_atlas.get(), m_nearest.get(), false);
    }
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(const char *vert, const char *frag,
                                                   QRhiShaderResourceBindings *srb, QRhiRenderTarget *rt,
                                                   bool alpha = false, bool sky = false) {
        auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi()->newGraphicsPipeline());
        p->setShaderStages(
            {{QRhiShaderStage::Vertex, shader(vert)}, {QRhiShaderStage::Fragment, shader(frag)}});
        if (!sky) {
            QRhiVertexInputLayout layout;
            layout.setBindings({QRhiVertexInputBinding(sizeof(wb::Vertex))});
            layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                                  {0, 1, QRhiVertexInputAttribute::Float3, 12},
                                  {0, 2, QRhiVertexInputAttribute::Float4, 24},
                                  {0, 3, QRhiVertexInputAttribute::Float2, 40}});
            p->setVertexInputLayout(layout);
        }
        p->setTopology(QRhiGraphicsPipeline::Triangles);
        p->setCullMode(QRhiGraphicsPipeline::None);
        p->setDepthTest(!sky);
        p->setDepthWrite(!alpha && !sky);
        p->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (alpha) {
            QRhiGraphicsPipeline::TargetBlend blend;
            blend.enable = true;
            blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
            blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            p->setTargetBlends({blend});
        }
        p->setSampleCount(rt->sampleCount());
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(rt->renderPassDescriptor());
        if (!p->create())
            qFatal("Unable to create terrain/effect graphics pipeline");
        return p;
    }
    void initialize(QRhiCommandBuffer *) override {
        // QQuickRhiItem keeps this renderer with its QRhi. A new scene graph gets
        // a new renderer; render-target resizing only rebuilds compatible pipes.
        if (!m_rhi) {
            m_rhi = rhi();
            m_clock.start();
            auto uniform = [&](int size) {
                auto b = std::unique_ptr<QRhiBuffer>(
                    rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, size));
                if (!b->create())
                    qFatal("Unable to allocate uniform buffer");
                return b;
            };
            m_uniform = uniform(UniformFloats * 4);
            m_reflectionUniform = uniform(UniformFloats * 4);
            for (int i = 0; i < 2; ++i) {
                m_shadowUniform[i] = uniform(64);
                m_shadows[i].create(rhi(), QSize(ShadowSize, ShadowSize), QRhiTexture::R32F);
            }
            m_atlas.reset(rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
            m_white.reset(rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
            m_skyTexture.reset(rhi()->newTexture(QRhiTexture::RGBA32F, QSize(2048, 1024)));
            if (!m_atlas->create() || !m_white->create() || !m_skyTexture->create())
                qFatal("Unable to create fallback texture");
            auto sampler = [&](QRhiSampler::Filter filter, QRhiSampler::Filter mip) {
                auto s = std::unique_ptr<QRhiSampler>(rhi()->newSampler(
                    filter, filter, mip, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
                if (!s->create())
                    qFatal("Unable to create sampler");
                return s;
            };
            m_nearest = sampler(QRhiSampler::Nearest, QRhiSampler::None);
            m_linear = sampler(QRhiSampler::Linear, QRhiSampler::None);
            m_surfaceSampler = sampler(QRhiSampler::Linear, QRhiSampler::Linear);
            m_skySampler.reset(rhi()->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                 QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
            if (!m_skySampler->create())
                qFatal("Unable to create sky sampler");
            atlasBindings();
            m_skyBindings = skyBindings(m_uniform.get());
            m_reflectionSkyBindings = skyBindings(m_reflectionUniform.get());
            for (int i = 0; i < 2; ++i)
                m_shadowSurfaceBindings[i] =
                    bindings(m_shadowUniform[i].get(), m_white.get(), m_nearest.get(), false);
            m_shadow = pipeline(":/shaders/shadow.vert.qsb", ":/shaders/shadow.frag.qsb",
                                m_shadowBindings[0].get(), m_shadows[0].target.get());
        }
        const QSize viewSize = renderTarget()->pixelSize();
        const float scale = std::min(.5f, 1024.f / std::max(viewSize.width(), viewSize.height()));
        const QSize reflectSize(std::max(64, int(viewSize.width() * scale)),
                                std::max(64, int(viewSize.height() * scale)));
        if (!m_reflection.target || m_reflection.color->pixelSize() != reflectSize) {
            m_reflectOpaque.reset();
            m_reflectSky.reset();
            m_waterBindings.reset();
            m_reflection.create(rhi(), reflectSize, QRhiTexture::RGBA8, true);
            m_reflectionReady = false;
            m_waterBindings.reset(rhi()->newShaderResourceBindings());
            m_waterBindings->setBindings(
                {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                     m_uniform.get()),
                 QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                           m_shadows[0].color.get(), m_nearest.get()),
                 QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                           m_shadows[1].color.get(), m_nearest.get()),
                 QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage,
                                                           m_reflection.color.get(),
                                                           m_surfaceSampler.get())});
            if (!m_waterBindings->create())
                qFatal("Unable to bind water effects");
            m_reflectOpaque = pipeline(":/shaders/voxel.vert.qsb", ":/shaders/voxel.frag.qsb",
                                       m_reflectionBindings.get(), m_reflection.target.get());
            m_reflectSky = pipeline(":/shaders/sky.vert.qsb", ":/shaders/sky.frag.qsb",
                                    m_reflectionSkyBindings.get(), m_reflection.target.get(), false, true);
        }
        m_opaque = pipeline(":/shaders/voxel.vert.qsb", ":/shaders/voxel.frag.qsb", m_mainBindings.get(),
                            renderTarget());
        m_alpha = pipeline(":/shaders/voxel.vert.qsb", ":/shaders/voxel.frag.qsb", m_mainBindings.get(),
                           renderTarget(), true);
        m_sky = pipeline(":/shaders/sky.vert.qsb", ":/shaders/sky.frag.qsb", m_skyBindings.get(),
                         renderTarget(), false, true);
        m_water = pipeline(":/shaders/voxel.vert.qsb", ":/shaders/water.frag.qsb", m_waterBindings.get(),
                           renderTarget(), true);
    }
    void synchronize(QQuickRhiItem *item) override {
        auto *v = static_cast<Viewport *>(item);
        m_matrix = v->viewProjection();
        m_camera = v->camera();
        m_hit = v->hit();
        m_frames = v->frameCounter();
        m_metrics = v->renderMetrics();
        if (v->editor()) {
            m_scene = v->editor()->scene();
            m_environment = v->editor()->world().environment;
            m_radius = v->editor()->radius();
            m_tool = v->editor()->tool();
            m_brushShape = v->editor()->brushShape();
            m_decay = v->editor()->brushDecay();
            m_selection = v->editor()->selectionRect();
            m_skyTexturePath = v->editor()->world().environment.skyTexturePath;
        }
    }
    QMatrix4x4 textureMatrix(const QMatrix4x4 &view) const {
        QMatrix4x4 bias;
        bias.translate(.5f, .5f, rhi()->isClipDepthZeroToOne() ? 0.f : .5f);
        bias.scale(.5f, rhi()->isYUpInFramebuffer() ? .5f : -.5f, rhi()->isClipDepthZeroToOne() ? 1.f : .5f);
        return bias * rhi()->clipSpaceCorrMatrix() * view;
    }
    struct ShadowView {
        QMatrix4x4 view;
        float bias;
    };
    ShadowView lightView(const QRectF &receiver) const {
        const auto sun = m_environment.sunDirection();
        QMatrix4x4 light;
        light.lookAt(sun * 1000.f, {}, std::abs(sun.y()) > .98 ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0));
        QVector3D lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        auto include = [&](QRectF box, bool xy) {
            for (float x : {float(box.left()), float(box.right())})
                for (float z : {float(box.top()), float(box.bottom())})
                    for (float y : {float(wb::World::MinY), float(wb::World::MaxY)}) {
                        const auto p = light.map(QVector3D(x, y, z));
                        for (int a = xy ? 0 : 2; a < 3; ++a) {
                            lo[a] = std::min(lo[a], p[a]);
                            hi[a] = std::max(hi[a], p[a]);
                        }
                    }
        };
        include(receiver, true);
        // Depth uses ALL casters, not just the camera frustum: a mountain behind
        // the viewer must still cast onto a visible lake or ground patch.
        include(QRectF(m_scene->worldArea), false);
        const float half = std::max(hi.x() - lo.x(), hi.y() - lo.y()) * .5f + 2.f;
        const float texel = 2.f * half / ShadowSize;
        float cx = std::floor((lo.x() + hi.x()) * .5f / texel) * texel;
        float cy = std::floor((lo.y() + hi.y()) * .5f / texel) * texel;
        QMatrix4x4 projection;
        projection.ortho(cx - half, cx + half, cy - half, cy + half, -hi.z() - 4.f, -lo.z() + 4.f);
        return {projection * light, std::max(.12f, texel * .65f) / (hi.z() - lo.z() + 8.f)};
    }
    Uniforms sceneUniform(const QMatrix4x4 &view, QVector3D camera, bool reflection, bool shadows,
                          const std::array<ShadowView, 2> &shadowViews) {
        Uniforms u{};
        const auto mvp = rhi()->clipSpaceCorrMatrix() * view;
        std::memcpy(u.data(), mvp.constData(), 64);
        u[16] = camera.x();
        u[17] = camera.y();
        u[18] = camera.z();
        u[20] = m_hit.x + .5f;
        u[21] = m_hit.y + .5f;
        u[22] = m_hit.z + .5f;
        u[23] = float(m_radius);
        u[24] = !reflection && m_hit.valid && m_tool >= 0 && m_tool <= 5;
        u[25] = m_tool == 5 ? 1.f : float(m_tool);
        u[26] = float(m_brushShape);
        u[27] = (m_tool == 0 || m_tool == 1 || m_tool == 5) ? float(1 - m_decay) : 1.f;
        u[28] = float(m_selection.left());
        u[29] = float(m_selection.top());
        u[30] = float(m_selection.right() + 1);
        u[31] = float(m_selection.bottom() + 1);
        u[32] = !reflection && m_selection.isValid();
        const auto sun = m_environment.sunDirection();
        u[36] = sun.x();
        u[37] = sun.y();
        u[38] = sun.z();
        u[39] = m_environment.sun ? m_environment.sunIntensity / 100.f : 0;
        auto color = [&](int offset, const QString &name, float w) {
            QColor c(name);
            u[offset] = c.redF();
            u[offset + 1] = c.greenF();
            u[offset + 2] = c.blueF();
            u[offset + 3] = w;
        };
        color(40, m_environment.skyColor, m_environment.sky ? m_environment.skyIntensity / 100.f : 0);
        color(44, m_environment.groundColor, std::exp2(m_environment.exposure / 10.f));
        color(48, m_environment.sunColor, m_environment.enabled ? 1.f : 0.f);
        color(52, m_environment.horizonColor, m_environment.atmosphere ? m_environment.haze / 100.f : 0);
        u[56] = m_environment.sky;
        u[57] = m_environment.sun;
        u[58] = m_environment.atmosphere;
        u[59] = rhi()->clipSpaceCorrMatrix()(1, 1);
        const auto inverse = view.inverted();
        std::memcpy(u.data() + 60, inverse.constData(), 64);
        for (int i = 0; i < 2; ++i)
            std::memcpy(u.data() + 76 + i * 16, m_shadowSample[i].constData(), 64);
        std::memcpy(u.data() + 108, m_reflectionSample.constData(), 64);
        u[124] = float(std::fmod(m_clock.elapsed() * .001 * m_environment.waveSpeed / 35.f, 3600.));
        u[125] = m_environment.waterWaves ? m_environment.waveStrength / 100.f : 0;
        u[126] = m_environment.waterReflections && m_reflectionReady;
        u[127] = m_scene ? m_scene->seaSurface : 0;
        u[128] = shadows;
        u[129] = 1.f / ShadowSize;
        u[130] = reflection;
        u[131] = m_environment.waveDensity / 100.f;
        u[132] = shadowViews[0].bias;
        u[133] = shadowViews[1].bias;
        u[134] = m_environment.cloudDensity / 100.f;
        u[135] = m_environment.cloudSpeed / 100.f;
        u[136] = m_environment.waterDepthFade / 100.f;
        u[137] = m_environment.waterFoamStrength / 100.f;
        // Water vertex alpha stores depth over a 64-block range. Keep the
        // foam control in the same unit so a UI value of N means roughly N
        // blocks of shoreline instead of turning the entire shallow shelf
        // into foam when N is above one.
        u[138] = m_environment.waterFoamWidth / 64.f;
        u[139] = m_environment.clouds && m_environment.sky;
        u[140] = m_skyTextureValid && !m_skyTexturePath.isEmpty() && m_environment.sky;
        return u;
    }
    void upload(const wb::MeshPtr &src, QRhiResourceUpdateBatch *updates) {
        if (m_chunks.contains(src))
            return;
        auto &dst = m_chunks[src];
        auto vertices = [&](const std::vector<wb::Vertex> &data, std::unique_ptr<QRhiBuffer> &buffer,
                            int &count) {
            count = int(data.size());
            if (!count)
                return;
            const auto bytes = quint32(data.size() * sizeof(wb::Vertex));
            buffer.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
            if (!buffer->create())
                qFatal("Unable to allocate terrain vertices");
            updates->uploadStaticBuffer(buffer.get(), 0, bytes, data.data());
            m_uploadBytes += bytes;
        };
        vertices(src->opaque, dst.opaque, dst.opaqueCount);
        vertices(src->transparent, dst.transparent, dst.transparentCount);
        vertices(src->water, dst.water, dst.waterCount);
        dst.indexCount = int(src->opaqueIndices.size());
        if (dst.indexCount) {
            const auto bytes = quint32(src->opaqueIndices.size() * sizeof(uint32_t));
            dst.indices.reset(rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, bytes));
            if (!dst.indices->create())
                qFatal("Unable to allocate terrain indices");
            updates->uploadStaticBuffer(dst.indices.get(), 0, bytes, src->opaqueIndices.data());
            m_uploadBytes += bytes;
        }
        if (!src->surfaceColors.isNull()) {
            dst.colors.reset(rhi()->newTexture(QRhiTexture::RGBA8, src->surfaceColors.size(), 1,
                                               QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
            if (!dst.colors->create())
                qFatal("Unable to allocate terrain colors");
            updates->uploadTexture(dst.colors.get(), src->surfaceColors);
            if (src->surfaceColors.width() > 1 || src->surfaceColors.height() > 1)
                updates->generateMips(dst.colors.get());
            dst.colorBytes = src->surfaceColorBytes();
            m_uploadBytes += src->surfaceColors.sizeInBytes();
            dst.mainBindings = bindings(m_uniform.get(), dst.colors.get(), m_surfaceSampler.get());
            dst.reflectionBindings =
                bindings(m_reflectionUniform.get(), dst.colors.get(), m_surfaceSampler.get());
        }
    }
    void draw(QRhiCommandBuffer *cb, QRhiBuffer *v, int count, QRhiBuffer *indices = nullptr,
              int indexCount = 0) {
        if (!v || !count)
            return;
        const QRhiCommandBuffer::VertexInput input(v, 0);
        if (indices && indexCount) {
            cb->setVertexInput(0, 1, &input, indices, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(indexCount);
        } else {
            cb->setVertexInput(0, 1, &input);
            cb->draw(count);
        }
        ++m_draws;
    }
    void viewport(QRhiCommandBuffer *cb, QRhiRenderTarget *rt) {
        const auto size = rt->pixelSize();
        cb->setViewport({0, 0, float(size.width()), float(size.height())});
    }
    void render(QRhiCommandBuffer *cb) override {
        QElapsedTimer timer;
        timer.start();
        m_uploadBytes = 0;
        m_draws = 0;
        auto *updates = rhi()->nextResourceUpdateBatch();
        if (!m_whiteReady) {
            updates->uploadTexture(m_white.get(), wb::whiteAtlas());
            m_whiteReady = true;
        }
        if (!m_skyTextureReady || m_uploadedSkyTexturePath != m_skyTexturePath) {
            constexpr QSize SkySize(2048, 1024);
            QImage image;
            QString error;
            image = wb::loadSkyImage(m_skyTexturePath, error);
            const bool valid = !image.isNull();
            if (valid)
                image = image.scaled(SkySize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            else {
                image = QImage(SkySize, QImage::Format_RGBA32FPx4);
                image.fill(QColor("#6d86a4"));
            }
            const QByteArray pixels(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
            updates->uploadTexture(m_skyTexture.get(),
                                   QRhiTextureUploadDescription({QRhiTextureUploadEntry(
                                       0, 0, QRhiTextureSubresourceUploadDescription(pixels))}));
            m_skyTextureReady = true;
            m_skyTextureValid = valid;
            m_uploadedSkyTexturePath = m_skyTexturePath;
        }
        const auto assets = m_scene ? m_scene->assets : std::shared_ptr<const wb::AssetLibrary>{};
        if (!m_textureReady || assets != m_uploadedAssets) {
            const QImage image = assets ? assets->atlas : wb::whiteAtlas();
            m_atlas.reset(rhi()->newTexture(QRhiTexture::RGBA8, image.size()));
            if (!m_atlas->create())
                qFatal("Unable to allocate atlas");
            atlasBindings();
            updates->uploadTexture(m_atlas.get(), image);
            m_uploadedAssets = assets;
            m_textureReady = true;
        }
        const bool shadows = m_scene && m_environment.enabled && m_environment.sun && m_environment.shadows &&
                             m_environment.sunIntensity > 0;
        bool visibleWater = false;
        if (m_scene && m_environment.water) {
            auto wet = [&](const wb::MeshPtr &mesh) {
                return mesh && !mesh->water.empty() && wb::visibleMesh(*mesh, m_matrix);
            };
            for (const auto &mesh : m_scene->chunks)
                visibleWater |= wet(mesh);
            for (const auto &tile : m_scene->farTiles)
                visibleWater |= wet(tile.mesh);
        }
        const bool reflection =
            visibleWater && m_environment.waterReflections && m_camera.y() > m_scene->seaSurface + .05f;
        std::array<ShadowView, 2> shadowViews{};
        bool updateShadow = false;
        if (shadows) {
            const auto inverse = m_matrix.inverted();
            const auto far = inverse.map(QVector3D(0, 0, 1));
            const auto forward = (far - m_camera).normalized();
            const float distance =
                std::clamp((m_camera.y() - m_scene->seaSurface) / std::max(.1f, -forward.y()), 64.f, 20000.f);
            const auto focus = m_camera + forward * distance;
            const float radius = std::clamp(distance * .65f, 192.f, 4096.f);
            shadowViews[0] =
                lightView(QRectF(focus.x() - radius, focus.z() - radius, 2 * radius, 2 * radius));
            shadowViews[1] = lightView(QRectF(m_scene->worldArea));
            updateShadow =
                !m_shadowReady || m_shadowScene != m_scene || m_shadowSun != m_environment.sunDirection();
            for (int i = 0; i < 2; ++i) {
                updateShadow |= m_shadowView[i] != shadowViews[i].view;
                m_shadowView[i] = shadowViews[i].view;
                m_shadowSample[i] = textureMatrix(m_shadowView[i]);
                const auto lightMvp = rhi()->clipSpaceCorrMatrix() * m_shadowView[i];
                updates->updateDynamicBuffer(m_shadowUniform[i].get(), 0, 64, lightMvp.constData());
            }
        }
        QMatrix4x4 reflectedView;
        QVector3D reflectedCamera = m_camera;
        if (m_scene) {
            QMatrix4x4 mirror;
            mirror.translate(0, 2 * m_scene->seaSurface, 0);
            mirror.scale(1, -1, 1);
            reflectedView = m_matrix * mirror;
            reflectedCamera.setY(2 * m_scene->seaSurface - m_camera.y());
        }
        // Waves perturb the sampling normal, not scene geometry. A static camera
        // and scene reuse their reflection/shadows indefinitely during animation.
        const bool updateReflection = reflection && (!m_reflectionReady || m_reflectedScene != m_scene ||
                                                     m_reflectedView != reflectedView ||
                                                     m_reflectedEnvironment != m_environment || updateShadow);
        if (updateReflection) {
            m_reflectedView = reflectedView;
            m_reflectionSample = textureMatrix(reflectedView);
            m_reflectionReady = true;
        }
        std::set<wb::MeshPtr, std::owner_less<wb::MeshPtr>> retained;
        std::vector<wb::MeshPtr> visible, reflected;
        std::array<std::vector<wb::MeshPtr>, 2> casters;
        auto collect = [&](const wb::MeshPtr &mesh) {
            if (!mesh)
                return;
            retained.insert(mesh);
            bool needed = false;
            if (wb::visibleMesh(*mesh, m_matrix)) {
                visible.push_back(mesh);
                needed = true;
            }
            if (updateReflection && wb::visibleMesh(*mesh, reflectedView)) {
                reflected.push_back(mesh);
                needed = true;
            }
            if (updateShadow)
                for (int i = 0; i < 2; ++i)
                    if (!mesh->opaque.empty() && wb::visibleMesh(*mesh, m_shadowView[i])) {
                        casters[i].push_back(mesh);
                        needed = true;
                    }
            if (needed)
                upload(mesh, updates);
        };
        if (m_scene) {
            for (auto mesh : m_scene->chunks)
                collect(mesh);
            for (const auto &t : m_scene->farTiles)
                collect(t.mesh);
        }
        std::erase_if(m_chunks, [&](const auto &pair) { return !retained.contains(pair.first); });
        auto main = sceneUniform(m_matrix, m_camera, false, shadows, shadowViews);
        if (!reflection)
            main[126] = 0;
        updates->updateDynamicBuffer(m_uniform.get(), 0, UniformFloats * 4, main.data());
        if (updateReflection) {
            const auto reflectedUniform =
                sceneUniform(reflectedView, reflectedCamera, true, shadows, shadowViews);
            updates->updateDynamicBuffer(m_reflectionUniform.get(), 0, UniformFloats * 4,
                                         reflectedUniform.data());
        }
        cb->resourceUpdate(updates);
        if (updateShadow) {
            for (int i = 0; i < 2; ++i) {
                cb->beginPass(m_shadows[i].target.get(), Qt::white, {1, 0});
                viewport(cb, m_shadows[i].target.get());
                cb->setGraphicsPipeline(m_shadow.get());
                for (const auto &src : casters[i]) {
                    auto &c = m_chunks.at(src);
                    cb->setShaderResources(c.colors ? m_shadowSurfaceBindings[i].get()
                                                    : m_shadowBindings[i].get());
                    draw(cb, c.opaque.get(), c.opaqueCount, c.indices.get(), c.indexCount);
                }
                cb->endPass();
            }
            m_shadowReady = true;
            m_shadowScene = m_scene;
            m_shadowSun = m_environment.sunDirection();
        }
        auto opaquePass = [&](const std::vector<wb::MeshPtr> &meshes, bool reflectedPass) {
            cb->setGraphicsPipeline(reflectedPass ? m_reflectOpaque.get() : m_opaque.get());
            for (const auto &src : meshes) {
                auto &c = m_chunks.at(src);
                cb->setShaderResources(
                    reflectedPass ? (c.colors ? c.reflectionBindings.get() : m_reflectionBindings.get())
                                  : (c.colors ? c.mainBindings.get() : m_mainBindings.get()));
                draw(cb, c.opaque.get(), c.opaqueCount, c.indices.get(), c.indexCount);
            }
        };
        if (updateReflection) {
            cb->beginPass(m_reflection.target.get(), QColor("#202c3b"), {1, 0});
            viewport(cb, m_reflection.target.get());
            if (m_environment.enabled) {
                cb->setGraphicsPipeline(m_reflectSky.get());
                cb->setShaderResources(m_reflectionSkyBindings.get());
                cb->draw(3);
                ++m_draws;
            }
            opaquePass(reflected, true);
            cb->endPass();
            auto *mips = rhi()->nextResourceUpdateBatch();
            mips->generateMips(m_reflection.color.get());
            cb->resourceUpdate(mips);
            m_reflectedScene = m_scene;
            m_reflectedEnvironment = m_environment;
        }
        cb->beginPass(renderTarget(), QColor("#202c3b"), {1, 0});
        viewport(cb, renderTarget());
        if (m_environment.enabled) {
            cb->setGraphicsPipeline(m_sky.get());
            cb->setShaderResources(m_skyBindings.get());
            cb->draw(3);
            ++m_draws;
        }
        opaquePass(visible, false);
        std::sort(visible.begin(), visible.end(), [&](const auto &a, const auto &b) {
            return (a->center - m_camera).lengthSquared() > (b->center - m_camera).lengthSquared();
        });
        if (m_environment.water) {
            cb->setGraphicsPipeline(m_water.get());
            cb->setShaderResources(m_waterBindings.get());
            for (const auto &src : visible) {
                auto &c = m_chunks.at(src);
                draw(cb, c.water.get(), c.waterCount);
            }
        }
        cb->setGraphicsPipeline(m_alpha.get());
        cb->setShaderResources(m_mainBindings.get());
        for (const auto &src : visible) {
            auto &c = m_chunks.at(src);
            draw(cb, c.transparent.get(), c.transparentCount);
        }
        cb->endPass();
        if (m_frames)
            ++(*m_frames);
        if (m_metrics) {
            m_metrics->renderedSourceRevision = m_scene ? m_scene->sourceRevision : 0;
            size_t bytes = 0;
            for (const auto &[src, c] : m_chunks)
                bytes += (c.opaque ? c.opaque->size() : 0) + (c.transparent ? c.transparent->size() : 0) +
                         (c.water ? c.water->size() : 0) + (c.indices ? c.indices->size() : 0) + c.colorBytes;
            const auto size = m_reflection.color->pixelSize();
            int mipPixels = 0;
            for (QSize mip = size;;
                 mip = QSize(std::max(1, mip.width() / 2), std::max(1, mip.height() / 2))) {
                mipPixels += mip.width() * mip.height();
                if (mip == QSize(1, 1))
                    break;
            }
            const int effectBytes =
                2 * ShadowSize * ShadowSize * 8 + size.width() * size.height() * 4 + mipPixels * 4;
            m_metrics->submitMicros = int(timer.nsecsElapsed() / 1000);
            m_metrics->uploadKiB = int(m_uploadBytes / 1024);
            m_metrics->residentKiB = int(bytes / 1024);
            m_metrics->draws = m_draws;
            m_metrics->effectKiB = effectBytes / 1024;
            m_metrics->visibleWater = visibleWater;
            if (updateShadow)
                ++m_metrics->shadowUpdates;
            if (updateReflection)
                ++m_metrics->reflectionUpdates;
        }
    }
};
} // namespace
QQuickRhiItemRenderer *createTerrainRenderer() {
    return new Renderer;
}
