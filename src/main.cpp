#include "launch.h"
#include "options.h"
#include "overlay_window.h"
#include "pipeline.h"

#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

static constexpr const char *kAppId = "org.omarchy.screensaver";

static int run_headless(const SsaverOptions &a, const std::string &input) {
    try {
        int cols = a.cols > 0 ? a.cols : 80;
        int rows = a.rows > 0 ? a.rows : 24;
        Pipeline p(input, cols, rows, a.effect, &a);
        int frames = 0;
        int non_space = 0;
        Frame fr;
        while (frames < a.frames && p.tick(fr)) {
            ++frames;
            for (const auto &c : fr.cells) {
                if (c.ch != U' ' && c.ch != 0)
                    ++non_space;
            }
        }
        std::printf("frames=%d cells=%dx%d non_space=%d effect=%s backend=%s\n", frames, p.cols(),
                    p.rows(), non_space, p.effect_name() ? p.effect_name() : "?",
                    p.backend() == VtBackend::Libghostty ? "libghostty" : "fallback");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "omarchy-launch-screensaver: " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char **argv) {
    SsaverOptions a = parse_args(argc, argv);
    std::string input = load_input_text(a);

    if (a.headless)
        return run_headless(a, input);

    // Match omarchy-launch-screensaver: already running => exit 0
    if (screensaver_already_running() || !acquire_instance_lock())
        return 0;

    // Toggle off blocks idle launch; `force` / --force still starts
    if (screensaver_toggled_off() && !a.force)
        return 1;

    // Idle path: don't cover the lock screen. force still starts
    // (the launch script itself never checks isLocked).
    if (!a.force && session_is_locked())
        return 0;

    quiet_walker();
    claim_screensaver_cmdline(argc > 0 ? argv[0] : nullptr);

    if (!std::getenv("WAYLAND_DISPLAY") && !std::getenv("WAYLAND_SOCKET")) {
        std::cerr << "omarchy-launch-screensaver: no WAYLAND_DISPLAY (use --headless on this box)\n";
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(kAppId));
    app.setDesktopFileName(QString::fromLatin1(kAppId));
    app.setQuitOnLastWindowClosed(true);

    HyprCursorGuard cursor;
    cursor.hide();

    const auto screens = app.screens();
    std::vector<std::unique_ptr<OverlayWindow>> windows;
    windows.reserve((size_t)std::max(1, (int)screens.size()));

    auto make_win = [&](QScreen *screen) {
        auto win = std::make_unique<OverlayWindow>(input, a, screen);
        if (screen) {
            win->setScreen(screen);
            win->setGeometry(screen->geometry());
        } else if (auto *pri = app.primaryScreen()) {
            win->resize(pri->size());
        } else {
            win->resize(1280, 720);
        }
        win->showFullScreen();
        return win;
    };

    if (screens.isEmpty()) {
        windows.push_back(make_win(app.primaryScreen()));
    } else {
        for (QScreen *screen : screens)
            windows.push_back(make_win(screen));
    }

    for (auto &w : windows) {
        QObject::connect(w.get(), &OverlayWindow::dismissRequested, [&]() {
            for (auto &x : windows)
                x->requestDismiss();
        });
    }

    return app.exec();
}
