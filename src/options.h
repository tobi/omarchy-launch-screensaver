#pragma once

#include <cstdint>
#include <string>

// Shared CLI / engine options. Defaults match Omarchy's omarchy-screensaver
// ttfx invocation (120 fps, canvas = output, centered, reuse, no-eol).
struct SsaverOptions {
    bool force = false;
    bool headless = false;
    std::string input_path;
    std::string effect; // empty + random_effect = random
    bool random_effect = true;
    int cols = 0;  // 0 = output size (overlay) or 80 (headless)
    int rows = 0;  // 0 = output size (overlay) or 24 (headless)
    int frames = 30;
    int frame_rate = 120;
    int canvas_width = 0;  // 0 = terminal/output cols
    int canvas_height = 0; // 0 = terminal/output rows
    bool reuse_canvas = true;
    std::string anchor_canvas = "c";
    std::string anchor_text = "c";
    bool no_eol = true;
    bool no_restore_cursor = true;
    bool has_seed = false;
    uint64_t seed = 0;
    std::string include_effects; // comma/space separated
    std::string exclude_effects;
};

SsaverOptions parse_args(int argc, char **argv);
void print_help();
std::string load_input_text(const SsaverOptions &opt);
