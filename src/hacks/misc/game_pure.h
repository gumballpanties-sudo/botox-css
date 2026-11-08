#pragma once
// Engine-free halves of the misc > game rows (botox chat_extras.cpp / bot_names.cpp /
// misc.cpp split_cvars), so tests/gamepure_test.cpp can run them without the game.
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace n_game
{
// ==== bf_read, the engine's user message buffer ======================================
// Bits LSB first inside each byte (tier1 bitbuf.cpp). SayText2 is all WRITE_BYTE /
// WRITE_STRING, so a string is 8-bit chars up to the 0 terminator.
struct BitReader
{
    const uint8_t* data = nullptr;
    int bits = 0, cur = 0;
    bool over = false;

    int Bit()
    {
        if (cur >= bits) { over = true; return 0; }
        const int b = (data[cur >> 3] >> (cur & 7)) & 1;
        ++cur;
        return b;
    }
    int Byte()
    {
        int v = 0;
        for (int i = 0; i < 8; ++i) v |= Bit() << i;
        return v;
    }
    std::string String(size_t max = 255)
    {
        std::string s;
        while (!over)
        {
            const int c = Byte();
            if (over || c == 0) break;
            if (s.size() < max) s += (char)c;
        }
        return s;
    }
};

// TF2 SayText2: byte ent, byte wants_to_chat, string msg_name, up to 4 params
// (UTIL_SayText2Filter, server/util.cpp). SayText (TF2 type 3): byte ent, string text,
// byte wants_to_chat (UTIL_SayTextFilter).
struct SayMsg
{
    int ent = 0;
    bool chat = false;
    std::string text, msgName;
    std::vector<std::string> params;
};

inline bool ParseSayText2(const uint8_t* data, int bytes, SayMsg& out)
{
    BitReader r{ data, bytes * 8, 0, false };
    out.ent  = r.Byte();
    out.chat = r.Byte() != 0;
    out.msgName = r.String();
    for (int i = 0; i < 4 && r.cur < r.bits; ++i)
    {
        std::string p = r.String();
        if (r.over) break;
        out.params.push_back(std::move(p));
    }
    return !r.over || !out.msgName.empty();
}

inline bool ParseSayText(const uint8_t* data, int bytes, SayMsg& out)
{
    BitReader r{ data, bytes * 8, 0, false };
    out.ent  = r.Byte();
    out.text = r.String();
    out.chat = r.Byte() != 0;
    return !out.text.empty();
}

// ==== chat text helpers =============================================================
inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Control bytes out, outer spaces trimmed. TF2 colours are \x07RRGGBB and \x08RRGGBBAA, so
// the hex that follows goes too (botox only had CS:GO's single-byte colours).
inline std::string StripControls(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        const unsigned char c = (unsigned char)in[i];
        if (c == 0x07) { i += 6; continue; }
        if (c == 0x08) { i += 8; continue; }
        if (c >= 0x20) out += (char)c;
    }
    const size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, out.find_last_not_of(' ') - first + 1);
}

inline bool HasLetter(const std::string& t)
{
    return std::any_of(t.begin(), t.end(), [](char c) { return (unsigned char)c >= 0x80 || std::isalpha((unsigned char)c); });
}

// gtx guesses 1-2 latin words wildly ("ez" -> basque "no"), so those need a sure detect
inline bool ShortLatin(const std::string& t)
{
    if (std::any_of(t.begin(), t.end(), [](char c) { return (unsigned char)c >= 0x80; })) return false;
    int words = 0;
    for (size_t i = 0; i < t.size(); ++i) words += t[i] != ' ' && (i == 0 || t[i - 1] == ' ');
    return words <= 2;
}

inline std::string BaseCode(const std::string& code) { return Lower(code.substr(0, code.find('-'))); }

inline std::string UrlEncode(const std::string& in)
{
    std::string out;
    char hex[4];
    for (const unsigned char c : in)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { snprintf(hex, sizeof(hex), "%%%02X", c); out += hex; }
    }
    return out;
}

// community chat plugins send the whole line in one string: "*DEAD* [VIP] name : text"
inline bool SplitLine(const std::string& line, const std::string& playerName, std::string& name, std::string& text)
{
    size_t at = std::string::npos;
    if (!playerName.empty())
    {
        name = StripControls(playerName);
        at   = name.empty() ? std::string::npos : line.find(name);
        if (at != std::string::npos) at += name.size();
    }
    if (at == std::string::npos)
    {
        at = line.find(": ");
        if (at == std::string::npos) return false;
        if (name.empty()) name = StripControls(line.substr(0, at));
    }
    text = line.substr(at);
    text.erase(0, text.find_first_not_of(" :"));
    return !text.empty();
}

// botox on_chat_message's pick, TF2 message types: 3 SayText (whole line), 4 SayText2
// ("TF_Chat_*" token = params name/text; other "#" tokens = name change etc, skipped;
// a raw msg_name is a plugin's own format).
inline bool PickChatLine(int type, const SayMsg& say, const std::string& playerName, std::string& name, std::string& text)
{
    if (type == 3) return SplitLine(StripControls(say.text), playerName, name, text);
    const bool token = !say.msgName.empty() && say.msgName[0] == '#';
    if (say.msgName.find("Chat") != std::string::npos ||
        (!token && say.params.size() >= 2 && !StripControls(say.params[1]).empty()))
    {
        if (say.params.size() < 2) return false;
        name = StripControls(say.params[0]);
        text = StripControls(say.params[1]);
        return !text.empty();
    }
    if (!token) return SplitLine(StripControls(say.msgName), playerName, name, text);
    return false;
}

