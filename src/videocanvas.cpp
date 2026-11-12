#include "videocanvas.h"


#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVideoFrameFormat>

namespace
{
const double kHandleSize = 12.0;      // resize handle, widget pixels
const double kMinVisible = 24.0;      // of a gauge that is dragged off the picture
const double kMinItemHeight = 0.03;   // fraction of the frame height

// GLSL 1.10 / GLSL ES 1.00: runs on anything that has shaders at all,
// including the 2.1 compatibility context macOS gives by default.
const char kVertexShader[] = R"(
attribute vec2 position;
attribute vec2 texCoord;
varying vec2 uv;
void main()
{
    uv = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

const char kFragmentShader[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 uv;
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;
uniform int mode;          // 0 RGBA, 1 planar YUV, 2 Y + interleaved chroma
uniform bool swapChroma;
uniform float lumaWidthUsed;
uniform float chromaWidthUsed;
uniform mat4 colorMatrix;
void main()
{
    vec2 lumaUv = vec2(uv.x * lumaWidthUsed, uv.y);
    if (mode == 0) {
        gl_FragColor = vec4(texture2D(tex0, lumaUv).rgb, 1.0);
        return;
    }
    vec2 chromaUv = vec2(uv.x * chromaWidthUsed, uv.y);
    float y = texture2D(tex0, lumaUv).r;
    vec2 chroma;
    if (mode == 1)
        chroma = vec2(texture2D(tex1, chromaUv).r, texture2D(tex2, chromaUv).r);
    else
        chroma = texture2D(tex1, chromaUv).ra;
    if (swapChroma)
        chroma = chroma.yx;
    gl_FragColor = vec4((colorMatrix * vec4(y, chroma, 1.0)).rgb, 1.0);
}
)";

// Standard YCbCr -> RGB for normalized texture values.
QMatrix4x4 yuvToRgb(double kr, double kb, bool fullRange)
{
    const double kg = 1.0 - kr - kb;
    const QMatrix4x4 decode(1.0f, 0.0f, float(2.0 * (1.0 - kr)), 0.0f,
                            1.0f, float(-2.0 * kb * (1.0 - kb) / kg), float(-2.0 * kr * (1.0 - kr) / kg), 0.0f,
                            1.0f, float(2.0 * (1.0 - kb)), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 1.0f);
    // bring Y to 0..1 and chroma to -0.5..0.5 first
    QMatrix4x4 range;
    if (fullRange)
        {
            range.translate(0.0f, -128.0f / 255.0f, -128.0f / 255.0f);
        }
    else
        {
            range.scale(255.0f / 219.0f, 255.0f / 224.0f, 255.0f / 224.0f);
            range.translate(-16.0f / 255.0f, -128.0f / 255.0f, -128.0f / 255.0f);
        }
    return decode * range;
}

QMatrix4x4 colorMatrixFor(const QVideoFrameFormat &format)
{
    enum Standard { Unknown, Bt601, Bt709, Bt2020 };
    Standard standard = Unknown;
    bool fullRange = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    switch (format.colorSpace())
        {
        case QVideoFrameFormat::ColorSpace_BT601:
            standard = Bt601;
            break;
        case QVideoFrameFormat::ColorSpace_BT709:
            standard = Bt709;
            break;
        case QVideoFrameFormat::ColorSpace_BT2020:
            standard = Bt2020;
            break;
        case QVideoFrameFormat::ColorSpace_AdobeRgb:
            standard = Bt601;
            fullRange = true;
            break;
        default:
            break;
        }
    if (format.colorRange() == QVideoFrameFormat::ColorRange_Full)
        {
            fullRange = true;
        }
#else
    switch (format.yCbCrColorSpace())
        {
        case QVideoFrameFormat::YCbCr_BT601:
        case QVideoFrameFormat::YCbCr_xvYCC601:
            standard = Bt601;
            break;
        case QVideoFrameFormat::YCbCr_BT709:
        case QVideoFrameFormat::YCbCr_xvYCC709:
            standard = Bt709;
            break;
        case QVideoFrameFormat::YCbCr_BT2020:
            standard = Bt2020;
            break;
        case QVideoFrameFormat::YCbCr_JPEG:
            standard = Bt601;
            fullRange = true;
            break;
        default:
            break;
        }
#endif
    if (standard == Unknown)
        {
            // untagged files: the usual convention is SD = 601, HD = 709
            standard = format.frameHeight() > 576 ? Bt709 : Bt601;
        }
    switch (standard)
        {
        case Bt601:
            return yuvToRgb(0.299, 0.114, fullRange);
        case Bt2020:
            return yuvToRgb(0.2627, 0.0593, fullRange);
        default:
            return yuvToRgb(0.2126, 0.0722, fullRange);
        }
}

int rotationOf(const QVideoFrame &frame)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return int(frame.rotation());
#else
    return int(frame.rotationAngle());
#endif
}
}

