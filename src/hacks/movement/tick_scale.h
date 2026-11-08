#pragma once
// botox csgo-sdk hacks/movement/tick_scale.h, ported 1:1 for assist_predict.h's budget/scale needs.
//
// CSS shims:
//   n_tick::interval/engine_interval (botox: g_global_vars_base->interval_per_tick, forced-128
//   cvar override) -> TickInterval() (prediction.h). CSS has no client-side forced-128 toggle
//   (that was a TF2-only checkbox), so interval() just IS TickInterval() -- no cache needed,
//   TickInterval() is already a single field read.
//   e_search trimmed to the two searches assist_predict.h's callers add (pixeljump/head-bounce
//   assist); other units extend this enum if they want fair-share cmd-clock budgeting too.
#include "prediction.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace n_tick
{
    inline float interval() { return TickInterval(); }
    inline float rate() { return 1.f / interval(); }
    inline float engine_interval() { return TickInterval(); }   // real server tick, same as interval() here
    inline float scale() { return std::clamp(64.f * interval(), 0.25f, 4.f); }

    inline long long qpc_freq()
    {
        static const long long freq = [] {
            LARGE_INTEGER f{};
            QueryPerformanceFrequency(&f);
            return f.QuadPart > 0ll ? f.QuadPart : 1ll;
        }();
        return freq;
    }
    inline long long qpc_now() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart; }

    constexpr double k_cmd_total_s = 0.6 / 128.0;

    // search ids an assist_predict.h caller can opt into for cross-search fair-share budgeting
    enum e_search : int { search_none = -1, search_pja = 0, search_ba, search_psa };

    inline unsigned s_hungry_prev = 0u, s_hungry_now = 0u, s_started_now = 0u;
    inline unsigned s_fair_cuts = 0u;
    inline long long s_cmd_start = 0ll, s_cmd_span = 0ll;
    inline bool s_cmd_clock_live = false;

    // call once per cmd to get fair-share budgeting between search ids; harmless if never called
    // (c_sim_budget::start then just applies its own fraction*interval cap, no fairness)
    inline void begin_cmd_clock()
    {
        s_cmd_start = qpc_now();
        s_cmd_span  = static_cast<long long>(static_cast<double>(qpc_freq()) * k_cmd_total_s);
        s_cmd_clock_live = true;
        s_hungry_prev    = s_hungry_now;
        s_hungry_now = s_started_now = 0u;
    }
    inline long long cmd_clock_left()
    {
        if (!s_cmd_clock_live) return (1ll << 62);
        const long long left = s_cmd_span - (qpc_now() - s_cmd_start);
        return left > 0ll ? left : 0ll;
    }

    // ticks_at_64 scaled to the server's real tick rate (botox: 48 ticks at 64 -> fewer/more here)
    inline int ticks(int ticks_at_64)
    {
        const float s = scale();
        if (s <= 0.f) return ticks_at_64;
        const int out = static_cast<int>(std::round(static_cast<float>(ticks_at_64) / s));
        return out < 1 ? 1 : out;
    }

    class c_sim_budget
    {
        long long m_freq = 1ll, m_start = 0ll, m_span = 0ll;
        e_search m_id = search_none;

        static long long now() { return qpc_now(); }

        bool hungry() const
        {
            if (m_id != search_none && s_cmd_clock_live)
                s_hungry_now |= 1u << m_id;
            return true;
        }

    public:
        void start(const float fraction, const float interval_override = 0.f, const e_search id = search_none)
        {
            m_freq  = qpc_freq();
            m_start = now();
            m_id    = id;
            const float ipt = interval_override > 0.f ? interval_override : interval();
            m_span  = static_cast<long long>(static_cast<double>(m_freq) * static_cast<double>(ipt) *
                                             static_cast<double>(std::clamp(fraction, 0.02f, 0.9f)));

            long long left = cmd_clock_left();
            if (s_cmd_clock_live) {
                if (id != search_none) s_started_now |= 1u << id;
                if (const int owed = std::popcount(s_hungry_prev & ~s_started_now); owed > 0 && m_span > left / (1 + owed)) {
                    left /= 1 + owed;
                    s_fair_cuts++;
                }
            }
            if (m_span > left) m_span = left;
        }

        long long used_us() const { return (now() - m_start) * 1000000ll / m_freq; }
        long long left_us() const { return std::max(0ll, m_span - (now() - m_start)) * 1000000ll / m_freq; }

        bool expired() const { return now() - m_start >= m_span && hungry(); }
        bool cannot_fit(const long long want_us) const { return (now() - m_start) + want_us * m_freq / 1000000ll >= m_span && hungry(); }
    };
}
