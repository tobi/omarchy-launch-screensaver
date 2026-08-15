#include "pipeline.h"
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void expect(bool ok, const char *msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    try {
        Pipeline p("omarchy\n", 40, 12, "print");
        int frames = 0;
        int non_space = 0;
        Frame fr;
        while (frames < 40 && p.tick(fr)) {
            ++frames;
            for (const auto &c : fr.cells) {
                if (c.ch != U' ' && c.ch != 0)
                    ++non_space;
            }
        }
        std::printf("pipeline frames=%d non_space=%d effect=%s backend=%s libghostty=%s\n", frames,
                    non_space, p.effect_name() ? p.effect_name() : "?",
                    p.backend() == VtBackend::Libghostty ? "libghostty" : "fallback",
                    Pipeline::libghostty_linked() ? "linked" : "not-linked");
        expect(frames > 0, "no frames");
        expect(non_space > 0, "no non-space cells");
        expect(p.effect_name() && std::strcmp(p.effect_name(), "print") == 0, "effect name");

        // omarchy-screensaver loops: a finished effect can be rebuilt
        SsaverOptions opt;
        opt.effect = "print";
        opt.random_effect = false;
        opt.frame_rate = 120;
        opt.anchor_canvas = "c";
        opt.anchor_text = "c";
        Pipeline p2("omarchy\n", 40, 12, "print", &opt);
        Frame fr2;
        expect(p2.tick(fr2), "loop rebuild produced a frame");

        try {
            Pipeline bad("omarchy\n", 20, 8, "not-an-effect");
            std::fprintf(stderr, "FAIL: unknown effect should throw\n");
            return 1;
        } catch (const std::exception &) {
        }

        std::puts("test_pipeline: ok");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
