#pragma once

#include "overlay/overlay.h"
#include "telemetry/telemetrysession.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVideoFrame>

class QOpenGLShaderProgram;

// Preview: the current video frame, letterboxed, with the gauges on top.
//
// The picture goes to the GPU as the decoder delivers it (YUV planes as
// textures; a shader converts and scales), so the GUI thread spends well
// under a millisecond per frame. Formats the shader does not know take the
// slow road through QVideoFrame::toImage().
// Gauges are drawn by Overlay::render() - the same code the export uses.
class VideoCanvas : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit VideoCanvas(QWidget *parent = nullptr);
    ~VideoCanvas() override;

    void setSession(const TelemetrySessionPtr &session);
    void setOverlay(const Overlay &overlay);

    // Telemetry time of the frame on screen (video time + sync offset).
    double telemetryTime() const { return m_videoTime + m_syncOffset; }

public slots:
    // Connected to QVideoSink::videoFrameChanged.
    void setFrame(const QVideoFrame &frame);
    // Player position in ms; used for frames that carry no timestamp.
    void setPosition(qint64 positionMs);
    // telemetry time = video time + offset
    void setSyncOffset(double seconds);
    void clear();

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    enum Layout
    {
        Rgba,        // one RGBA texture (fallback)
        Planar,      // Y, U, V in three textures
        SemiPlanar   // Y, then interleaved chroma in one two-channel texture
    };

    void releaseGl();
    void uploadPendingFrame();
    bool uploadYuv(QVideoFrame &frame);
    void uploadImage(const QImage &image);
    // Returns the fraction of the texture width that holds picture (rows may be padded).
    float uploadPlane(int index, const uchar *data, int bytesPerLine, int bytesPerPixel, int width, int height);
    void drawVideo(const QRectF &target);
    QSizeF displaySize() const;
    QRectF videoRect() const;

    QVideoFrame m_pendingFrame;     // not uploaded yet
    bool m_hasPicture{false};       // textures hold something to show
    QSize m_pictureSize;            // as stored, before rotation
    int m_rotation{0};              // degrees clockwise: 0, 90, 180, 270
    Layout m_layout{Rgba};
    bool m_swapChroma{false};
    float m_lumaWidthUsed{1.0f};
    float m_chromaWidthUsed{1.0f};
    QMatrix4x4 m_colorMatrix;       // YUV -> RGB

    QOpenGLShaderProgram *m_program{nullptr};
    QOpenGLBuffer m_vertexBuffer;
    unsigned int m_textures[3] {0, 0, 0};
    QSize m_textureSizes[3];
    int m_textureFormats[3] {0, 0, 0};

    double m_videoTime{0.0};
    double m_positionTime{0.0};
    double m_syncOffset{0.0};
    TelemetrySessionPtr m_session;
    Overlay m_overlay;
};
