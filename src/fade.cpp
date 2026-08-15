#include "fade.h"

#include <algorithm>

Fade::Fade(float in_ms, float out_ms)
    : in_ms_(std::max(1.f, in_ms)), out_ms_(std::max(1.f, out_ms)) {}

float Fade::ease(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float Fade::alpha() const {
    switch (phase_) {
    case FadePhase::In:
        return ease(elapsed_ms_ / in_ms_);
    case FadePhase::Visible:
        return 1.f;
    case FadePhase::Out:
        return out_from_ * (1.f - ease(elapsed_ms_ / out_ms_));
    case FadePhase::Done:
        return 0.f;
    }
    return 0.f;
}

void Fade::tick(float dt_ms) {
    const float dt = std::max(0.f, dt_ms);
    switch (phase_) {
    case FadePhase::In:
        elapsed_ms_ += dt;
        if (elapsed_ms_ >= in_ms_) {
            elapsed_ms_ = in_ms_;
            phase_ = FadePhase::Visible;
        }
        break;
    case FadePhase::Visible:
        break;
    case FadePhase::Out:
        elapsed_ms_ += dt;
        if (elapsed_ms_ >= out_ms_) {
            elapsed_ms_ = out_ms_;
            phase_ = FadePhase::Done;
        }
        break;
    case FadePhase::Done:
        break;
    }
}

void Fade::dismiss() {
    if (phase_ == FadePhase::Out || phase_ == FadePhase::Done)
        return;
    out_from_ = alpha();
    elapsed_ms_ = 0.f;
    phase_ = FadePhase::Out;
}
