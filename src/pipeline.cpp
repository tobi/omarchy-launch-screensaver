#include "pipeline.h"
#include "ttfx_c.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

bool Pipeline::libghostty_linked() { return true; }

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
    cfg.include_effects = opt && !opt->include_effects.empty() ? opt->include_effects.c_str() : nullptr;
    cfg.exclude_effects = opt && !opt->exclude_effects.empty() ? opt->exclude_effects.c_str() : nullptr;
    ttfx_ = ttfx_create_ex(&cfg);
    if (!ttfx_) {
        const char *err = ttfx_last_error();
        throw std::runtime_error(err ? err : "ttfx_create failed");
    }
    const char *be = ttfx_backend((TtfxEngine *)ttfx_);
    backend_ = (be && std::strcmp(be, "libghostty") == 0) ? VtBackend::Libghostty
                                                          : VtBackend::Fallback;
}

Pipeline::~Pipeline() {
    if (ttfx_)
        ttfx_destroy((TtfxEngine *)ttfx_);
}

const char *Pipeline::effect_name() const {
    return ttfx_ ? ttfx_effect_name((TtfxEngine *)ttfx_) : "";
}

bool Pipeline::tick(Frame &out) {
    const uint8_t *data = nullptr;
    size_t len = 0;
    int rc = ttfx_next_frame((TtfxEngine *)ttfx_, &data, &len);
    if (rc < 0) {
        const char *err = ttfx_last_error();
        throw std::runtime_error(err ? err : "ttfx_next_frame failed");
    }
    if (rc == 0)
        return false;
    out.vt.assign(reinterpret_cast<const char *>(data), len);
    out.cols = cols_;
    out.rows = rows_;
    out.cells.assign((size_t)cols_ * (size_t)rows_, Cell{});
    feed_vt(data, len, out);
    return true;
}

void Pipeline::feed_vt(const uint8_t *data, size_t len, Frame &out) {
    std::vector<TtfxCell> raw((size_t)cols_ * (size_t)rows_);
    int rc = ttfx_raster_cells((TtfxEngine *)ttfx_, raw.data(), cols_, rows_);
    if (rc == 1) {
        backend_ = VtBackend::Libghostty;
        for (size_t i = 0; i < raw.size(); ++i) {
            out.cells[i].ch = (char32_t)(raw[i].ch ? raw[i].ch : U' ');
            out.cells[i].fg_r = raw[i].fg_r;
            out.cells[i].fg_g = raw[i].fg_g;
            out.cells[i].fg_b = raw[i].fg_b;
            out.cells[i].bg_r = raw[i].bg_r;
            out.cells[i].bg_g = raw[i].bg_g;
            out.cells[i].bg_b = raw[i].bg_b;
        }
        return;
    }
    if (rc < 0) {
        const char *err = ttfx_last_error();
        throw std::runtime_error(err ? err : "ttfx_raster_cells failed");
    }
    backend_ = VtBackend::Fallback;
    parse_fallback(data, len, out);
}

void Pipeline::parse_fallback(const uint8_t *data, size_t len, Frame &out) {
    int x = 0, y = 0;
    uint8_t fr = 220, fg = 220, fb = 220;
    uint8_t br = 0, bg = 0, bb = 0;
    size_t i = 0;
    auto put = [&](char32_t ch) {
        if (y >= 0 && y < rows_ && x >= 0 && x < cols_) {
            Cell &c = out.cells[(size_t)y * (size_t)cols_ + (size_t)x];
            c.ch = ch;
            c.fg_r = fr;
            c.fg_g = fg;
            c.fg_b = fb;
            c.bg_r = br;
            c.bg_g = bg;
            c.bg_b = bb;
        }
        ++x;
    };

    while (i < len) {
        unsigned char b = data[i];
        if (b == 0x1b && i + 1 < len && data[i + 1] == '[') {
            i += 2;
            std::vector<int> nums;
            int cur = 0;
            bool have = false;
            while (i < len) {
                unsigned char c = data[i++];
                if (c >= '0' && c <= '9') {
                    cur = cur * 10 + (c - '0');
                    have = true;
                } else if (c == ';') {
                    nums.push_back(have ? cur : 0);
                    cur = 0;
                    have = false;
                } else {
                    if (have)
                        nums.push_back(cur);
                    if (c == 'm') {
                        if (nums.empty())
                            nums.push_back(0);
                        for (size_t k = 0; k < nums.size(); ++k) {
                            int n = nums[k];
                            if (n == 0) {
                                fr = 220;
                                fg = 220;
                                fb = 220;
                                br = bg = bb = 0;
                            } else if (n == 38 && k + 4 < nums.size() && nums[k + 1] == 2) {
                                fr = (uint8_t)nums[k + 2];
                                fg = (uint8_t)nums[k + 3];
                                fb = (uint8_t)nums[k + 4];
                                k += 4;
                            } else if (n == 48 && k + 4 < nums.size() && nums[k + 1] == 2) {
                                br = (uint8_t)nums[k + 2];
                                bg = (uint8_t)nums[k + 3];
                                bb = (uint8_t)nums[k + 4];
                                k += 4;
                            }
                        }
                    } else if (c == 'H' || c == 'f') {
                        int row = nums.size() >= 1 ? nums[0] : 1;
                        int col = nums.size() >= 2 ? nums[1] : 1;
                        y = std::max(0, row - 1);
                        x = std::max(0, col - 1);
                    } else if (c == 'J') {
                        out.cells.assign((size_t)cols_ * (size_t)rows_, Cell{});
                    }
                    break;
                }
            }
            continue;
        }
        if (b == '\n') {
            ++y;
            x = 0;
            ++i;
            continue;
        }
        if (b == '\r') {
            x = 0;
            ++i;
            continue;
        }
        if (b < 0x20) {
            ++i;
            continue;
        }
        char32_t cp = b;
        int need = 0;
        if ((b & 0x80) == 0) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            need = 1;
            cp = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            need = 2;
            cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            need = 3;
            cp = b & 0x07;
        }
        ++i;
        for (int n = 0; n < need && i < len; ++n)
            cp = (cp << 6) | (data[i++] & 0x3F);
        put(cp);
    }
}
