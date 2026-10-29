#include "xpad.h"

#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common.h"

namespace hikaye::xpad {

namespace {
struct XState {  // XINPUT_STATE (xinput.h'a bağımlı olmamak için)
    DWORD packet;
    WORD buttons;
    BYTE ltrig, rtrig;
    SHORT lx, ly, rx, ry;
};
WORD g_button = 0x0020;  // XINPUT_GAMEPAD_BACK (varsayılan); HIKAYE_AUTOBACK_BTN: BACK, A, B, START, X, Y
std::atomic<int> g_calls{0}, g_presses{0};
std::atomic<bool> g_logged_first{false};
using GetState = DWORD(WINAPI*)(DWORD, XState*);

GetState g_orig_by_name = nullptr, g_orig_ordinal = nullptr;
std::atomic<bool> g_armed{false};
std::atomic<unsigned long long> g_start{0};
unsigned long long g_delay_ms = 25000ULL;
DWORD g_packet = 0x1000;
constexpr int kPresses = 10;
constexpr unsigned long long kPeriodMs = 2000ULL, kHoldMs = 150ULL;

bool Pressing() {
    if (!g_armed.load()) return false;
    const unsigned long long now = GetTickCount64();
    const unsigned long long t0 = g_start.load();
    if (t0 == 0 || now < t0 + g_delay_ms) return false;
    const unsigned long long dt = now - (t0 + g_delay_ms);
    if (dt >= kPeriodMs * kPresses) return false;  // 10 basış tamamlandı
    return (dt % kPeriodMs) < kHoldMs;
}

DWORD Inject(DWORD idx, XState* st, DWORD r) {
    if (idx == 0 && !g_logged_first.exchange(true)) Log("xpad: oyun XInputGetState'i cagirdi (kablo bagli: " + std::string(r == ERROR_SUCCESS ? "evet" : "hayir") + ")");
    ++g_calls;
    if (idx != 0 || !st || !Pressing()) return r;
    {   // her yeni basışı bir kez kaydet (mod.log)
        static std::atomic<unsigned long long> last_logged{~0ULL};
        const unsigned long long k = (GetTickCount64() - g_start.load() - g_delay_ms) / kPeriodMs;
        if (last_logged.exchange(k) != k) Log("xpad: Geri tusu enjekte edildi #" + std::to_string(++g_presses) + " (oyunun XInput cagri sayisi: " + std::to_string(g_calls.load()) + ")");
    }
    if (r != ERROR_SUCCESS) {  // kumanda yok: bağlı gibi davran
        std::memset(st, 0, sizeof(*st));
        r = ERROR_SUCCESS;
    }
    st->buttons |= g_button;
    st->packet = ++g_packet;
    return r;
}

DWORD WINAPI HookByName(DWORD idx, XState* st) {
    const DWORD r = g_orig_by_name ? g_orig_by_name(idx, st) : ERROR_DEVICE_NOT_CONNECTED;
    return Inject(idx, st, r);
}
DWORD WINAPI HookOrdinal(DWORD idx, XState* st) {  // XInputGetStateEx (100)
    const DWORD r = g_orig_ordinal ? g_orig_ordinal(idx, st) : ERROR_DEVICE_NOT_CONNECTED;
    return Inject(idx, st, r);
}


// ================= DirectInput8 (klavye) =================
// GTA5.exe DINPUT8.dll'den DirectInput8Create'i içe aktarır. Oluşturulan IDirectInput8'in CreateDevice'i, o da klavye aygıtının
// GetDeviceState/GetDeviceData yöntemleri sarmalanır (sadece vtable işaretçileri değiştirilir). Zamanı gelince klavye durumunda seçilen tuş
// (varsayılan Backspace, DIK 0x0E) basılı görünür.

// Her basış penceresinde bir kez günlüğe yaz (çağrılar çok sık)
void LogPressOnce(const std::string& what, std::atomic<unsigned long long>& last) {
    const unsigned long long k = (GetTickCount64() - g_start.load() - g_delay_ms) / kPeriodMs;
    if (last.exchange(k) != k) Log("xpad: " + what + " (basis #" + std::to_string(k + 1) + ")");
}
std::atomic<unsigned long long> g_last_di_state{~0ULL}, g_last_di_data{~0ULL};

BYTE g_dik = 0x0E;
constexpr int kMaxVt = 8;
struct VtEntry {
    void** vt = nullptr;
    void* orig = nullptr;
};
VtEntry g_dev_state[kMaxVt], g_dev_data[kMaxVt], g_create[2];
void* g_keyboards[kMaxVt] = {};
std::atomic<bool> g_log_di_state{false}, g_log_di_data{false}, g_log_di_create{false};
bool g_prev_press = false;

void* FindOrig(VtEntry* arr, int n, void** vt) {
    for (int i = 0; i < n; ++i) {
        if (arr[i].vt == vt) return arr[i].orig;
    }
    return nullptr;
}
bool PatchVt(VtEntry* arr, int n, void** vt, int index, void* hook) {
    for (int i = 0; i < n; ++i) {
        if (arr[i].vt == vt) return true;  // zaten yamalı
    }
    for (int i = 0; i < n; ++i) {
        if (arr[i].vt != nullptr) continue;
        DWORD old = 0;
        if (!VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old)) return false;
        arr[i].vt = vt;
        arr[i].orig = vt[index];
        vt[index] = hook;
        VirtualProtect(&vt[index], sizeof(void*), old, &old);
        return true;
    }
    return false;
}
bool IsKeyboard(void* self) {
    for (void* k : g_keyboards) {
        if (k == self) return true;
    }
    return false;
}

using DevState = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using DevData = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID, LPDWORD, DWORD);
using CreateDev = HRESULT(STDMETHODCALLTYPE*)(void*, const GUID&, void**, IUnknown*);
using DiCreate = HRESULT(WINAPI*)(HINSTANCE, DWORD, const IID&, LPVOID*, LPUNKNOWN);
DiCreate g_orig_di_create = nullptr;

