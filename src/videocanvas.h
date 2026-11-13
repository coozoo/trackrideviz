#pragma once

#include "overlay/overlay.h"
#include "sync/syncmap.h"

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

    void setOverlay(const Overlay &overlay);
    // Index of the selected gauge, -1 for none.
    int selectedItem() const { return m_selected; }
    void setSelectedItem(int index);

    // Width / height of the picture as shown; 0 while there is none.
    double pictureAspect() const;

    // Telemetry time of the frame on screen (video time + sync offset).
    double telemetryTime() const { return m_sync.telemetryTime(m_videoTime); }

public slots:
    // Connected to QVideoSink::videoFrameChanged.
    void setFrame(const QVideoFrame &frame);
    // Player position in ms; used for frames that carry no timestamp.
    void setPosition(qint64 positionMs);
    // telemetry time = video time + offset
    void setSync(const SyncMap &sync);
    void clear();

signals:
    void selectedItemChanged(int index);
    // The user finished moving or resizing a gauge. Fractions of the frame, as in OverlayItem.
    void itemGeometryEdited(int index, double x, double y, double height);
    void itemDeleteRequested(int index);
    void itemMenuRequested(int index, const QPoint &globalPosition);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
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
    SyncMap m_sync;
    QRectF resizeHandle(const QRectF &itemRect) const;
    void updateCursor(const QPointF &position);

    Overlay m_overlay;
    int m_selected{-1};
    // A gauge being moved or resized with the mouse. The overlay shows it
    // where the mouse has it; the project hears about it on release.
    enum Drag { NoDrag, Moving, Resizing };
    Drag m_drag{NoDrag};
    QPointF m_dragStart;
    QRectF m_dragStartRect;
    bool m_dragMoved{false};
};
