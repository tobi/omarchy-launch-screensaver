#include "options.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

static void die_need(const char *name) {
    std::cerr << "omarchy-launch-screensaver: " << name << " needs a value\n";
    std::exit(2);
}

static const char *take(int &i, int argc, char **argv, const char *name, const char *eq) {
    if (eq)
        return eq;
    if (i + 1 >= argc)
        die_need(name);
    return argv[++i];
}

static int take_int(int &i, int argc, char **argv, const char *name, const char *eq) {
    const char *s = take(i, argc, argv, name, eq);
    char *end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (!end || *end) {
        std::cerr << "omarchy-launch-screensaver: " << name << " expects an integer, got '" << s
                  << "'\n";
        std::exit(2);
    }
    return (int)v;
}

static uint64_t take_u64(int &i, int argc, char **argv, const char *name, const char *eq) {
    const char *s = take(i, argc, argv, name, eq);
    char *end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (!end || *end) {
        std::cerr << "omarchy-launch-screensaver: " << name << " expects an integer, got '" << s
                  << "'\n";
        std::exit(2);
    }
    return (uint64_t)v;
}

void print_help() {
    std::cout <<
        "omarchy-launch-screensaver — Omarchy screensaver (Qt + ttfx + libghostty)\n"
        "\n"
        "Usage:\n"
        "  omarchy-launch-screensaver [force] [options]\n"
        "\n"
        "Drop-in replacement for Omarchy's launcher + effect loop. One Wayland\n"
        "overlay per output; does not spawn a terminal or a ttfx child.\n"
        "\n"
        "  force, --force          launch even if screensaver toggle is off\n"
        "  -h, --help              show this help\n"
        "  -i, --input PATH        input text (also --input-file)\n"
        "  --effect NAME           pin one effect (still loops it)\n"
        "  -R, --random-effect     pick a random effect (default)\n"
        "  --include-effects LIST  limit random pick (comma-separated)\n"
        "  --exclude-effects LIST  skip these when random\n"
        "  --frame-rate N          default 120\n"
        "  --fade N[,OUT]          appear ms (default 1000), dismiss ms (default 200)\n"
        "  --canvas-width N        0 = output width\n"
        "  --canvas-height N       0 = output height\n"
        "  --reuse-canvas          keep canvas between frames (default on)\n"
        "  --anchor-canvas c|...   canvas anchor (default c)\n"
        "  --anchor-text c|...     text anchor (default c)\n"
        "  --no-eol                omit trailing newline (default on)\n"
        "  --no-restore-cursor     leave cursor hidden (default on)\n"
        "  --seed N                deterministic RNG\n"
        "  --headless              no Wayland; for tests\n"
        "  --frames N              headless only (default 30)\n"
        "  --cols N --rows N       headless canvas (default 80x24)\n"
        "\n"
        "Input file default: ~/.config/omarchy/branding/screensaver.txt\n"
        "then the bundled assets/screensaver.txt.\n"
        "\n"
        "Toggle: if ~/.local/state/omarchy/toggles/screensaver-off exists\n"
        "(omarchy-toggle-enabled screensaver-off), launch exits 1 unless force.\n"
        "Menu System > Screensaver calls: omarchy-launch-screensaver force\n";
}

