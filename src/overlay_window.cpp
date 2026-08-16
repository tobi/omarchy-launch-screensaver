#include "overlay_window.h"

#include <LayerShellQt/Window>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QSize>
#include <QSurfaceFormat>
#include <QTimer>
#include <algorithm>

static constexpr qreal kFontPointSize = 18.0;
static constexpr int kMaxFps = 30;
static constexpr const char *kAppId = "org.omarchy.screensaver";

static QFont screensaver_font() {
    QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
    font.setStyleName(QStringLiteral("Regular"));
    font.setPointSizeF(kFontPointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

static void cell_metrics(int *cell_w, int *cell_h, int *ascent = nullptr) {
    QFontMetrics fm(screensaver_font());
    *cell_w = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    *cell_h = std::max(1, fm.height());
    if (ascent)
        *ascent = fm.ascent();
}

static QSize output_grid(int width, int height) {
    int cell_w = 1, cell_h = 1;
    cell_metrics(&cell_w, &cell_h);
    return QSize(std::max(8, width / cell_w), std::max(4, height / cell_h));
}


static int clamp_fps(int fps) {
    if (fps <= 0)
        return kMaxFps;
    return std::min(kMaxFps, fps);
}

OverlaySession::OverlaySession(std::string input, SsaverOptions opt, QObject *parent)
    : QObject(parent),
      input_(std::move(input)),
      opt_(std::move(opt)),
      fade_((float)std::max(1, opt_.fade_ms),
            (float)std::max(1, opt_.fade_out_ms)) {}

void OverlaySession::addWindow(OverlayWindow *w) {
    windows_.push_back(w);
}

void OverlaySession::windowReady(OverlayWindow *w) {
    if (!pipeline_)
        rebuildPipeline(false);
    if (ticking_) {
        rasterize();
        if (w)
            w->update();
        return;
    }
    ticking_ = true;
    clock_.start();
    last_ms_ = 0;
    onTick();
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &OverlaySession::onTick);
    timer->start(std::max(1, 1000 / clamp_fps(opt_.frame_rate)));
}

void OverlaySession::requestDismiss() {
    if (dismissing_)
        return;
    dismissing_ = true;
    fade_.dismiss();
}

OverlayWindow *OverlaySession::largestWindow() const {
    OverlayWindow *best = nullptr;
    qint64 best_area = 0;
    for (OverlayWindow *w : windows_) {
        if (!w || w->width() < 32 || w->height() < 32)
            continue;
        const qint64 area = (qint64)w->width() * (qint64)w->height();
        if (area > best_area) {
            best_area = area;
            best = w;
        }
    }
    return best;
}

void OverlaySession::windowResized() {
    rebuildPipeline(false);
}

void OverlaySession::rebuildPipeline(bool force) {
    OverlayWindow *src = largestWindow();
    if (!src)
        return;
    const QSize grid = output_grid(src->width(), src->height());
    int cols = opt_.cols > 0 ? opt_.cols : grid.width();
    int rows = opt_.rows > 0 ? opt_.rows : grid.height();
    if (opt_.canvas_width > 0)
        cols = opt_.canvas_width;
    if (opt_.canvas_height > 0)
        rows = opt_.canvas_height;
    if (!force && pipeline_ && cols == grid_cols_ && rows == grid_rows_)
        return;
    try {
        SsaverOptions engine = opt_;
        engine.canvas_width = 0;
        engine.canvas_height = 0;
        pipeline_ = std::make_unique<Pipeline>(input_, cols, rows, opt_.effect, &engine);
        grid_cols_ = cols;
        grid_rows_ = rows;
    } catch (...) {
        pipeline_.reset();
        grid_cols_ = 0;
        grid_rows_ = 0;
    }
}

void OverlaySession::rasterize() {
    OverlayWindow *src = largestWindow();
    if (!src)
        return;
    const int W = src->width();
    const int H = src->height();
    if (image_.width() != W || image_.height() != H)
        image_ = QImage(W, H, QImage::Format_ARGB32_Premultiplied);
    image_.fill(qRgb(0, 0, 0));
    if (frame_.cells.empty() || frame_.cols <= 0 || frame_.rows <= 0)
        return;

    QPainter p(&image_);
    const int cols = frame_.cols;
    const int rows = frame_.rows;
    p.setFont(screensaver_font());
    p.setRenderHint(QPainter::TextAntialiasing, true);
    int cellW = 1, cellH = 1, ascent = 0;
    cell_metrics(&cellW, &cellH, &ascent);
    const int originX = (W - cellW * cols) / 2;
    const int originY = (H - cellH * rows) / 2;
    for (int y = 0; y < rows; ++y) {
        const int y0 = originY + y * cellH;
        for (int x = 0; x < cols; ++x) {
            const Cell &c = frame_.cells[(size_t)y * (size_t)cols + (size_t)x];
            const int x0 = originX + x * cellW;
            if (c.bg_r | c.bg_g | c.bg_b)
                p.fillRect(x0, y0, cellW, cellH, QColor(c.bg_r, c.bg_g, c.bg_b));
            if (c.ch != U' ' && c.ch != 0) {
                p.setPen(QColor(c.fg_r, c.fg_g, c.fg_b));
                const char32_t ch = static_cast<char32_t>(c.ch);
                p.drawText(x0, y0 + ascent, QString::fromUcs4(&ch, 1));
            }
        }
    }
}

void OverlaySession::onTick() {
    const qint64 now = clock_.elapsed();
    fade_.tick(float(now - last_ms_));
    last_ms_ = now;
    if (fade_.done()) {
        QCoreApplication::quit();
        return;
    }
    if (pipeline_) {
        const int engine_fps = std::max(1, opt_.frame_rate);
        const int present = clamp_fps(engine_fps);
        const int steps = std::max(1, engine_fps / present);
        for (int i = 0; i < steps; ++i) {
            if (!pipeline_->tick(frame_)) {
                rebuildPipeline(true);
                if (pipeline_)
                    pipeline_->tick(frame_);
            }
        }
    }
    rasterize();
    for (OverlayWindow *w : windows_)
        w->update();
}

OverlayWindow::OverlayWindow(OverlaySession &session, QScreen *screen)
    : session_(&session), screen_(screen) {
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    setFormat(fmt);
    setTitle(QString::fromLatin1(kAppId));
    setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::BlankCursor);
    applyLayerShell();
}

