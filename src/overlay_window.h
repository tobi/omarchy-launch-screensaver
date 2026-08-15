#pragma once

#include "fade.h"
#include "options.h"
#include "pipeline.h"

#include <QElapsedTimer>
#include <QRasterWindow>
#include <memory>
#include <string>

class QScreen;

class OverlayWindow : public QRasterWindow {
    Q_OBJECT
public:
    OverlayWindow(std::string input, SsaverOptions opt, QScreen *screen);

    void requestDismiss();

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
    void rebuildPipeline();
    bool input_armed() const;

    std::string input_;
    SsaverOptions opt_;
    QScreen *screen_ = nullptr;
    std::unique_ptr<Pipeline> pipeline_;
    Frame frame_;
    Fade fade_;
    QElapsedTimer clock_;
    qint64 last_ms_ = 0;
    bool layer_applied_ = false;
    bool dismissing_ = false;
};