SsaverOptions parse_args(int argc, char **argv) {
    SsaverOptions a;
    for (int i = 1; i < argc; ++i) {
        std::string raw = argv[i];
        std::string key = raw;
        const char *eq = nullptr;
        if (raw.rfind("--", 0) == 0) {
            auto pos = raw.find('=');
            if (pos != std::string::npos) {
                key = raw.substr(0, pos);
                eq = argv[i] + pos + 1;
            }
        }

        if (raw == "force" || key == "--force") {
            a.force = true;
        } else if (key == "-h" || key == "--help") {
            print_help();
            std::exit(0);
        } else if (key == "-i" || key == "--input" || key == "--input-file") {
            a.input_path = take(i, argc, argv, "--input", eq);
        } else if (key == "--effect") {
            a.effect = take(i, argc, argv, "--effect", eq);
            a.random_effect = false;
        } else if (key == "-R" || key == "--random-effect" || key == "--random") {
            a.effect.clear();
            a.random_effect = true;
        } else if (key == "--include-effects") {
            a.include_effects = take(i, argc, argv, "--include-effects", eq);
        } else if (key == "--exclude-effects") {
            a.exclude_effects = take(i, argc, argv, "--exclude-effects", eq);
        } else if (key == "--frame-rate") {
            a.frame_rate = take_int(i, argc, argv, "--frame-rate", eq);
        } else if (key == "--fade") {
            const char *s = take(i, argc, argv, "--fade", eq);
            char *end = nullptr;
            long in = std::strtol(s, &end, 10);
            if (end == s || in <= 0) {
                std::cerr << "omarchy-launch-screensaver: --fade needs N or N,OUT\n";
                std::exit(2);
            }
            a.fade_ms = (int)in;
            if (*end == ',' || *end == ':') {
                char *end2 = nullptr;
                long out = std::strtol(end + 1, &end2, 10);
                if (end2 == end + 1 || *end2 || out <= 0) {
                    std::cerr << "omarchy-launch-screensaver: --fade OUT must be a positive int\n";
                    std::exit(2);
                }
                a.fade_out_ms = (int)out;
            } else if (*end) {
                std::cerr << "omarchy-launch-screensaver: --fade needs N or N,OUT\n";
                std::exit(2);
            }
        } else if (key == "--canvas-width") {
            a.canvas_width = take_int(i, argc, argv, "--canvas-width", eq);
        } else if (key == "--canvas-height") {
            a.canvas_height = take_int(i, argc, argv, "--canvas-height", eq);
        } else if (key == "--reuse-canvas") {
            a.reuse_canvas = true;
        } else if (key == "--anchor-canvas") {
            a.anchor_canvas = take(i, argc, argv, "--anchor-canvas", eq);
        } else if (key == "--anchor-text") {
            a.anchor_text = take(i, argc, argv, "--anchor-text", eq);
        } else if (key == "--no-eol") {
            a.no_eol = true;
        } else if (key == "--no-restore-cursor") {
            a.no_restore_cursor = true;
        } else if (key == "--seed") {
            a.seed = take_u64(i, argc, argv, "--seed", eq);
            a.has_seed = true;
        } else if (key == "--headless") {
            a.headless = true;
        } else if (key == "--frames") {
            a.frames = take_int(i, argc, argv, "--frames", eq);
        } else if (key == "--cols") {
            a.cols = take_int(i, argc, argv, "--cols", eq);
        } else if (key == "--rows") {
            a.rows = take_int(i, argc, argv, "--rows", eq);
        } else {
            std::cerr << "omarchy-launch-screensaver: unknown arg " << raw << "\n";
            std::exit(2);
        }
    }
    if (a.frame_rate <= 0)
        a.frame_rate = 120;
    if (a.fade_ms <= 0)
        a.fade_ms = 1000;
    if (a.fade_out_ms <= 0)
        a.fade_out_ms = 200;
    if (a.frames < 0)
        a.frames = 0;
    return a;
}

static std::string read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string load_input_text(const SsaverOptions &opt) {
    if (!opt.input_path.empty()) {
        auto s = read_file(opt.input_path);
        if (s.empty()) {
            std::cerr << "omarchy-launch-screensaver: cannot read --input " << opt.input_path
                      << "\n";
            std::exit(1);
        }
        return s;
    }
    const char *home = std::getenv("HOME");
    if (home) {
        auto s = read_file(std::string(home) + "/.config/omarchy/branding/screensaver.txt");
        if (!s.empty())
            return s;
    }
#ifdef SSAVER_ASSET_DIR
    {
        auto s = read_file(std::string(SSAVER_ASSET_DIR) + "/screensaver.txt");
        if (!s.empty())
            return s;
    }
#endif
    const char *candidates[] = {
        "assets/screensaver.txt",
        "/workspace/ssaver/assets/screensaver.txt",
    };
    for (auto *p : candidates) {
        auto s = read_file(p);
        if (!s.empty())
            return s;
    }
    return "omarchy\n";
}
