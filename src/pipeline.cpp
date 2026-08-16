#include "pipeline.h"

#include <algorithm>
#include <stdexcept>

Pipeline::Pipeline(std::string input, int cols, int rows, std::string effect,
                   const SsaverOptions *opt)
    : cols_(std::max(1, cols)), rows_(std::max(1, rows)) {
    const char *eff = effect.empty() || effect == "random" ? nullptr : effect.c_str();
    TtfxConfig cfg{};
    cfg.input = input.c_str();
    cfg.cols = cols_;
    cfg.rows = rows_;
    cfg.effect = eff;
    cfg.frame_rate = opt ? opt->frame_rate : 120;
    cfg.canvas_width = opt ? opt->canvas_width : 0;
    cfg.canvas_height = opt ? opt->canvas_height : 0;
    cfg.reuse_canvas = !opt || opt->reuse_canvas ? 1 : 0;
    cfg.anchor_canvas = opt ? opt->anchor_canvas.c_str() : "c";
    cfg.anchor_text = opt ? opt->anchor_text.c_str() : "c";
    cfg.no_eol = !opt || opt->no_eol ? 1 : 0;
    cfg.no_restore_cursor = !opt || opt->no_restore_cursor ? 1 : 0;
    cfg.has_seed = opt && opt->has_seed ? 1 : 0;
    cfg.seed = opt ? opt->seed : 0;
    cfg.include_effects =
        opt && !opt->include_effects.empty() ? opt->include_effects.c_str() : nullptr;
    cfg.exclude_effects =
        opt && !opt->exclude_effects.empty() ? opt->exclude_effects.c_str() : nullptr;
    ttfx_ = ttfx_create_ex(&cfg);
    if (!ttfx_) {
        const char *err = ttfx_last_error();
        throw std::runtime_error(err ? err : "ttfx_create failed");
    }
}

Pipeline::~Pipeline() {
    if (ttfx_)
        ttfx_destroy((TtfxEngine *)ttfx_);
}

const char *Pipeline::effect_name() const {
    return ttfx_ ? ttfx_effect_name((TtfxEngine *)ttfx_) : "";
}

bool Pipeline::tick(Frame &out) {
    out.cols = cols_;
    out.rows = rows_;
    out.cells.resize((size_t)cols_ * (size_t)rows_);
    const int next = ttfx_next_cells((TtfxEngine *)ttfx_, out.cells.data(), cols_, rows_);
    if (next < 0) {
        const char *err = ttfx_last_error();
        throw std::runtime_error(err ? err : "ttfx_next_cells failed");
    }
    return next == 1;
}
