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
        int empty_rows = 0;
        Frame fr;
        while (frames < a.frames && p.tick(fr)) {
            ++frames;
            non_space = 0;
            empty_rows = 0;
            for (int y = 0; y < fr.rows; ++y) {
                bool any = false;
                for (int x = 0; x < fr.cols; ++x) {
                    const Cell &c = fr.cells[(size_t)y * (size_t)fr.cols + (size_t)x];
                    if (c.ch != U' ' && c.ch != 0) {
                        ++non_space;
                        any = true;
                    }
                }
                if (!any)
                    ++empty_rows;
            }
        }
        std::printf("frames=%d cells=%dx%d non_space=%d empty_rows=%d effect=%s backend=%s\n",
                    frames, p.cols(), p.rows(), non_space, empty_rows,
                    p.effect_name() ? p.effect_name() : "?",
                    p.backend() == VtBackend::Libghostty ? "libghostty" : "fallback");
        if (std::getenv("SSAVER_DUMP") && !fr.vt.empty()) {
            int nls = 0, crs = 0;
            for (unsigned char b : fr.vt) {
                if (b == '\n')
                    ++nls;
                else if (b == '\r')
                    ++crs;
            }
            std::printf("vt_len=%zu newlines=%d crs=%d\n", fr.vt.size(), nls, crs);
            if (FILE *vf = std::fopen("/tmp/ssaver.vt", "wb")) {
                std::fwrite(fr.vt.data(), 1, fr.vt.size(), vf);
                std::fclose(vf);
            }
            const std::string &vt = fr.vt;
            std::printf("cup_rows=");
            for (size_t i = 0; i + 1 < vt.size(); ++i) {
                if ((unsigned char)vt[i] != 0x1b || vt[i + 1] != '[')
                    continue;
                size_t j = i + 2;
                int row = 0;
                bool have = false;
                while (j < vt.size() && vt[j] >= '0' && vt[j] <= '9') {
                    have = true;
                    row = row * 10 + (vt[j++] - '0');
                }
                if (j < vt.size() && (vt[j] == 'H' || vt[j] == 'f' || vt[j] == ';')) {
                    if (have)
                        std::printf("%d ", row);
                }
            }
            std::printf("\n");
        }
        if (std::getenv("SSAVER_DUMP") && fr.cols > 0 && fr.rows > 0) {
            for (int y = 0; y < fr.rows; ++y) {
                for (int x = 0; x < fr.cols; ++x) {
                    char32_t ch = fr.cells[(size_t)y * (size_t)fr.cols + (size_t)x].ch;
                    if (ch < 32 || ch == 0)
                        ch = U' ';
                    char buf[8] = {};
                    if (ch < 128)
                        buf[0] = (char)ch;
                    else
                        buf[0] = '#';
                    std::fputs(buf, stdout);
                }
                std::fputc('\n', stdout);
            }
        }
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
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    qputenv("QT_NO_XDG_DESKTOP_PORTAL", "1");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(kAppId));
    app.setDesktopFileName(QString::fromLatin1(kAppId));
    app.setQuitOnLastWindowClosed(true);

    const auto screens = app.screens();
    OverlaySession session(input, a);
    std::vector<std::unique_ptr<OverlayWindow>> windows;
    windows.reserve((size_t)std::max(1, (int)screens.size()));

    auto make_win = [&](QScreen *screen) {
        auto win = std::make_unique<OverlayWindow>(session, screen);
        session.addWindow(win.get());
        win->pinToScreen(screen);
        if (screen)
            win->setGeometry(screen->geometry());
        else if (auto *pri = app.primaryScreen())
            win->resize(pri->size());
        else
            win->resize(1280, 720);
        win->showFullScreen();
        return win;
    };

    if (screens.isEmpty()) {
        windows.push_back(make_win(app.primaryScreen()));
    } else {
        for (QScreen *screen : screens)
            windows.push_back(make_win(screen));

    }

    return app.exec();
}
