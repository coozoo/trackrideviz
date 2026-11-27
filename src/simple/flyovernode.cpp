#include "flyovernode.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QSGVertexColorMaterial>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
const double kPi = 3.14159265358979323846;
const int kCornerSteps = 8;     // points along each rounded corner of the clip

// The land's points as Flyover::Land has them: east, north, height; u, v; red, green, blue, light.
const QSGGeometry::AttributeSet &landAttributes()
{
    static const QSGGeometry::Attribute attributes[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 3, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 4, QSGGeometry::FloatType, QSGGeometry::ColorAttribute),
    };
    static const QSGGeometry::AttributeSet set = {3, int(Flyover::LandStride * sizeof(float)), attributes};
    return set;
}
}

// What the land's shaders (shaders/flyoverland.*) are given.
class FlyoverLandMaterial : public QSGMaterial
{
public:
    FlyoverLandMaterial()
    {
        // drawn among the see-through items, in the order of its triangles (no depth test of
        // its own); its own matrix, so never batched with others
        setFlag(Blending);
        setFlag(RequiresFullMatrix);
    }
    ~FlyoverLandMaterial() override { delete texture; }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial *other) const override
    {
        return this == other ? 0 : (this < other ? -1 : 1);
    }

    QSGTexture *texture{nullptr};   // the map, or a white pixel without one
    bool textured{false};
    QMatrix4x4 toClip;
    QRectF rect;
    float exaggeration{1.0f};
    float hazeFrom{0.0f};
    float hazeTo{1.0f};
};

class FlyoverLandShader : public QSGMaterialShader
{
public:
    FlyoverLandShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/flyoverland.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/flyoverland.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        const auto *material = static_cast<FlyoverLandMaterial *>(newMaterial);
        QByteArray *buffer = state.uniformData();
        Q_ASSERT(buffer->size() >= 176);
        char *data = buffer->data();
        // as the block of the shaders lays it out (std140)
        std::memcpy(data, state.combinedMatrix().constData(), 64);
        std::memcpy(data + 64, material->toClip.constData(), 64);
        const QColor haze = Flyover::hazeColor();
        const float values[] = {
            float(material->rect.x()), float(material->rect.y()), float(material->rect.width()), float(material->rect.height()),
            float(haze.redF()), float(haze.greenF()), float(haze.blueF()), material->textured ? 1.0f : 0.0f,
            material->exaggeration, material->hazeFrom, material->hazeTo, state.opacity(),
        };
        std::memcpy(data + 128, values, sizeof(values));
        return true;
    }

    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMaterial,
                            QSGMaterial *) override
    {
        auto *material = static_cast<FlyoverLandMaterial *>(newMaterial);
        if (binding == 1 && material->texture)
            {
                material->texture->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
                *texture = material->texture;
            }
    }
};

QSGMaterialShader *FlyoverLandMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new FlyoverLandShader;
}

FlyoverNode::FlyoverNode()
{
    m_clip = new QSGClipNode;
    m_clip->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0));
    m_clip->setFlag(QSGNode::OwnsGeometry);
    appendChildNode(m_clip);

    m_sky = new QSGGeometryNode;
    auto *skyGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 6);
    skyGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    m_sky->setGeometry(skyGeometry);
    m_sky->setMaterial(new QSGVertexColorMaterial);
    m_sky->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    m_clip->appendChildNode(m_sky);

    m_land = new QSGGeometryNode;
    m_land->setGeometry(new QSGGeometry(landAttributes(), 0, 0, QSGGeometry::UnsignedShortType));
    m_material = new FlyoverLandMaterial;
    m_land->setMaterial(m_material);
    m_land->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    m_clip->appendChildNode(m_land);
}

bool FlyoverNode::available(QQuickWindow *window)
{
    return window && window->rendererInterface()
           && window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
}