OverlayWindow::~OverlayWindow() = default;

void OverlayWindow::applyLayerShell() {
    auto *ls = LayerShellQt::Window::get(this);
    if (!ls)
        return;
    ls->setLayer(LayerShellQt::Window::LayerOverlay);
    ls->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    ls->setExclusiveZone(-1);
    ls->setDesiredSize(QSize(0, 0));
    static bool exclusive_given = false;
    exclusive_keys_ = !exclusive_given;
    exclusive_given = true;
    ls->setKeyboardInteractivity(
        exclusive_keys_ ? LayerShellQt::Window::KeyboardInteractivityExclusive
                        : LayerShellQt::Window::KeyboardInteractivityOnDemand);
    ls->setScope(QString::fromLatin1(kAppId));
    ls->setWantsToBeOnActiveScreen(false);
    if (screen_)
        ls->setScreen(screen_);
    ls->setActivateOnShow(exclusive_keys_);
}

void OverlayWindow::pinToScreen(QScreen *screen) {
    screen_ = screen;
    if (screen)
        setScreen(screen);
    auto *ls = LayerShellQt::Window::get(this);
    if (!ls)
        return;
    ls->setWantsToBeOnActiveScreen(false);
    if (screen) {
        ls->setScreen(screen);
        ls->setDesiredSize(screen->size());
    }
}

bool OverlayWindow::pointerMoved(const QPoint &pos) {
    if (!have_pointer_) {
        last_pointer_ = pos;
        have_pointer_ = true;
        return false;
    }
    return (pos - last_pointer_).manhattanLength() > 2;
}

void OverlayWindow::requestDismiss() {
    session_->requestDismiss();
}

void OverlayWindow::exposeEvent(QExposeEvent *event) {
    QRasterWindow::exposeEvent(event);
    if (!mapped_) {
        mapped_ = true;
        session_->windowReady(this);
    }
}

void OverlayWindow::resizeEvent(QResizeEvent *event) {
    QRasterWindow::resizeEvent(event);
    if (mapped_)
        session_->windowResized();
}

void OverlayWindow::keyPressEvent(QKeyEvent *) { requestDismiss(); }
void OverlayWindow::mouseMoveEvent(QMouseEvent *e) {
    if (pointerMoved(e->position().toPoint()))
        requestDismiss();
}
void OverlayWindow::mousePressEvent(QMouseEvent *) { requestDismiss(); }
void OverlayWindow::focusOutEvent(QFocusEvent *e) { QRasterWindow::focusOutEvent(e); }

bool OverlayWindow::event(QEvent *event) {
    if (event->type() == QEvent::HoverMove) {
        auto *he = static_cast<QHoverEvent *>(event);
        if (pointerMoved(he->position().toPoint()))
            requestDismiss();
    }
    return QRasterWindow::event(event);
}

void OverlayWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const int W = width();
    const int H = height();
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(0, 0, W, H, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(session_->alpha());
    p.fillRect(0, 0, W, H, QColor(0, 0, 0));

    const QImage &img = session_->frameImage();
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return;

    const QSize target = img.size().scaled(W, H, Qt::KeepAspectRatioByExpanding);
    const QRect dest((W - target.width()) / 2, (H - target.height()) / 2,
                     target.width(), target.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(dest, img);
}
