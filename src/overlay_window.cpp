#include "overlay_window.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <qguiapplication_platform.h>
#include <qscreen_platform.h>
#include "layer_shell.h"

static constexpr int kCellW = 10;
static constexpr int kCellH = 18;
static constexpr const char *kAppId = "org.omarchy.screensaver";
static constexpr qint64 kInputGraceMs = 250;

OverlayWindow::OverlayWindow(std::string input, SsaverOptions opt, QScreen *screen)
    : input_(std::move(input)), opt_(std::move(opt)), screen_(screen) {
    setTitle(QString::fromLatin1(kAppId));
    setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::BlankCursor);
    clock_.start();
    last_ms_ = clock_.elapsed();

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OverlayWindow::onFrame);
    const int fps = std::max(1, opt_.frame_rate > 0 ? opt_.frame_rate : 120);
    timer->start(std::max(1, 1000 / fps));
}

void OverlayWindow::applyLayerShell() {
    if (layer_applied_)
        return;
    auto *wapp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    void *display = wapp ? (void *)wapp->display() : nullptr;
    void *surface = (void *)winId();
    void *output = nullptr;
#if QT_CONFIG(wayland)
    if (screen_) {
        if (auto *ni = screen_->nativeInterface<QNativeInterface::QWaylandScreen>())
            output = ni->output();
    }
#endif
    if (ssaver_apply_layer_shell(display, surface, output))
        layer_applied_ = true;
}

void OverlayWindow::rebuildPipeline() {
    int cols = opt_.cols > 0 ? opt_.cols : std::max(8, width() / kCellW);
    int rows = opt_.rows > 0 ? opt_.rows : std::max(4, height() / kCellH);
    if (opt_.canvas_width > 0)
        cols = opt_.canvas_width;
    if (opt_.canvas_height > 0)
        rows = opt_.canvas_height;
    try {
        pipeline_ = std::make_unique<Pipeline>(input_, cols, rows, opt_.effect, &opt_);
    } catch (...) {
        pipeline_.reset();
    }
}

bool OverlayWindow::input_armed() const { return clock_.elapsed() >= kInputGraceMs; }

void OverlayWindow::requestDismiss() {
    if (!input_armed())
        return;
    if (dismissing_)
        return;
    dismissing_ = true;
    fade_.dismiss();
    emit dismissRequested();
}

void OverlayWindow::exposeEvent(QExposeEvent *event) {
    QRasterWindow::exposeEvent(event);
    applyLayerShell();
    if (!pipeline_)
        rebuildPipeline();
}

void OverlayWindow::resizeEvent(QResizeEvent *event) {
    QRasterWindow::resizeEvent(event);
    rebuildPipeline();
}

void OverlayWindow::keyPressEvent(QKeyEvent *) { requestDismiss(); }
void OverlayWindow::mouseMoveEvent(QMouseEvent *) { requestDismiss(); }
void OverlayWindow::mousePressEvent(QMouseEvent *) { requestDismiss(); }
void OverlayWindow::focusOutEvent(QFocusEvent *e) {
    QRasterWindow::focusOutEvent(e);
    requestDismiss();
}

bool OverlayWindow::event(QEvent *event) {
    switch (event->type()) {
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::Enter:
        requestDismiss();
        break;
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
        requestDismiss();
        break;
    default:
        break;
    }
    return QRasterWindow::event(event);
}

void OverlayWindow::onFrame() {
    const qint64 now = clock_.elapsed();
    fade_.tick(float(now - last_ms_));
    last_ms_ = now;
    if (fade_.done()) {
        QCoreApplication::quit();
        return;
    }
    if (pipeline_) {
        if (!pipeline_->tick(frame_)) {
            // loop another random (or the pinned) effect — omarchy-screensaver's while true
            rebuildPipeline();
            if (pipeline_)
                pipeline_->tick(frame_);
        }
    }
    setOpacity(qreal(fade_.alpha()));
    update();
}

void OverlayWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(QRect(0, 0, width(), height()), QColor(0, 0, 0));
    if (frame_.cells.empty() || frame_.cols <= 0)
        return;
    QFont font(QStringLiteral("DejaVu Sans Mono"));
    font.setPixelSize(kCellH - 2);
    p.setFont(font);
    const int cellW = std::max(1, width() / frame_.cols);
    const int cellH = std::max(1, height() / frame_.rows);
    for (int y = 0; y < frame_.rows; ++y) {
        for (int x = 0; x < frame_.cols; ++x) {
            const Cell &c = frame_.cells[(size_t)y * (size_t)frame_.cols + (size_t)x];
            QRect r(x * cellW, y * cellH, cellW, cellH);
            if (c.bg_r | c.bg_g | c.bg_b)
                p.fillRect(r, QColor(c.bg_r, c.bg_g, c.bg_b));
            if (c.ch != U' ' && c.ch != 0) {
                p.setPen(QColor(c.fg_r, c.fg_g, c.fg_b));
                p.drawText(r, Qt::AlignCenter, QString::fromUcs4(&c.ch, 1));
            }
        }
    }
}