VideoCanvas::VideoCanvas(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 180);
    setMouseTracking(true);          // for the cursor that says what a drag would do
    setFocusPolicy(Qt::ClickFocus);  // Delete acts on the selected gauge
}

VideoCanvas::~VideoCanvas()
{
    releaseGl();
}

void VideoCanvas::setOverlay(const Overlay &overlay)
{
    m_overlay = overlay;
    m_drag = NoDrag;
    if (m_selected >= m_overlay.count())
        {
            setSelectedItem(-1);
        }
    update();
}

void VideoCanvas::setSelectedItem(int index)
{
    if (index < -1 || index >= m_overlay.count())
        {
            index = -1;
        }
    if (index != m_selected)
        {
            m_selected = index;
            emit selectedItemChanged(index);
            update();
        }
}

QRectF VideoCanvas::resizeHandle(const QRectF &itemRect) const
{
    return QRectF(itemRect.right() - kHandleSize, itemRect.bottom() - kHandleSize, kHandleSize, kHandleSize);
}

void VideoCanvas::updateCursor(const QPointF &position)
{
    if (!m_hasPicture)
        {
            unsetCursor();
            return;
        }
    const QRectF frame = videoRect();
    if (m_selected >= 0 && resizeHandle(m_overlay.itemRect(m_selected, frame)).contains(position))
        {
            setCursor(Qt::SizeFDiagCursor);
        }
    else if (m_overlay.itemAt(position, frame) >= 0)
        {
            setCursor(Qt::SizeAllCursor);
        }
    else
        {
            unsetCursor();
        }
}

void VideoCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_hasPicture)
        {
            QOpenGLWidget::mousePressEvent(event);
            return;
        }
    const QPointF position = event->position();
    const QRectF frame = videoRect();
    // the handle belongs to the selected gauge even where another gauge lies under it
    if (m_selected >= 0 && resizeHandle(m_overlay.itemRect(m_selected, frame)).contains(position))
        {
            m_drag = Resizing;
        }
    else
        {
            setSelectedItem(m_overlay.itemAt(position, frame));
            m_drag = m_selected >= 0 ? Moving : NoDrag;
        }
    if (m_drag != NoDrag)
        {
            m_dragStart = position;
            m_dragStartRect = m_overlay.itemRect(m_selected, frame);
            m_dragMoved = false;
        }
}

void VideoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF position = event->position();
    if (m_drag == NoDrag || m_selected < 0)
        {
            updateCursor(position);
            return;
        }
    const QRectF frame = videoRect();
    const QPointF delta = position - m_dragStart;
    QRectF rect = m_dragStartRect;
    if (m_drag == Moving)
        {
            rect.translate(delta);
            // at least a corner stays on the picture, so the gauge cannot be lost
            const double keep = qMin(kMinVisible, qMin(rect.width(), rect.height()));
            rect.moveLeft(qBound(frame.left() - rect.width() + keep, rect.left(), frame.right() - keep));
            rect.moveTop(qBound(frame.top() - rect.height() + keep, rect.top(), frame.bottom() - keep));
        }
    else
        {
            // the shape is fixed: the height follows whichever direction the mouse went further
            const double aspect = m_dragStartRect.width() / m_dragStartRect.height();
            const double height = qMax(m_dragStartRect.height() + delta.y(), (m_dragStartRect.width() + delta.x()) / aspect);
            rect.setHeight(qBound(kMinItemHeight * frame.height(), height, 2.0 * frame.height()));
            rect.setWidth(rect.height() * aspect);
        }
    m_dragMoved = true;
    m_overlay = m_overlay.withGeometry(m_selected, (rect.left() - frame.left()) / frame.width(),
                                       (rect.top() - frame.top()) / frame.height(), rect.height() / frame.height());
    update();
}

void VideoCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_drag != NoDrag)
        {
            const bool edited = m_dragMoved && m_selected >= 0;
            m_drag = NoDrag;
            if (edited)
                {
                    const OverlayItem &item = m_overlay.item(m_selected);
                    emit itemGeometryEdited(m_selected, item.x, item.y, item.height);
                }
        }
    updateCursor(event->position());
}

void VideoCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_selected >= 0 && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace))
        {
            emit itemDeleteRequested(m_selected);
            return;
        }
    if (event->key() == Qt::Key_Escape && m_selected >= 0)
        {
            setSelectedItem(-1);
            return;
        }
    QOpenGLWidget::keyPressEvent(event);
}

void VideoCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_hasPicture)
        {
            return;
        }
    const int index = m_overlay.itemAt(event->pos(), videoRect());
    if (index >= 0)
        {
            setSelectedItem(index);
            emit itemMenuRequested(index, event->globalPos());
        }
}

void VideoCanvas::setFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        {
            return;
        }
    // Only a reference is kept here; the pixels are touched in paintGL(). If
    // frames arrive faster than the screen is painted, the ones in between
    // cost nothing.
    m_pendingFrame = frame;
    // microseconds; -1 when the backend does not know
    m_videoTime = frame.startTime() >= 0 ? frame.startTime() / 1e6 : m_positionTime;
    update();
}

void VideoCanvas::setPosition(qint64 positionMs)
{
    m_positionTime = positionMs / 1000.0;
}

void VideoCanvas::setSyncOffset(double seconds)
{
    m_syncOffset = seconds;
    update();
}

void VideoCanvas::clear()
{
    m_pendingFrame = QVideoFrame();
    m_hasPicture = false;
    m_videoTime = 0.0;
    m_positionTime = 0.0;
    update();
}

void VideoCanvas::initializeGL()
{
    initializeOpenGLFunctions();
    // the context goes away when the widget is moved to another window
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &VideoCanvas::releaseGl);

    m_program = new QOpenGLShaderProgram;
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("position", 0);
    m_program->bindAttributeLocation("texCoord", 1);
    if (!m_program->link())
        {
            qWarning() << "Video shader failed:" << m_program->log();
        }

    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    glGenTextures(3, m_textures);
    for (int i = 0; i < 3; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, m_textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_textureSizes[i] = QSize();
            m_textureFormats[i] = 0;
        }
    glBindTexture(GL_TEXTURE_2D, 0);
    // a new context has none of the old picture
    m_hasPicture = false;
}

void VideoCanvas::releaseGl()
{
    if (!m_program)
        {
            return;
        }
    // Without this the base class destructor, which destroys the context,
    // would call back into an object that is already half destroyed.
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &VideoCanvas::releaseGl);
    makeCurrent();
    glDeleteTextures(3, m_textures);
    m_vertexBuffer.destroy();
    delete m_program;
    m_program = nullptr;
    m_hasPicture = false;
    doneCurrent();
}