HRESULT STDMETHODCALLTYPE HookGetDeviceState(void* self, DWORD cb, LPVOID data) {
    void** vt = *reinterpret_cast<void***>(self);
    auto orig = reinterpret_cast<DevState>(FindOrig(g_dev_state, kMaxVt, vt));
    const HRESULT hr = orig ? orig(self, cb, data) : E_FAIL;
    if (cb == 256 && data && (IsKeyboard(self) || true)) {  // 256 bayt = klavye durumu
        if (!g_log_di_state.exchange(true)) Log("xpad: oyun DirectInput klavye durumunu okudu (GetDeviceState)");
        if (Pressing()) {
            static_cast<BYTE*>(data)[g_dik] = 0x80;
            LogPressOnce("DirectInput klavye tusu enjekte edildi (GetDeviceState, DIK " + std::to_string(g_dik) + ")", g_last_di_state);
        }
    }
    return hr;
}

struct DiObjData {  // DIDEVICEOBJECTDATA (x64)
    DWORD ofs, data, ts, seq;
    ULONG_PTR app;
};

HRESULT STDMETHODCALLTYPE HookGetDeviceData(void* self, DWORD cbObj, LPVOID rgdod, LPDWORD inout, DWORD flags) {
    void** vt = *reinterpret_cast<void***>(self);
    auto orig = reinterpret_cast<DevData>(FindOrig(g_dev_data, kMaxVt, vt));
    const DWORD capacity = inout ? *inout : 0;
    const HRESULT hr = orig ? orig(self, cbObj, rgdod, inout, flags) : E_FAIL;
    if (IsKeyboard(self) && SUCCEEDED(hr) && rgdod && inout && cbObj >= sizeof(DiObjData)) {  // arabelleklenmiş klavye olayları
        if (!g_log_di_data.exchange(true)) Log("xpad: oyun DirectInput klavye olaylarini okudu (GetDeviceData)");
        const bool now = Pressing();
        if (now != g_prev_press && (flags & 1) == 0 && *inout < capacity) {  // DIGDD_PEEK (1) değilse: yeni basış/bırakış olayı ekle
            auto* ev = reinterpret_cast<DiObjData*>(static_cast<BYTE*>(rgdod) + static_cast<size_t>(*inout) * cbObj);
            ev->ofs = g_dik;
            ev->data = now ? 0x80 : 0;
            ev->ts = static_cast<DWORD>(GetTickCount64());
            ev->seq = 0;
            ev->app = 0;
            ++*inout;
            g_prev_press = now;
            if (now) LogPressOnce("DirectInput klavye tusu enjekte edildi (GetDeviceData, DIK " + std::to_string(g_dik) + ")", g_last_di_data);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(void* self, const GUID& rguid, void** out, IUnknown* outer) {
    void** vt = *reinterpret_cast<void***>(self);
    auto orig = reinterpret_cast<CreateDev>(FindOrig(g_create, 2, vt));
    const HRESULT hr = orig ? orig(self, rguid, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out) {
        // GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000}
        static const GUID kKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
        void** dvt = *reinterpret_cast<void***>(*out);
        PatchVt(g_dev_state, kMaxVt, dvt, 9, reinterpret_cast<void*>(&HookGetDeviceState));
        PatchVt(g_dev_data, kMaxVt, dvt, 10, reinterpret_cast<void*>(&HookGetDeviceData));
        if (std::memcmp(&rguid, &kKeyboard, sizeof(GUID)) == 0) {
            for (void*& k : g_keyboards) {
                if (!k) {
                    k = *out;
                    break;
                }
            }
            if (!g_log_di_create.exchange(true)) Log("xpad: DirectInput klavye aygiti olusturuldu, kancalar takildi");
        }
    }
    return hr;
}

HRESULT WINAPI HookDirectInput8Create(HINSTANCE inst, DWORD ver, const IID& riid, LPVOID* ppv, LPUNKNOWN outer) {
    const HRESULT hr = g_orig_di_create ? g_orig_di_create(inst, ver, riid, ppv, outer) : E_FAIL;
    if (SUCCEEDED(hr) && ppv && *ppv) {
        void** vt = *reinterpret_cast<void***>(*ppv);
        PatchVt(g_create, 2, vt, 3, reinterpret_cast<void*>(&HookCreateDevice));
    }
    return hr;
}

bool IEquals(const char* a, const char* b) { return _stricmp(a, b) == 0; }

// Ana modülün XINPUT*.dll içe aktarmalarını (IAT) bizim sarmalayıcılara çevirir
bool PatchIat() {
    const BYTE* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) return false;
    bool any = false;
    for (const auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        const char* dll = reinterpret_cast<const char*>(base + d->Name);
        const bool is_di = _strnicmp(dll, "DINPUT8", 7) == 0;
        if (_strnicmp(dll, "XINPUT", 6) != 0 && !is_di) continue;
        const auto* orig = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(const_cast<BYTE*>(base) + d->FirstThunk);
        for (; orig->u1.AddressOfData != 0; ++orig, ++thunk) {
            void* repl = nullptr;
            if (is_di) {
                if (!IMAGE_SNAP_BY_ORDINAL64(orig->u1.Ordinal)) {
                    const auto* bn = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
                    if (IEquals(reinterpret_cast<const char*>(bn->Name), "DirectInput8Create")) {
                        g_orig_di_create = reinterpret_cast<DiCreate>(thunk->u1.Function);
                        repl = reinterpret_cast<void*>(&HookDirectInput8Create);
                    }
                }
            } else if (IMAGE_SNAP_BY_ORDINAL64(orig->u1.Ordinal)) {
                if (IMAGE_ORDINAL64(orig->u1.Ordinal) == 100) {
                    g_orig_ordinal = reinterpret_cast<GetState>(thunk->u1.Function);
                    repl = reinterpret_cast<void*>(&HookOrdinal);
                }
            } else {
                const auto* byname = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
                if (IEquals(reinterpret_cast<const char*>(byname->Name), "XInputGetState")) {
                    g_orig_by_name = reinterpret_cast<GetState>(thunk->u1.Function);
                    repl = reinterpret_cast<void*>(&HookByName);
                }
            }
            if (!repl) continue;
            DWORD old = 0;
            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                thunk->u1.Function = reinterpret_cast<ULONGLONG>(repl);
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                any = true;
            }
        }
    }
    return any;
}
}  // namespace