// ChatPrintf takes the line as its FORMAT: no '%' may reach it (aim.h Aim_ChatPrint)
inline std::string NoPercent(std::string s)
{
    s.erase(std::remove(s.begin(), s.end(), '%'), s.end());
    return s;
}

// ==== minimal JSON (the gtx reply only) ===============================================
struct JVal
{
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0.0;
    std::string s;
    std::vector<JVal> a;   // array items, or object values (keys dropped: gtx never needs them)
};

struct JParser
{
    const char* p;
    const char* e;
    int depth = 0;

    void Ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }

    static void Utf8(std::string& o, unsigned cp)
    {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(unsigned& v)
    {
        if (e - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool Str(std::string& o)
    {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"')
        {
            if (*p != '\\') { o += *p++; continue; }
            if (++p >= e) return false;
            const char c = *p++;
            switch (c)
            {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {
                unsigned cp;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u')
                {
                    p += 2;
                    unsigned lo;
                    if (!Hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                Utf8(o, cp);
                break;
            }
            default: o += c;   // \" \\ \/
            }
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool Val(JVal& v)
    {
        if (++depth > 32) return false;
        Ws();
        if (p >= e) return false;
        bool ok = true;
        if (*p == '"') { v.t = JVal::Str; ok = Str(v.s); }
        else if (*p == '[' || *p == '{')
        {
            const bool obj = *p == '{';
            const char close = obj ? '}' : ']';
            v.t = obj ? JVal::Obj : JVal::Arr;
            ++p; Ws();
            if (p < e && *p == close) ++p;
            else for (;;)
            {
                if (obj)
                {
                    std::string key;
                    Ws();
                    if (!Str(key)) return false;
                    Ws();
                    if (p >= e || *p != ':') return false;
                    ++p;
                }
                v.a.emplace_back();
                if (!Val(v.a.back())) return false;
                Ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == close) { ++p; break; }
                return false;
            }
        }
        else if (e - p >= 4 && !strncmp(p, "null", 4)) { v.t = JVal::Null; p += 4; }
        else if (e - p >= 4 && !strncmp(p, "true", 4)) { v.t = JVal::Bool; v.n = 1; p += 4; }
        else if (e - p >= 5 && !strncmp(p, "false", 5)) { v.t = JVal::Bool; p += 5; }
        else
        {
            char* end = nullptr;
            const std::string num(p, (std::min)((size_t)(e - p), (size_t)64));
            v.n = strtod(num.c_str(), &end);
            if (end == num.c_str()) return false;
            v.t = JVal::Num;
            p += end - num.c_str();
        }
        --depth;
        return ok;
    }
};

inline bool ParseJson(const std::string& body, JVal& out)
{
    out = JVal{};   // a reused value must not keep a failed parse's half-built items
    JParser j{ body.data(), body.data() + body.size() };
    return j.Val(out);
}

// translate.googleapis.com gtx: [[["out","in",...],...],null,"src",null,null,null,conf,...]
// true = a translation worth printing (botox translate()'s accept rule, 1:1).
inline bool ParseGtx(const std::string& body, const std::string& original, const std::string& target,
                     std::string& out, std::string& source)
{
    JVal root;
    if (!ParseJson(body, root) || root.t != JVal::Arr || root.a.empty() || root.a[0].t != JVal::Arr) return false;
    for (const JVal& seg : root.a[0].a)
        if (seg.t == JVal::Arr && !seg.a.empty() && seg.a[0].t == JVal::Str) out += seg.a[0].s;
    source = root.a.size() > 2 && root.a[2].t == JVal::Str ? root.a[2].s : std::string();
    const double conf = root.a.size() > 6 && root.a[6].t == JVal::Num ? root.a[6].n : 0.0;
    return !out.empty() && BaseCode(source) != BaseCode(target) && Lower(out) != Lower(original) &&
           !(conf < 0.7 && ShortLatin(original));
}

// ==== steamcommunity profile xml (bot names "steam names") ===========================
// "" = no usable profile (private / no name / default avatar, botox fetch_worker's skips)
inline std::string SteamXmlName(const std::string& body, size_t maxLen)
{
    static const char kTag[] = "<steamID><![CDATA[";
    const size_t begin = body.find(kTag);
    if (begin == std::string::npos) return {};
    const size_t at  = begin + sizeof(kTag) - 1;
    const size_t end = body.find("]]>", at);
    if (end == std::string::npos || end == at) return {};
    static const char kDefaultAvatar[] = "fef49e7fa7e1997310d705b2a6158ff8dc1cdfeb";
    const size_t icon = body.find("<avatarIcon>");
    if (icon == std::string::npos || body.find(kDefaultAvatar, icon) < body.find("</avatarIcon>", icon)) return {};
    return body.substr(at, (std::min)(end - at, maxLen));
}

// ==== cvar list (botox n_misc::split_cvars) ==========================================
inline std::vector<std::string> SplitCvars(const std::string& value, char sep)
{
    std::vector<std::string> out;
    std::string part;
    for (const char c : value + sep)
    {
        if (c != sep && c != '\r' && c != '\n') { part += c; continue; }
        const size_t first = part.find_first_not_of(" \t");
        if (first != std::string::npos) out.push_back(part.substr(first, part.find_last_not_of(" \t") - first + 1));
        part.clear();
    }
    return out;
}
} // namespace n_game
