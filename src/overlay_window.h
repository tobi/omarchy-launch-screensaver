#pragma once

#include "fade.h"
#include "options.h"
#include "pipeline.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRasterWindow>
#include <memory>
#include <string>
#include <vector>

class QScreen;
class OverlayWindow;

// One engine + one raster, sized to the largest mapped output.
// Smaller overlays blit that image.
class OverlaySession : public QObject {
    Q_OBJECT
public:
    OverlaySession(std::string input, SsaverOptions opt, QObject *parent = nullptr);

    void addWindow(OverlayWindow *w);
    void windowReady(OverlayWindow *w);
    void windowResized();
    const QImage &frameImage() const { return image_; }
    float alpha() const { return fade_.alpha(); }
    void requestDismiss();

private slots:
    void onTick();

private:
    void rebuildPipeline(bool force);
    void rasterize();
    OverlayWindow *largestWindow() const;

    std::string input_;
    SsaverOptions opt_;
    std::vector<OverlayWindow *> windows_;
    std::unique_ptr<Pipeline> pipeline_;
    Frame frame_;
    Fade fade_;
    QImage image_;
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    int grid_cols_ = 0;
    int grid_rows_ = 0;
    bool ticking_ = false;
    bool dismissing_ = false;
};

class OverlayWindow : public QRasterWindow {
    Q_OBJECT
public:
    OverlayWindow(OverlaySession &session, QScreen *screen);
    ~OverlayWindow() override;

    void requestDismiss();
    void pinToScreen(QScreen *screen);

signals:
    void dismissRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool event(QEvent *event) override;

private:
    void applyLayerShell();
    bool pointerMoved(const QPoint &pos);

    OverlaySession *session_ = nullptr;
    QScreen *screen_ = nullptr;
    QPoint last_pointer_;
    bool have_pointer_ = false;
    bool mapped_ = false;
    bool exclusive_keys_ = false;
};
