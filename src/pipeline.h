#pragma once

#include "options.h"

#include <cstdint>
#include <string>
#include <vector>

struct Cell {
    char32_t ch = U' ';
    uint8_t fg_r = 220, fg_g = 220, fg_b = 220;
    uint8_t bg_r = 0, bg_g = 0, bg_b = 0;
};

struct Frame {
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells; // row-major, top-left first
    std::string vt;          // raw ttfx VT bytes
};

enum class VtBackend { Libghostty, Fallback };

class Pipeline {
public:
    Pipeline(std::string input, int cols, int rows, std::string effect,
             const SsaverOptions *opt = nullptr);
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    bool tick(Frame &out);
    const char *effect_name() const;
    VtBackend backend() const { return backend_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

    static bool libghostty_linked();

private:
    void feed_vt(const uint8_t *data, size_t len, Frame &out);
    void parse_fallback(const uint8_t *data, size_t len, Frame &out);

    void *ttfx_ = nullptr;
    void *term_ = nullptr; // ghostty_host handle when HAVE_LIBGHOSTTY
    int cols_;
    int rows_;
    VtBackend backend_ = VtBackend::Fallback;
};
