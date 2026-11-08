#pragma once
// botox misc.cpp parse_clantag_frames / join_clantag_frames / update_clantag animation, pure
// (no engine): game_extras.cpp Clantag_Frame sends what Step returns. tests/clantag_test.cpp.
// Only diff from botox: frames split on '|' (cfg STR rows are one line), botox splits on '\n'.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace clantag
{
struct Frame { std::string text; float hold = 1.f; };

// botox g_utilities.truncate_utf8: <= max bytes, never splits a utf8 char
inline std::string TruncateUtf8(const std::string& s, size_t max)
{
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
    return s.substr(0, n);
}

inline std::vector<Frame> ParseFrames(const std::string& value, float fallbackHold)
{
    std::vector<Frame> frames;
    if (value.empty()) return frames;

    std::string line;
    for (const char c : value + '|')
    {
        if (c == '\r' || c == '\n') continue;
        if (c != '|') { line += c; continue; }

        Frame frame;
        frame.hold = fallbackHold;
        const size_t tab = line.rfind('\t');
        if (tab != std::string::npos)
        {
            frame.hold = std::strtof(line.c_str() + tab + 1, nullptr);
            line.resize(tab);
        }
        // hand-edited configs: 0 would spam the server, nan would never advance
        if (std::isnan(frame.hold)) frame.hold = 1.f;
        frame.hold = std::clamp(frame.hold, 0.1f, 10.f);
        if (line.length() > 24) line.resize(24);

        frame.text = line;
        frames.push_back(frame);
        line.clear();
    }
    return frames;
}

inline std::string JoinFrames(const std::vector<Frame>& frames)
{
    std::string joined;
    for (size_t i = 0; i < frames.size(); i++)
    {
        char hold[16];
        snprintf(hold, sizeof(hold), "\t%.2f", frames[i].hold);
        if (i) joined += '|';
        joined += frames[i].text + hold;
    }
    return joined;
}

struct State
{
    int stage = 0; double lastStep = 0.0; std::string lastBase;
    std::string songKey; int songStage = 0; double songStep = 0.0;
};

struct Settings
{
    int anim = 0; std::string text; float speed = 1.f, hold = 2.f; std::string frames;
    std::string title, artist;   // now playing: title empty = nothing plays
};

// The tag botox would send at `time` (seconds): trailing space added, "" = clear.
inline std::string Step(State& st, const Settings& s, double time)
{
    std::string base = s.text;
    if (base.empty()) base = "botox";
    else if (base.length() > 24) base.resize(24);

    if (base != st.lastBase) { st.stage = 0; st.lastStep = 0.0; st.lastBase = base; }

    const int anim = s.anim;
    float speed = s.speed;
    if (!(speed >= 0.1f)) speed = 0.1f;

    std::string tag;
    const std::string songTitle = anim == 4 ? TruncateUtf8(s.title, 96) : std::string{};

    if (anim == 4 && !songTitle.empty())
    {
        // title, scroll left into artist, loop. each part waits 1 s once its end is shown.
        // server keeps 15 bytes (MAX_CLAN_TAG_LENGTH 16), 1 goes to the trailing space below
        constexpr int kWindowBytes = 14;
        const std::string artist = TruncateUtf8(s.artist, 96);

        std::vector<std::string> chars;
        std::vector<std::pair<int, int>> parts;
        const auto push = [&](const std::string& text) {
            for (size_t i = 0; i < text.size();)
            {
                size_t n = 1;
                while (i + n < text.size() && (static_cast<unsigned char>(text[i + n]) & 0xC0) == 0x80) n++;
                chars.push_back(static_cast<unsigned char>(text[i]) < 0x20 ? " " : text.substr(i, n));
                i += n;
            }
        };
        const auto pushPart = [&](const std::string& text, const char* gap) {
            const int start = static_cast<int>(chars.size());
            push(text);
            parts.emplace_back(start, static_cast<int>(chars.size()));
            push(gap);
        };

        pushPart(songTitle, artist.empty() ? "   " : " - ");
        if (!artist.empty()) pushPart(artist, " - ");

        // window no wider than the longest part: short title + artist never both on screen
        const int count = static_cast<int>(chars.size());
        int widest = 0;
        for (const auto& [start, end] : parts) widest = (std::max)(widest, end - start);

        const auto window = [&](const int at) {
            int length = 0, bytes = 0;
            while (length < widest && bytes + static_cast<int>(chars[(at + length) % count].size()) <= kWindowBytes)
                bytes += static_cast<int>(chars[(at + length++) % count].size());
            return length;
        };

        const std::string key = songTitle + '\n' + artist;
        if (key != st.songKey) { st.songKey = key; st.songStage = 0; st.songStep = time; }

        if (artist.empty() && window(0) >= parts[0].second)
            tag = songTitle;
        else
        {
            st.songStage %= count;
            bool pause = false;
            for (const auto& [start, end] : parts)
            {
                int at = start;
                while (at + window(at) < end) at++;
                pause |= at == st.songStage;
            }
            if (time - st.songStep >= speed + (pause ? 1.0 : 0.0))
            {
                st.songStep = time;
                st.songStage = (st.songStage + 1) % count;
            }
            const int length = window(st.songStage);
            for (int i = 0; i < length; i++) tag += chars[(st.songStage + i) % count];
        }
    }
    else if (anim == 4)
        tag = base;
    else if (anim == 3)
    {
        auto frames = ParseFrames(s.frames, speed);
        frames.erase(std::remove_if(frames.begin(), frames.end(), [](const Frame& f) { return f.text.empty(); }), frames.end());
        if (frames.empty())
            tag = base;
        else
        {
            const int frameCount = static_cast<int>(frames.size());
            st.stage %= frameCount;
            if (time - st.lastStep >= frames[st.stage].hold)
            {
                st.lastStep = time;
                st.stage = (st.stage + 1) % frameCount;
            }
            tag = frames[st.stage].text;
        }
    }
    else if (anim == 2)
    {
        const int loopLength = static_cast<int>(base.length());
        st.stage %= loopLength;
        if (time - st.lastStep >= speed)
        {
            st.lastStep = time;
            st.stage = (st.stage + 1) % loopLength;
        }
        tag = base.substr(st.stage) + base.substr(0, st.stage);
    }
    else if (anim == 1)
    {
        const int textLength     = static_cast<int>(base.length());
        const int typingEndStage = textLength - 1;
        const int pauseStage     = typingEndStage + 1;
        const int deleteEndStage = pauseStage + textLength;
        const int emptyStage     = deleteEndStage + 1;
        const int totalStages    = emptyStage + 1;

        float hold = s.hold;
        if (hold < speed) hold = speed;

        const float delay = st.stage == pauseStage ? hold : speed;
        if (time - st.lastStep >= delay)
        {
            st.lastStep = time;
            if (++st.stage >= totalStages) st.stage = 0;
        }

        if (st.stage <= typingEndStage)
            tag = base.substr(0, st.stage + 1);
        else if (st.stage == pauseStage)
            tag = base;
        else if (st.stage <= deleteEndStage)
        {
            const int deleted = st.stage - pauseStage;
            if (deleted < textLength) tag = base.substr(deleted);
        }
    }
    else
        tag = base;

    if (!tag.empty()) tag += ' ';
    return tag;
}
} // namespace clantag
