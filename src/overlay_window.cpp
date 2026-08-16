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
#include <qguiapplication_platform.h>

static constexpr qreal kFontPointSize = 9.0;

static QFont screensaver_font() {
    QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
    font.setStyleName(QStringLiteral("Regular"));
    font.setPointSizeF(kFontPointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

static QSize input_grid_size(const std::string &input) {
    int cols = 0;
    int row_cols = 0;
    int rows = 1;
    for (unsigned char ch : input) {
        if (ch == '\n') {
            cols = std::max(cols, row_cols);
            row_cols = 0;
            ++rows;
        } else if (ch == '\t') {
            row_cols += 4;
        } else if ((ch & 0xc0) != 0x80) {
            ++row_cols;
        }
    }
    cols = std::max(cols, row_cols);
    if (!input.empty() && input.back() == '\n' && rows > 1)
        --rows;
    return QSize(std::max(1, cols), std::max(1, rows));
}

static QFont fitted_font(int width, int height, int cols, int rows) {
    QFont font = screensaver_font();
    int low = 1;
    int high = std::max(1, height);
    while (low < high) {
        const int px = low + (high - low + 1) / 2;
        font.setPixelSize(px);
        QFontMetrics fm(font);
        if (fm.horizontalAdvance(QLatin1Char('M')) * cols <= width &&
            fm.height() * rows <= height)
            low = px;
        else
            high = px - 1;
    }
    font.setPixelSize(low);
    return font;
}
static constexpr const char *kAppId = "org.omarchy.screensaver";

OverlayWindow::OverlayWindow(std::string input, SsaverOptions opt, QScreen *screen)
    : input_(std::move(input)),
      opt_(std::move(opt)),
      screen_(screen),
      fade_((float)std::max(1, opt_.fade_ms),
            (float)std::max(1, opt_.fade_out_ms)) {
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

void OverlayWindow::rebuildPipeline(bool force) {
    if (width() < 32 || height() < 32)
        return;
    const QSize input_grid = input_grid_size(input_);
    int cols = opt_.cols > 0 ? opt_.cols : input_grid.width();
    int rows = opt_.rows > 0 ? opt_.rows : input_grid.height();
    if (opt_.canvas_width > 0)
        cols = opt_.canvas_width;
    if (opt_.canvas_height > 0)
        rows = opt_.canvas_height;
    if (!force && pipeline_ && cols == grid_cols_ && rows == grid_rows_)
        return;
    try {
        pipeline_ = std::make_unique<Pipeline>(input_, cols, rows, opt_.effect, &opt_);
        grid_cols_ = cols;
        grid_rows_ = rows;
    } catch (...) {
        pipeline_.reset();
        grid_cols_ = 0;
        grid_rows_ = 0;
    }
}

void OverlayWindow::requestDismiss() {
    if (dismissing_)
        return;
    dismissing_ = true;
    fade_.dismiss();
    emit dismissRequested();
}

void OverlayWindow::exposeEvent(QExposeEvent *event) {
    QRasterWindow::exposeEvent(event);
    if (!mapped_) {
        mapped_ = true;
        clock_.start();
        last_ms_ = 0;
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &OverlayWindow::onFrame);
        const int fps = std::max(1, opt_.frame_rate > 0 ? opt_.frame_rate : 120);
        timer->start(std::max(1, 1000 / fps));
    }
    if (!pipeline_)
        rebuildPipeline();
}

void OverlayWindow::resizeEvent(QResizeEvent *event) {
    QRasterWindow::resizeEvent(event);
    if (mapped_)
        rebuildPipeline();
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
            rebuildPipeline(true);
            if (pipeline_)
                pipeline_->tick(frame_);
        }
    }
    update();
}

void OverlayWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const int W = width();
    const int H = height();

    // Bake the fade into each committed ARGB buffer. Protocol-side alpha is
    // latched on wl_surface commit and could otherwise remain stale until an
    // input event caused Qt to commit again.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(0, 0, W, H, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(fade_.alpha());
    p.fillRect(0, 0, W, H, QColor(0, 0, 0));
    if (frame_.cells.empty() || frame_.cols <= 0 || frame_.rows <= 0)
        return;
    const int cols = frame_.cols;
    const int rows = frame_.rows;
    QFont font = fitted_font(W, H, cols, rows);
    p.setFont(font);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFontMetrics fm(p.font());
    const int cellW = std::max(1, fm.horizontalAdvance(QLatin1Char('M')));
    const int cellH = std::max(1, fm.height());
    const int ascent = fm.ascent();
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
                p.drawText(x0, y0 + ascent, QString::fromUcs4(&c.ch, 1));
            }
        }
    }
}
