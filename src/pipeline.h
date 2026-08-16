#pragma once

#include "options.h"
#include "ttfx_c.h"
#include <string>
#include <vector>

using Cell = TtfxCell;

struct Frame {
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells; // row-major, top-left first
};

class Pipeline {
public:
    Pipeline(std::string input, int cols, int rows, std::string effect,
             const SsaverOptions *opt = nullptr);
    ~Pipeline();

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    bool tick(Frame &out);
    const char *effect_name() const;
    int cols() const { return cols_; }
    int rows() const { return rows_; }



    void *ttfx_ = nullptr;
    int cols_;
    int rows_;
};