void FlyoverNode::update(QQuickWindow *window, const Flyover::Scene &scene, const QRectF &rect, double radius)
{
    // the rounded rectangle it is drawn inside, and the sky's colors down it
    if (rect != m_rect || radius != m_radius)
        {
            m_rect = rect;
            m_radius = radius;
            const double r = std::clamp(radius, 0.0, std::min(rect.width(), rect.height()) / 2.0);
            m_clip->setIsRectangular(r <= 0.0);
            m_clip->setClipRect(rect);
            QSGGeometry *clip = m_clip->geometry();
            if (r > 0.0)
                {
                    // the outline round the corners, then a triangle from the middle to each
                    // piece of it (fans are not drawn by every graphics API)
                    const QPointF corners[4] = {rect.topRight() + QPointF(-r, r), rect.bottomRight() + QPointF(-r, -r),
                                                rect.bottomLeft() + QPointF(r, -r), rect.topLeft() + QPointF(r, r)};
                    QVector<QPointF> outline;
                    for (int c = 0; c < 4; ++c)
                        {
                            for (int step = 0; step <= kCornerSteps; ++step)
                                {
                                    const double angle = (-90.0 + 90.0 * c + 90.0 * step / kCornerSteps) * kPi / 180.0;
                                    outline.append(corners[c] + QPointF(r * std::cos(angle), r * std::sin(angle)));
                                }
                        }
                    clip->allocate(int(outline.size()) * 3);
                    clip->setDrawingMode(QSGGeometry::DrawTriangles);
                    QSGGeometry::Point2D *point = clip->vertexDataAsPoint2D();
                    const QPointF middle = rect.center();
                    for (int k = 0; k < outline.size(); ++k)
                        {
                            const QPointF &a = outline.at(k);
                            const QPointF &b = outline.at((k + 1) % outline.size());
                            point++->set(float(middle.x()), float(middle.y()));
                            point++->set(float(a.x()), float(a.y()));
                            point++->set(float(b.x()), float(b.y()));
                        }
                }
            else
                {
                    clip->allocate(0);
                }
            m_clip->markDirty(QSGNode::DirtyGeometry);

            const QColor top = Flyover::skyColor();
            const QColor haze = Flyover::hazeColor();
            const float x0 = float(rect.left());
            const float x1 = float(rect.right());
            const float rows[3] = {float(rect.top()), float(rect.top() + rect.height() * Flyover::SkyHazeFrom), float(rect.bottom())};
            QSGGeometry::ColoredPoint2D *sky = m_sky->geometry()->vertexDataAsColoredPoint2D();
            for (int row = 0; row < 3; ++row)
                {
                    const QColor &color = row == 0 ? top : haze;
                    sky[row * 2].set(x0, rows[row], uchar(color.red()), uchar(color.green()), uchar(color.blue()), 255);
                    sky[row * 2 + 1].set(x1, rows[row], uchar(color.red()), uchar(color.green()), uchar(color.blue()), 255);
                }
            m_sky->markDirty(QSGNode::DirtyGeometry);
        }

    // the land: its points when it is another land, its triangles' order for every picture
    QSGGeometry *geometry = m_land->geometry();
    if (scene.land != m_shown)
        {
            m_shown = scene.land;
            const int n = m_shown ? m_shown->cells : 0;
            geometry->allocate(m_shown ? (n + 1) * (n + 1) : 0, n * n * 6);
            if (m_shown)
                {
                    std::memcpy(geometry->vertexData(), m_shown->vertices.constData(), m_shown->vertices.size() * sizeof(float));
                }
            delete m_material->texture;
            QImage map = m_shown && !m_shown->texture.isNull() ? m_shown->texture : QImage();
            m_material->textured = !map.isNull();
            if (map.isNull())
                {
                    map = QImage(1, 1, QImage::Format_RGB32);
                    map.fill(Qt::white);
                }
            m_material->texture = window->createTextureFromImage(map, QQuickWindow::TextureHasMipmaps);
            m_material->texture->setFiltering(QSGTexture::Linear);
            m_material->texture->setMipmapFiltering(QSGTexture::Linear);
            m_material->texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_material->texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            geometry->markVertexDataDirty();
        }
    // as many as the land has room for: the rest drawn as nothing
    quint16 *indices = geometry->indexDataAsUShort();
    const int used = std::min(int(scene.indices.size()), geometry->indexCount());
    std::memcpy(indices, scene.indices.constData(), used * sizeof(quint16));
    std::fill(indices + used, indices + geometry->indexCount(), quint16(0));
    geometry->markIndexDataDirty();
    m_material->toClip = scene.toClip;
    m_material->rect = rect;
    m_material->exaggeration = scene.exaggeration;
    m_material->hazeFrom = scene.hazeFrom;
    m_material->hazeTo = scene.hazeTo;
    m_land->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}