float VideoCanvas::uploadPlane(int index, const uchar *data, int bytesPerLine, int bytesPerPixel, int width, int height)
{
    // Rows may be padded. Instead of GL_UNPACK_ROW_LENGTH (missing in GLES 2)
    // the padding is uploaded too and the shader samples only the used part.
    const int textureWidth = bytesPerLine / bytesPerPixel;
    const GLenum format = bytesPerPixel == 4 ? GL_RGBA : bytesPerPixel == 2 ? GL_LUMINANCE_ALPHA : GL_LUMINANCE;
    const QSize size(textureWidth, height);
    glActiveTexture(GL_TEXTURE0 + index);
    glBindTexture(GL_TEXTURE_2D, m_textures[index]);
    if (m_textureSizes[index] != size || m_textureFormats[index] != int(format))
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GLint(format), textureWidth, height, 0, format, GL_UNSIGNED_BYTE, data);
            m_textureSizes[index] = size;
            m_textureFormats[index] = int(format);
        }
    else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, height, format, GL_UNSIGNED_BYTE, data);
        }
    // stop half a texel short of the padding so filtering does not pull it in
    return textureWidth > width ? (width - 0.5f) / textureWidth : 1.0f;
}

bool VideoCanvas::uploadYuv(QVideoFrame &frame)
{
    int chromaPlanes = 0;
    bool halfHeight = true;
    bool swap = false;
    switch (frame.pixelFormat())
        {
        case QVideoFrameFormat::Format_YUV420P:
            chromaPlanes = 2;
            break;
        case QVideoFrameFormat::Format_YV12:
            chromaPlanes = 2;
            swap = true;
            break;
        case QVideoFrameFormat::Format_YUV422P:
            chromaPlanes = 2;
            halfHeight = false;
            break;
        case QVideoFrameFormat::Format_NV12:
            chromaPlanes = 1;
            break;
        case QVideoFrameFormat::Format_NV21:
            chromaPlanes = 1;
            swap = true;
            break;
        default:
            return false;
        }
    if (!frame.map(QVideoFrame::ReadOnly))
        {
            return false;
        }
    const int width = frame.width();
    const int height = frame.height();
    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = halfHeight ? (height + 1) / 2 : height;
    const int chromaBytes = chromaPlanes == 1 ? 2 : 1;
    bool ok = frame.planeCount() == 1 + chromaPlanes
              && frame.bytesPerLine(0) >= width
              && frame.mappedBytes(0) >= frame.bytesPerLine(0) * height;
    for (int plane = 1; ok && plane <= chromaPlanes; ++plane)
        {
            ok = frame.bytesPerLine(plane) >= chromaWidth * chromaBytes
                 && frame.mappedBytes(plane) >= frame.bytesPerLine(plane) * chromaHeight;
        }
    if (ok)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            m_lumaWidthUsed = uploadPlane(0, frame.bits(0), frame.bytesPerLine(0), 1, width, height);
            for (int plane = 1; plane <= chromaPlanes; ++plane)
                {
                    m_chromaWidthUsed = uploadPlane(plane, frame.bits(plane), frame.bytesPerLine(plane),
                                                    chromaBytes, chromaWidth, chromaHeight);
                }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            m_layout = chromaPlanes == 2 ? Planar : SemiPlanar;
            m_swapChroma = swap;
            m_colorMatrix = colorMatrixFor(frame.surfaceFormat());
            m_pictureSize = QSize(width, height);
            m_rotation = rotationOf(frame);
        }
    frame.unmap();
    return ok;
}

void VideoCanvas::uploadImage(const QImage &source)
{
    if (source.isNull())
        {
            return;
        }
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    m_lumaWidthUsed = uploadPlane(0, image.constBits(), int(image.bytesPerLine()), 4, image.width(), image.height());
    m_layout = Rgba;
    m_pictureSize = image.size();
    // toImage() has turned the picture upright already
    m_rotation = 0;
}

void VideoCanvas::uploadPendingFrame()
{
    if (!m_pendingFrame.isValid())
        {
            return;
        }
    QVideoFrame frame = m_pendingFrame;
    m_pendingFrame = QVideoFrame();
    if (!uploadYuv(frame))
        {
            uploadImage(frame.toImage());
        }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hasPicture = !m_pictureSize.isEmpty();
}

