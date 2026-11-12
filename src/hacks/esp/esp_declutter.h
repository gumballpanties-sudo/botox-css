#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Esp label declutter (not botox; TF2 + CSS extra). A "stack" = one rigid group of labels on one
// entity (name row, below-box rows, flag column, health number), recorded with EspQ::Stack. Stacks
// are placed nearest first; each later one slides along its own direction (up / down) until it
// clears every label already placed, or gives up past maxShift and stays put. Stack 0 = fixed text
// (damage numbers, world names, hud text): blocks, never moves. Pure math: tests/esp_declutter_test.cpp.
namespace EspDeclutter
{
    struct Box { float l, t, r, b; };
    struct Stack { int key; bool up; float prio; Box box; float dy; };

    inline bool Hit(const Box& a, const Box& b) { return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b; }

    // union b into the stack with this key (one entry per key, first seen sets up/prio)
    inline void Add(std::vector<Stack>& s, int key, bool up, float prio, const Box& b)
    {
        for (Stack& k : s)
            if (k.key == key)
            {
                k.box = { std::min(k.box.l, b.l), std::min(k.box.t, b.t), std::max(k.box.r, b.r), std::max(k.box.b, b.b) };
                return;
            }
        s.push_back({ key, up, prio, b, 0.f });
    }

    // one row inside its own stack: slide b along the stack direction until it clears every earlier
    // row of that stack. Rows are laid out with the recorded line height; this catches a real
    // font taller than that (flag column / below-box rows printing over each other).
    inline float Settle(const std::vector<Box>& prior, const Box& b, bool up)
    {
        float dy = 0.f;
        for (int it = 0; it < 64; it++)
        {
            const Box m = { b.l, b.t + dy, b.r, b.b + dy };
            const Box* hit = nullptr;
            for (const Box& p : prior)
                if (Hit(m, p)) { hit = &p; break; }
            if (!hit) return dy;
            dy = up ? hit->t - b.b : hit->b - b.t;
        }
        return dy;
    }

    // fills s[i].dy. placed = the fixed boxes on entry; scratch (stacks get appended as placed).
    // ponytail: O(n^2) per frame, fine for <= ~200 labels; grid bucket if it ever shows in a profile.
    inline void Resolve(std::vector<Stack>& s, std::vector<Box>& placed, float maxShift)
    {
        std::vector<int> order(s.size());
        for (int i = 0; i < (int)s.size(); i++) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return s[a].prio < s[b].prio; });
        for (int i : order)
        {
            Stack& k = s[i];
            float dy = 0.f;
            for (int it = 0; it < 64; it++)
            {
                const Box m = { k.box.l, k.box.t + dy, k.box.r, k.box.b + dy };
                const Box* hit = nullptr;
                for (const Box& p : placed)
                    if (Hit(m, p)) { hit = &p; break; }
                if (!hit) break;
                dy = k.up ? hit->t - k.box.b : hit->b - k.box.t;   // just past it, monotonic in the stack's direction
                if (std::fabs(dy) > maxShift || it == 63) { dy = 0.f; break; }
            }
            k.dy = dy;
            placed.push_back({ k.box.l, k.box.t + dy, k.box.r, k.box.b + dy });
        }
    }

    // frame-to-frame ease of one stack's offset; rate = 1/s (exp decay, frame rate independent)
    inline float Ease(float cur, float target, float dt, float rate = 14.f)
    {
        return cur + (target - cur) * (1.f - std::exp(-rate * std::max(dt, 0.f)));
    }
}
