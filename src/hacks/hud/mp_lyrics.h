#pragma once
// Media player lyrics, the pure half (botox media_player.cpp url_encode / parse_lrc / fetch_lyrics
// json read + misc.cpp draw_lyrics row pick). No winhttp, no winrt: tests/mpaudio_test.cpp.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace mpl
{
    struct Line { double startMs; std::string text; };

    // botox url_encode: unreserved bytes kept, the rest %XX (utf-8 bytes as they are)
    inline std::wstring UrlEncode(const std::string& text)
    {
        std::wstring out;
        for (const unsigned char c : text)
        {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.' || c == '~')
                out += (wchar_t)c;
            else
            {
                wchar_t hex[4]{};
                swprintf(hex, 4, L"%%%02X", c);
                out += hex;
            }
        }
        return out;
    }

    // botox parse_lrc: "[mm:ss.xx][mm:ss.xx] text" -> one line per stamp, sorted (stable)
    inline std::vector<Line> ParseLrc(const std::string& lrc)
    {
        std::vector<Line> out;
        std::istringstream stream(lrc);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<double> stamps;
            size_t at = 0;
            while (at < line.size() && line[at] == '[')
            {
                const size_t close = line.find(']', at);
                if (close == std::string::npos) break;
                int minutes = 0;
                float seconds = 0.f;
                if (sscanf_s(line.substr(at + 1, close - at - 1).c_str(), "%d:%f", &minutes, &seconds) == 2)
                    stamps.push_back((minutes * 60.0 + seconds) * 1000.0);
                at = close + 1;
            }
            std::string text = line.substr(at);
            if (!text.empty() && text.front() == ' ') text.erase(0, 1);
            for (const double s : stamps) out.push_back({ s, text });
        }
        std::stable_sort(out.begin(), out.end(), [](const Line& a, const Line& b) { return a.startMs < b.startMs; });
        return out;
    }

    // botox reads json["syncedLyrics"] with nlohmann; this dll has no json lib, and the lrclib
    // /api/get reply is one flat object, so: find the top-level key, decode its string value
    // (\" \\ \/ \b \f \n \r \t \uXXXX incl. surrogate pairs -> utf-8). null / missing / not a
    // string / broken = false, same as botox's is_string() check.
    inline bool JsonStringField(const std::string& body, const char* key, std::string& out)
    {
        const std::string want = std::string("\"") + key + "\"";
        size_t i = 0;
        int depth = 0;
        bool inStr = false;
        // walk the document so a key text inside some other value never matches
        for (; i < body.size(); ++i)
        {
            const char c = body[i];
            if (inStr)
            {
                if (c == '\\') ++i;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '{' || c == '[') { ++depth; continue; }
            if (c == '}' || c == ']') { --depth; continue; }
            if (c != '"') continue;
            if (depth == 1 && body.compare(i, want.size(), want) == 0)
            {
                size_t j = i + want.size();
                while (j < body.size() && (body[j] == ' ' || body[j] == '\t' || body[j] == '\r' || body[j] == '\n')) ++j;
                if (j < body.size() && body[j] == ':')
                {
                    i = j + 1;
                    break;
                }
            }
            inStr = true;
        }
        if (i >= body.size()) return false;
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) ++i;
        if (i >= body.size() || body[i] != '"') return false;

        const auto putUtf8 = [&](unsigned cp) {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        };
        const auto hex4 = [&](size_t at, unsigned& v) {
            if (at + 4 > body.size()) return false;
            char buf[5] = { body[at], body[at + 1], body[at + 2], body[at + 3], 0 };
            char* end = nullptr;
            v = (unsigned)strtoul(buf, &end, 16);
            return end == buf + 4;
        };

        out.clear();
        for (++i; i < body.size(); ++i)
        {
            const char c = body[i];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (++i >= body.size()) return false;
            switch (body[i])
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned cp = 0;
                if (!hex4(i + 1, cp)) return false;
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < body.size() && body[i + 1] == '\\' && body[i + 2] == 'u')
                {
                    unsigned lo = 0;
                    if (hex4(i + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                putUtf8(cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    // botox draw_lyrics row pick: active = last line started by now, `rows` centred on it,
    // clamped into the song. first = top row index, shown = row count.
    inline void Window(const std::vector<Line>& lines, double nowMs, int rows, int& active, int& first, int& shown)
    {
        const int count = (int)lines.size();
        const auto next = std::upper_bound(lines.begin(), lines.end(), nowMs, [](double t, const Line& l) { return t < l.startMs; });
        active = (std::max)(0, (int)(next - lines.begin()) - 1);
        rows   = std::clamp(rows, 1, 9);
        shown  = (std::min)(rows, count);
        first  = count ? std::clamp(active - (rows - 1) / 2, 0, count - shown) : 0;
    }
}