namespace {
unsigned long long g_sig_start = 0, g_sig_delay_ms = 10000ULL;
bool g_sig_on = false, g_sig_done = false;
}  // namespace

// Yükleme ekranı geçişi artık RAGE native'leriyle yapılıyor (main.cpp ScriptLoop):
//   SCRIPT::SHUTDOWN_LOADING_SCREEN() + CONTROLS::_SET_CONTROL_NORMAL(0, 194, 1.0f)
// Windows düzeyi pencere zorlama (SendInput / SetForegroundWindow) kaldırıldı.
std::atomic<bool> g_wk_cancel{false};

void StartWinKey() {
    // Devre dışı — her şey RAGE native (SHUTDOWN_LOADING_SCREEN + _SET_CONTROL_NORMAL).
}

void Install() {
    StartWinKey();
    {   // sinyal sayacı DLL yüklenince başlar
        char am[8] = {};
        if (GetEnvironmentVariableA("HIKAYE_AUTOMENU", am, sizeof(am)) > 0 && am[0] == '1') {
            g_sig_on = true;
            g_sig_start = GetTickCount64();
            char dl[16] = {};
            if (GetEnvironmentVariableA("HIKAYE_AUTOMENU_DELAY", dl, sizeof(dl)) > 0) {
                const int s = std::atoi(dl);
                if (s >= 0 && s < 600) g_sig_delay_ms = static_cast<unsigned long long>(s) * 1000ULL;
            }
            Log("xpad: menu sinyali acik (" + std::to_string(g_sig_delay_ms / 1000) + " sn sonra HikayeTurkRP/save_menu.signal yazilacak)");
        }
    }
    char env[8] = {};
    if (GetEnvironmentVariableA("HIKAYE_AUTOBACK", env, sizeof(env)) == 0 || env[0] != '1') return;
    char dl[16] = {};
    if (GetEnvironmentVariableA("HIKAYE_AUTOBACK_DELAY", dl, sizeof(dl)) > 0) {
        const int s = std::atoi(dl);
        if (s >= 0 && s < 600) g_delay_ms = static_cast<unsigned long long>(s) * 1000ULL;
    }
    char bt[16] = {};
    if (GetEnvironmentVariableA("HIKAYE_AUTOBACK_BTN", bt, sizeof(bt)) > 0) {
        const std::string b = bt;
        if (b == "A") g_button = 0x1000;
        else if (b == "B") g_button = 0x2000;
        else if (b == "X") g_button = 0x4000;
        else if (b == "Y") g_button = 0x8000;
        else if (b == "START") g_button = 0x0010;
        else g_button = 0x0020;
    }
    char kn[16] = {};
    if (GetEnvironmentVariableA("HIKAYE_AUTOBACK_KEY", kn, sizeof(kn)) > 0) {
        const std::string k = kn;
        if (k == "ENTER") g_dik = 0x1C;
        else if (k == "ESC") g_dik = 0x01;
        else if (k == "SPACE") g_dik = 0x39;
        else g_dik = 0x0E;  // BACKSPACE
    }
    const bool ok = PatchIat();
    Log(std::string("xpad: IAT yamasi ") + (ok ? "uygulandi" : "UYGULANAMADI (XInput ice aktarmasi bulunamadi)") + " (tus maskesi " + std::to_string(g_button) + ", gecikme " + std::to_string(g_delay_ms / 1000) + " sn, by-name:" + (g_orig_by_name ? "1" : "0") + ", ordinal:" + (g_orig_ordinal ? "1" : "0") + ", DirectInput:" + (g_orig_di_create ? "1" : "0") + ", DIK " + std::to_string(g_dik) + ")");
}

void TickSignal() {
    // SaveSkipThread (HIKAYE_WINKEY=1) tüm save menüsü geçişini ASI içinden halleder; sinyal dosyası artık gerekmiyor.
    (void)g_sig_on; (void)g_sig_done; (void)g_sig_start; (void)g_sig_delay_ms;
}

void Arm() {
    if (g_armed.load()) return;
    char env[8] = {};
    if (GetEnvironmentVariableA("HIKAYE_AUTOBACK", env, sizeof(env)) == 0 || env[0] != '1') return;
    g_start = GetTickCount64();
    g_armed = true;
}

void Disarm() {
    g_armed = false;
    g_wk_cancel = true;  // hikâye yüklendi: bekleyen Windows düzeyi Backspace vazgeçilir
}

}  // namespace hikaye::xpad
