#pragma once

#include "fade.h"
#include "options.h"
#include "pipeline.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QRasterWindow>
#include <memory>

class QScreen;

class OverlayWindow : public QRasterWindow {
    Q_OBJECT
public:
    OverlayWindow(std::string input, SsaverOptions opt, QScreen *screen);
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

private slots:
    void onFrame();

private:
    void applyLayerShell();
    void rebuildPipeline(bool force = false);
    bool pointerMoved(const QPoint &pos);

    std::string input_;
    SsaverOptions opt_;
    QScreen *screen_ = nullptr;
    std::unique_ptr<Pipeline> pipeline_;
    Frame frame_;
    Fade fade_;
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    int grid_cols_ = 0;
    int grid_rows_ = 0;
    QPoint last_pointer_;
    bool have_pointer_ = false;
    bool mapped_ = false;
    bool dismissing_ = false;
    bool exclusive_keys_ = false;
};
