#include "fade.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void expect(bool ok, const char *msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    {
        Fade f(400.f, 250.f);
        expect(std::fabs(f.alpha()) < 1e-6f, "in starts at 0");
        f.tick(400.f);
        expect(f.phase() == FadePhase::Visible, "in reaches Visible");
        expect(std::fabs(f.alpha() - 1.f) < 1e-5f, "in reaches 1");
    }
    {
        Fade f(10.f, 250.f);
        f.tick(10.f);
        f.dismiss();
        f.tick(250.f);
        expect(f.phase() == FadePhase::Done, "out reaches Done");
        expect(std::fabs(f.alpha()) < 1e-5f, "out reaches 0");
    }
    {
        Fade f(400.f, 250.f);
        float prev = f.alpha();
        for (int i = 0; i < 40; ++i) {
            f.tick(10.f);
            float a = f.alpha();
            expect(a + 1e-6f >= prev, "in monotonic");
            prev = a;
        }
        expect(std::fabs(f.alpha() - 1.f) < 1e-5f, "in end 1");
    }
    {
        Fade f(1.f, 250.f);
        f.tick(1.f);
        f.dismiss();
        float prev = f.alpha();
        expect(std::fabs(prev - 1.f) < 1e-5f, "out starts 1");
        for (int i = 0; i < 25; ++i) {
            f.tick(10.f);
            float a = f.alpha();
            expect(a <= prev + 1e-6f, "out monotonic");
            prev = a;
        }
        expect(std::fabs(f.alpha()) < 1e-5f, "out end 0");
        expect(f.phase() == FadePhase::Done, "out done");
    }
    {
        Fade f(400.f, 250.f);
        f.tick(200.f);
        expect(f.phase() == FadePhase::In, "still in");
        float mid = f.alpha();
        expect(mid > 0.f && mid < 1.f, "mid-in alpha");
        f.dismiss();
        expect(f.phase() == FadePhase::Out, "mid-in dismiss");
        expect(std::fabs(f.alpha() - mid) < 1e-5f, "out starts at current alpha");
    }
    std::puts("test_fade: ok");
    return 0;
}
