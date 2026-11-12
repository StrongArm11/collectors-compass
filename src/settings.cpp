// Saving the settings between launches: a small text file on the SD card (in Lichii: user/sdmc), written with the
// system's own file functions (nn::fs, linked by name from the system library when the game starts). Never the
// game's save data.
//
// The file is read the first time a map is on screen (the game has its file system running by then) and written
// when the settings menu closes after a change. A missing or unreadable file just means the defaults.
#include "settings.hpp"

namespace nn {
namespace fs {
struct FileHandle { void* handle; };
struct WriteOption { int flags; };
#define MM_FS __attribute__((weak, visibility("default")))
MM_FS u32 MountSdCard(const char* name);
MM_FS u32 OpenFile(FileHandle* out, const char* path, int mode);
MM_FS u32 ReadFile(FileHandle file, long offset, void* buffer, unsigned long size);
MM_FS u32 WriteFile(FileHandle file, long offset, const void* buffer, unsigned long size, const WriteOption& option);
MM_FS u32 GetFileSize(long* out, FileHandle file);
MM_FS u32 SetFileSize(FileHandle file, long size);
MM_FS void CloseFile(FileHandle file);
MM_FS u32 CreateFile(const char* path, long size);
MM_FS u32 CreateDirectory(const char* path);
MM_FS void SetEnabledAutoAbort(bool enabled);
#undef MM_FS
}  // namespace fs
}  // namespace nn