double VideoCanvas::pictureAspect() const
{
    const QSizeF size = displaySize();
    return m_hasPicture && size.height() > 0 ? size.width() / size.height() : 0.0;
}

QSizeF VideoCanvas::displaySize() const
{
    return m_rotation % 180 == 0 ? QSizeF(m_pictureSize) : QSizeF(m_pictureSize.height(), m_pictureSize.width());
}

QRectF VideoCanvas::videoRect() const
{
    const QSizeF scaled = displaySize().scaled(size(), Qt::KeepAspectRatio);
    return QRectF((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled.width(), scaled.height());
}

void VideoCanvas::drawVideo(const QRectF &target)
{
    // widget coordinates -> clip space
    const float left = float(2.0 * target.left() / width() - 1.0);
    const float right = float(2.0 * target.right() / width() - 1.0);
    const float top = float(1.0 - 2.0 * target.top() / height());
    const float bottom = float(1.0 - 2.0 * target.bottom() / height());
    // screen corners clockwise from top left, and the picture corners in the same order
    const float corners[4][2] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
    const float picture[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const int turns = (m_rotation / 90) % 4;
    // x, y, u, v per corner, as a triangle strip
    float vertices[16];
    const int stripOrder[4] = {0, 1, 3, 2};
    for (int i = 0; i < 4; ++i)
        {
            const int corner = stripOrder[i];
            // turned clockwise, the picture's top left ends up at the screen's top right
            const int source = (corner - turns + 4) % 4;
            vertices[i * 4] = corners[corner][0];
            vertices[i * 4 + 1] = corners[corner][1];
            vertices[i * 4 + 2] = picture[source][0];
            vertices[i * 4 + 3] = picture[source][1];
        }

    m_program->bind();
    for (int i = 0; i < 3; ++i)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        }
    m_program->setUniformValue("tex0", 0);
    m_program->setUniformValue("tex1", 1);
    m_program->setUniformValue("tex2", 2);
    m_program->setUniformValue("mode", int(m_layout));
    m_program->setUniformValue("swapChroma", m_swapChroma);
    m_program->setUniformValue("lumaWidthUsed", m_lumaWidthUsed);
    m_program->setUniformValue("chromaWidthUsed", m_chromaWidthUsed);
    m_program->setUniformValue("colorMatrix", m_colorMatrix);
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices, int(sizeof(vertices)));
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_vertexBuffer.release();
    m_program->release();
    for (int i = 2; i >= 0; --i)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
}

void VideoCanvas::paintGL()
{
    QPainter painter(this);

    // The picture is drawn with our own GL calls; the painter has to be told,
    // or its cached idea of the GL state no longer matches and what it draws
    // afterwards silently disappears.
    painter.beginNativePainting();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const bool usable = m_program && m_program->isLinked();
    if (usable)
        {
            uploadPendingFrame();
            if (m_hasPicture)
                {
                    drawVideo(videoRect());
                }
        }
    painter.endNativePainting();

    if (!m_hasPicture)
        {
            painter.setPen(Qt::gray);
            painter.drawText(rect(), Qt::AlignCenter, usable ? tr("Open a video to start")
                             : tr("The video cannot be shown: OpenGL is not available."));
            return;
        }
    const QRectF frame = videoRect();
    // the export has nothing outside the picture, so the preview shows nothing there either
    painter.save();
    painter.setClipRect(frame);
    m_overlay.render(painter, frame, telemetryTime());
    painter.restore();

    if (m_selected >= 0 && m_selected < m_overlay.count())
        {
            // two pens, so the outline shows on bright and on dark footage alike
            const QRectF rect = m_overlay.itemRect(m_selected, frame);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRect(rect);
            painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
            painter.drawRect(rect);
            painter.setPen(QPen(Qt::black, 1));
            painter.setBrush(Qt::white);
            painter.drawRect(resizeHandle(rect));
            if (!m_overlay.hasGauge(m_selected))
                {
                    painter.setPen(Qt::white);
                    painter.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, tr("This gauge is not in the library"));
                }
        }
}
