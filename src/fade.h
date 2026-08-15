#pragma once

enum class FadePhase { In, Visible, Out, Done };

class Fade {
public:
    Fade(float in_ms = 400.f, float out_ms = 250.f);

    FadePhase phase() const { return phase_; }
    bool done() const { return phase_ == FadePhase::Done; }
    float alpha() const;

    void tick(float dt_ms);
    void dismiss();

private:
    static float ease(float t);

    FadePhase phase_ = FadePhase::In;
    float elapsed_ms_ = 0.f;
    float in_ms_;
    float out_ms_;
    float out_from_ = 1.f;
};