namespace mm {
namespace {

constexpr const char* kMount = "mmsd";
constexpr const char* kDir = "mmsd:/OoTMinimapMod";
constexpr const char* kPath = "mmsd:/OoTMinimapMod/settings.ini";
constexpr int kOpenRead = 1, kOpenWrite = 2, kOpenAppend = 4;
constexpr int kWriteFlush = 1;

bool s_mounted = false, s_mountFailed = false, s_loaded = false;
SpinLock s_fileLock;

// Settings file keys. The old one-set-for-all keys (only_now, show_collected) are still read, for every kind; the
// per-kind keys after them win.
struct Key { const char* name; int kind; bool KindSettings::*value; bool Settings::*global; };
constexpr int kAll = -1, kNone = -2;
constexpr Key kKeys[] = {
    {"only_now", kAll, &KindSettings::onlyNow, nullptr}, {"show_collected", kAll, &KindSettings::showCollected, nullptr},
    {"skulltulas", kSkulltula, &KindSettings::show, nullptr}, {"skulltulas_only_now", kSkulltula, &KindSettings::onlyNow, nullptr},
    {"skulltulas_show_collected", kSkulltula, &KindSettings::showCollected, nullptr},
    {"hearts", kHeart, &KindSettings::show, nullptr}, {"hearts_only_now", kHeart, &KindSettings::onlyNow, nullptr},
    {"hearts_show_collected", kHeart, &KindSettings::showCollected, nullptr},
    {"big_poes", kPoe, &KindSettings::show, nullptr}, {"big_poes_only_now", kPoe, &KindSettings::onlyNow, nullptr},
    {"big_poes_show_collected", kPoe, &KindSettings::showCollected, nullptr},
    {"silver_rupees", kRupee, &KindSettings::show, nullptr}, {"silver_rupees_only_now", kRupee, &KindSettings::onlyNow, nullptr},
    {"silver_rupees_show_collected", kRupee, &KindSettings::showCollected, nullptr},
    {"hints", kNone, nullptr, &Settings::hints},
};
constexpr int kFirstSaved = 2;   // the old keys aren't written any more

void setKey(Settings& s, const Key& k, bool v) {
    if (k.global != nullptr) { s.*(k.global) = v; return; }
    if (k.kind == kAll) { for (auto& kind : s.kind) kind.*(k.value) = v; return; }
    s.kind[k.kind].*(k.value) = v;
}
bool getKey(const Settings& s, const Key& k) { return k.global != nullptr ? s.*(k.global) : s.kind[k.kind].*(k.value); }

bool available() {
    using namespace nn::fs;
    return &MountSdCard != nullptr && &OpenFile != nullptr && &ReadFile != nullptr && &WriteFile != nullptr &&
           &GetFileSize != nullptr && &SetFileSize != nullptr && &CloseFile != nullptr && &CreateFile != nullptr &&
           &CreateDirectory != nullptr;
}

// The file system stops the game on unexpected errors unless told otherwise; ours should only fail quietly.
struct QuietErrors {
    QuietErrors() { if (&nn::fs::SetEnabledAutoAbort != nullptr) nn::fs::SetEnabledAutoAbort(false); }
    ~QuietErrors() { if (&nn::fs::SetEnabledAutoAbort != nullptr) nn::fs::SetEnabledAutoAbort(true); }
};

bool mount() {
    if (s_mounted) return true;
    if (s_mountFailed) return false;
    if (!available()) { s_mountFailed = true; log("settings: the system's file functions aren't linked; settings won't be saved"); return false; }
    const u32 rc = nn::fs::MountSdCard(kMount);
    if (rc != 0) { s_mountFailed = true; log("settings: SD card not available (%x); settings won't be saved", rc); return false; }
    s_mounted = true;
    return true;
}

bool startsWith(const char* s, const char* prefix) {
    while (*prefix != 0) { if (*s++ != *prefix++) return false; }
    return true;
}

void parse(const char* text, long size, Settings& out, int& found) {
    long i = 0;
    while (i < size) {
        const char* line = text + i;
        long n = 0;
        while (i + n < size && line[n] != '\n') ++n;
        for (const Key& k : kKeys) {
            if (!startsWith(line, k.name)) continue;
            long j = 0;
            while (k.name[j] != 0) ++j;
            while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
            if (j >= n || line[j] != '=') continue;
            ++j;
            while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
            if (j < n && (line[j] == '0' || line[j] == '1')) { setKey(out, k, line[j] == '1'); ++found; }
        }
        i += n + 1;
    }
}

}  // namespace

void settingsLoad() {
    LockGuard guard(s_fileLock);
    if (s_loaded) return;
    s_loaded = true;
    QuietErrors quiet;
    if (!mount()) return;
    nn::fs::FileHandle file{};
    if (nn::fs::OpenFile(&file, kPath, kOpenRead) != 0) { log("settings: no saved settings yet; using the defaults"); return; }
    long size = 0;
    char text[1024];
    u32 rc = nn::fs::GetFileSize(&size, file);
    if (rc == 0 && size > 0) {
        if (size > static_cast<long>(sizeof(text))) size = sizeof(text);
        rc = nn::fs::ReadFile(file, 0, text, static_cast<unsigned long>(size));
    }
    nn::fs::CloseFile(file);
    if (rc != 0) { log("settings: couldn't read the settings file (%x); using the defaults", rc); return; }
    Settings loaded = g_settings;
    int found = 0;
    parse(text, size, loaded, found);
    g_settings = loaded;
    g_settingsVersion.fetch_add(1, std::memory_order_release);
    log("settings: loaded %d from the SD card (Skulltulas %d%d%d, hearts %d%d%d, Poes %d%d%d, rupees %d%d%d [show, only now, "
        "collected], hints %s)", found,
        loaded.kind[0].show, loaded.kind[0].onlyNow, loaded.kind[0].showCollected, loaded.kind[1].show, loaded.kind[1].onlyNow,
        loaded.kind[1].showCollected, loaded.kind[2].show, loaded.kind[2].onlyNow, loaded.kind[2].showCollected,
        loaded.kind[3].show, loaded.kind[3].onlyNow, loaded.kind[3].showCollected, loaded.hints ? "on" : "off");
}

void settingsSave() {
    LockGuard guard(s_fileLock);
    QuietErrors quiet;
    if (!mount()) return;
    char text[1024];
    int n = 0;
    auto put = [&](const char* s) { while (*s != 0 && n < static_cast<int>(sizeof(text)) - 1) text[n++] = *s++; };
    put("# Collector's Compass settings (1 = on, 0 = off). Changed from the menu on the pause map (L + R).\n");
    for (int i = kFirstSaved; i < static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0])); ++i) {
        put(kKeys[i].name);
        put(getKey(g_settings, kKeys[i]) ? "=1\n" : "=0\n");
    }
    nn::fs::CreateDirectory(kDir);   // already there: fine
    nn::fs::FileHandle file{};
    u32 rc = nn::fs::OpenFile(&file, kPath, kOpenWrite | kOpenAppend);
    if (rc != 0) {
        nn::fs::CreateFile(kPath, 0);
        rc = nn::fs::OpenFile(&file, kPath, kOpenWrite | kOpenAppend);
    }
    if (rc != 0) { log("settings: couldn't open the settings file for writing (%x)", rc); return; }
    rc = nn::fs::SetFileSize(file, n);
    const nn::fs::WriteOption flush{kWriteFlush};
    if (rc == 0) rc = nn::fs::WriteFile(file, 0, text, static_cast<unsigned long>(n), flush);
    nn::fs::CloseFile(file);
    if (rc != 0) log("settings: couldn't write the settings file (%x)", rc);
    else log("settings: saved to the SD card (OoTMinimapMod/settings.ini)");
}

}  // namespace mm
