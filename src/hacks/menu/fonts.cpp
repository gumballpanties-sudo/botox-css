#include "fonts.h"
#include "../../globals/globals.h"   // DebugLog
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>

char g_fontIndName[64] = "verdanab";
int  g_fontIndSize     = kFontIndDefSize;
int  g_fontIndFlags    = kFontIndDefFlags;
char g_fontEspName[64] = "Verdana";
int  g_fontEspSize     = kFontEspDefSize;
int  g_fontEspFlags    = kFontEspDefFlags;

static std::atomic<int> s_reload{ 0 };

static std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// botox fonts.cpp font_key: "Rubik-Regular" == "rubik_regular" == "Rubik Regular"
static std::string FontKey(const std::string& s)
{
    std::string k = ToLower(s);
    std::erase_if(k, [](char c) { return c == ' ' || c == '-' || c == '_'; });
    return k;
}

struct FontFile { std::string name, path; };

// botox fonts.cpp scan_directory, verbatim behaviour: ttf/otf/ttc, stem = display name.
static void ScanDir(const std::string& dir, std::vector<FontFile>& out)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;
    for (const auto& e : std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (!e.is_regular_file(ec) || ec || !e.path().has_filename()) continue;
        const std::string ext = ToLower(e.path().extension().string());
        if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
        const std::u8string stem = e.path().stem().u8string(), path = e.path().u8string();
        out.push_back({ std::string((const char*)stem.c_str()), std::string((const char*)path.c_str()) });
    }
}

static std::vector<FontFile>    s_files;
static std::vector<std::string> s_fileNames;
static std::string              s_winFonts;   // "<windir>\Fonts"

// botox n_fonts::on_attach, lazily on first use (first open of the fonts tab, or the first
// atlas build with a custom face) instead of at inject. Embedded-font install skipped: it
// exists for botox's chud/default hud pages, which TF2 does not have.
static void EnsureScanned()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    char win[MAX_PATH] = {};
    GetWindowsDirectoryA(win, MAX_PATH);
    s_winFonts = std::string(win) + "\\Fonts";
    ScanDir(s_winFonts, s_files);

    char user[MAX_PATH] = {};
    if (ExpandEnvironmentStringsA("%LOCALAPPDATA%\\Microsoft\\Windows\\Fonts", user, MAX_PATH))
        ScanDir(user, s_files);

    std::sort(s_files.begin(), s_files.end(), [](const FontFile& a, const FontFile& b) { return ToLower(a.name) < ToLower(b.name); });
    s_files.erase(std::unique(s_files.begin(), s_files.end(),
                              [](const FontFile& a, const FontFile& b) { return ToLower(a.name) == ToLower(b.name); }),
                  s_files.end());
    s_fileNames.reserve(s_files.size());
    for (const auto& f : s_files) s_fileNames.push_back(f.name);
    DebugLog("fonts: %d font files listed", (int)s_files.size());
}

const std::vector<std::string>& Fonts_FileNames()
{
    EnsureScanned();
    return s_fileNames;
}

// botox find_path: exact stem, then family key ("Rubik" -> "Rubik Regular"), then the
// windows fonts folder guess.
std::string Fonts_FindPath(const char* name)
{
    EnsureScanned();
    const std::string want = ToLower(name ? name : "");
    for (const auto& f : s_files)
        if (ToLower(f.name) == want) return f.path;
    const std::string key = FontKey(want);
    for (const std::string& k : { key, key + "regular" })
        for (const auto& f : s_files)
            if (FontKey(f.name) == k) return f.path;
    return s_winFonts + "\\" + (name ? name : "") + ".ttf";
}

// esp page lists GDI families (the vgui face takes a family), but the ImGui esp face needs a file.
// Windows' font registry maps "Rubik SemiBold (TrueType)" -> "Rubik-SemiBold.ttf" (machine fonts,
// relative to the Fonts folder; per-user fonts carry a full path). No exact entry -> botox
// find_path's stem/family-key match, which still catches "Verdana" -> verdana.ttf.
std::string Fonts_FamilyPath(const char* family)
{
    EnsureScanned();
    const std::string want = ToLower(family ? family : "");
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER })
    {
        HKEY key = nullptr;
        if (RegOpenKeyExA(root, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        std::string found;
        char name[256], data[MAX_PATH];
        for (DWORD i = 0;; ++i)
        {
            DWORD nName = sizeof(name), nData = sizeof(data), type = 0;
            if (RegEnumValueA(key, i, name, &nName, nullptr, &type, (BYTE*)data, &nData) != ERROR_SUCCESS) break;
            if (type != REG_SZ || nData == 0) continue;
            data[std::min<DWORD>(nData, sizeof(data) - 1)] = '\0';
            std::string n = ToLower(name);
            if (const size_t p = n.find(" ("); p != std::string::npos) n.resize(p);   // " (TrueType)" / " (OpenType)"
            if (n != want) continue;
            found = strchr(data, '\\') ? std::string(data) : s_winFonts + "\\" + data;
            break;
        }
        RegCloseKey(key);
        if (!found.empty()) return found;
    }
    return Fonts_FindPath(family);
}

static int CALLBACK OnFamily(const LOGFONTA* lf, const TEXTMETRICA*, DWORD, LPARAM p)
{
    if (lf->lfFaceName[0] && lf->lfFaceName[0] != '@')   // '@' = vertical CJK duplicate
        reinterpret_cast<std::vector<std::string>*>(p)->push_back(lf->lfFaceName);
    return 1;
}

const std::vector<std::string>& Fonts_Families()
{
    static std::vector<std::string> s_fams;
    static bool s_done = false;
    if (s_done) return s_fams;
    s_done = true;

    HDC dc = GetDC(nullptr);
    if (dc)
    {
        LOGFONTA lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExA(dc, &lf, OnFamily, (LPARAM)&s_fams, 0);
        ReleaseDC(nullptr, dc);
    }
    std::sort(s_fams.begin(), s_fams.end(), [](const std::string& a, const std::string& b) { return ToLower(a) < ToLower(b); });
    s_fams.erase(std::unique(s_fams.begin(), s_fams.end()), s_fams.end());   // one entry per charset otherwise
    DebugLog("fonts: %d gdi families listed", (int)s_fams.size());
    return s_fams;
}

void Fonts_RequestReload() { s_reload.fetch_add(1); }

void Fonts_ResetIndicator()
{
    strcpy_s(g_fontIndName, kFontIndDefName);
    g_fontIndSize = kFontIndDefSize;
    g_fontIndFlags = kFontIndDefFlags;
}

void Fonts_ResetEsp()
{
    strcpy_s(g_fontEspName, kFontEspDefName);
    g_fontEspSize = kFontEspDefSize;
    g_fontEspFlags = kFontEspDefFlags;
}

uint32_t Fonts_IndSig() { return FontSig(g_fontIndName, g_fontIndSize, g_fontIndFlags, s_reload.load()); }
uint32_t Fonts_EspSig() { return FontSig(g_fontEspName, g_fontEspSize, g_fontEspFlags, s_reload.load()); }

static std::atomic<uint32_t> s_espApplied{ 0 };

bool Fonts_EspDue()
{
    static FontSettle s_settle;
    return s_settle.Due(Fonts_EspSig(), s_espApplied.load(), GetTickCount64());
}

void Fonts_EspApplied(uint32_t sig) { s_espApplied = sig; }
