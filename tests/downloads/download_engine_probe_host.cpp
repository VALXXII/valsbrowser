// tests/downloads: what the engine itself does with a download it does not let finish (held by SmartScreen, blocked,
// insecure, antivirus), measured on a WebView2 host whose window is never shown (no ShowWindow, tool window, off
// every monitor, never activated). Driven by download_engine_probe.py on local pages only, one fresh temporary
// profile per run. Nothing appears on screen. The browser's own code is not involved: these are engine facts.
//
//   download_engine_probe_host.exe --url <page> --file <file url> --udf <profile> --dir <download folder>
//        [--trigger navigate|script|gesture] [--then none|cancel|link-cancel] [--reputation on|off] [--hold-ms N] [--seconds N]
//
// Prints FACT lines: every DownloadStarting / SaveFileSecurityCheckStarting / StateChanged (state, interrupt reason,
// bytes, ResultFilePath), every change in the download folder's listing (name and size: where the bytes really
// are), « held » once every byte is in and the operation stays in progress for --hold-ms, then what --then does:
//   cancel       Cancel(): the state that follows and what is left in the folder;
//   link-cancel  a hard link to the file that holds the bytes, then Cancel(): does the link survive, does the
//                engine's own file go, and when.
// --reputation off (ICoreWebView2Settings8, this probe's profile only) tells a SmartScreen hold from any other.
// The process is per-monitor DPI aware (WebView2 158 refuses a controller in a process that is not: 0x8007139F).
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WebView2.h>
#include <shobjidl.h>
#include <cstdio>
#include <functional>
#include <algorithm>
#include <map>
#include <string>

namespace {
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2NavigationCompletedEventHandler,"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,"{5c4889f0-5ef6-4c5a-952c-d8f1b92d0574}")
GUID_TEXT(ICoreWebView2DownloadStartingEventHandler,"{efedc989-c396-41ca-83f7-07f845a55724}")
GUID_TEXT(ICoreWebView2BytesReceivedChangedEventHandler,"{828e8ab6-d94c-4264-9cef-5217170d6251}")
GUID_TEXT(ICoreWebView2StateChangedEventHandler,"{81336594-7ede-4ba9-bf71-acf0a95b58dd}")
GUID_TEXT(ICoreWebView2SaveFileSecurityCheckStartingEventHandler,"{7899576c-19e3-57c8-b7d1-55808292de57}")

template<class I, class... A> class Handler final : public I {
    LONG refs = 1; IID iid{}; std::function<HRESULT(A...)> fn;
public:
    explicit Handler(std::function<HRESULT(A...)> f) : fn(std::move(f)) { CLSIDFromString(GuidText<I>(), &iid); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == iid) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};
template<class I, class... A, class F> I* Callback(F f) { return new Handler<I, A...>(std::move(f)); }
IID Iid(const wchar_t* s) { IID i{}; CLSIDFromString(s, &i); return i; }
const IID IID_View4 = Iid(L"{20d02d59-6df2-42dc-bd06-f98a694b1302}");     // ICoreWebView2_4 (DownloadStarting)
const IID IID_View26 = Iid(L"{806268b8-f897-5685-88e5-c45fca0b1a48}");    // ICoreWebView2_26 (SaveFileSecurityCheckStarting)
const IID IID_Settings8 = Iid(L"{9e6b0e8f-86ad-4e81-8147-a9b5edb68650}"); // ICoreWebView2Settings8 (IsReputationCheckingRequired)

std::wstring url, fileUrl, udf, dir, trigger = L"navigate", then = L"none", reputation = L"on", aes = L"off";
int seconds = 60, holdMs = 4000, restoreDelay = 0, otherHandleMs = 0;
std::wstring otherShare = L"all";   // all: read, write and delete allowed; read: a reader that does not allow deletion
// --then observe: the mechanism of a « lingering » engine file (still there after its cancel), see Observe().
int dropLinkMs = 0, touchMs = 0, observeMs = 60000; std::wstring linkHidden = L"on", releaseOp = L"keep";
// --act-on zone: act only once the held file carries its zone mark (the engine's own pass is over), at most --cap-ms
// after the last byte; --restore-on-gone on: each removal of the engine's name is counted and the file given back at once.
std::wstring actOn = L"hold", restoreOnGone = L"off"; int capMs = 20000, removals = 0;
ICoreWebView2Controller* controller = nullptr;
ICoreWebView2* view = nullptr;
ICoreWebView2DownloadOperation* op = nullptr;
bool finished = false, fileTriggered = false, held = false, acted = false, terminal = false;
ULONGLONG t0 = 0, fullAt = 0;
std::wstring lastListing, linkPath;

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
std::wstring Ms() { return std::to_wstring((long long)(GetTickCount64() - t0)); }
void Say(const std::wstring& s) { printf("%s\n", Utf8(L"[" + Ms() + L"] " + s).c_str()); fflush(stdout); }
void Finish() { if (!finished) { finished = true; Say(L"FACT end"); PostQuitMessage(0); } }
std::map<UINT_PTR, std::function<void()>> timers;
void After(UINT ms, std::function<void()> f) {
    const UINT_PTR id = SetTimer(nullptr, 0, ms, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); auto it = timers.find(id); if (it == timers.end()) return; auto f = std::move(it->second); timers.erase(it); f(); });
    timers[id] = std::move(f);
}
std::wstring Str(LPWSTR s) { std::wstring r = s ? s : L""; if (s) CoTaskMemFree(s); return r; }
bool Exists(const std::wstring& p) { return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
long long SizeOf(const std::wstring& p) { WIN32_FILE_ATTRIBUTE_DATA d{}; return GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d) ? ((long long)d.nFileSizeHigh << 32 | d.nFileSizeLow) : -1; }
// Every file of the download folder (hidden ones too), name=size, plus the zone mark when there is one.
std::wstring Listing() {
    std::wstring out; WIN32_FIND_DATAW f{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return L"(none)";
    do {
        const std::wstring n = f.cFileName; if (n == L"." || n == L"..") continue;
        out += n + L"=" + std::to_wstring(((long long)f.nFileSizeHigh << 32) | f.nFileSizeLow);
        if (f.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) out += L"(hidden)";
        if (Exists(dir + L"\\" + n + L":Zone.Identifier")) out += L"(zone)";
        out += L" ";
    } while (FindNextFileW(h, &f));
    FindClose(h); return out.empty() ? L"(empty)" : out;
}
void Files(const wchar_t* why) { const auto l = Listing(); if (l != lastListing) { lastListing = l; Say(std::wstring(L"FACT files (") + why + L") " + l); } }
// The file that holds the bytes: the final path if present, else the newest .crdownload of the folder.
std::wstring BytesFile(const std::wstring& final) {
    if (Exists(final)) return final;
    std::wstring best; FILETIME newest{}; WIN32_FIND_DATAW f{}; HANDLE h = FindFirstFileW((dir + L"\\*.crdownload").c_str(), &f);
    if (h == INVALID_HANDLE_VALUE) return L"";
    do { if (CompareFileTime(&f.ftLastWriteTime, &newest) > 0) { newest = f.ftLastWriteTime; best = dir + L"\\" + f.cFileName; } } while (FindNextFileW(h, &f));
    FindClose(h); return best;
}
std::wstring StateText() {
    if (!op) return L"(no operation)";
    COREWEBVIEW2_DOWNLOAD_STATE s = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS; op->get_State(&s);
    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON r = COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE; op->get_InterruptReason(&r);
    INT64 got = 0, total = 0; op->get_BytesReceived(&got); op->get_TotalBytesToReceive(&total);
    BOOL resume = FALSE; op->get_CanResume(&resume);
    LPWSTR p = nullptr; op->get_ResultFilePath(&p);
    return L"state=" + std::to_wstring((int)s) + L" reason=" + std::to_wstring((int)r) + L" bytes=" + std::to_wstring(got) + L"/" + std::to_wstring(total) +
           L" canResume=" + std::to_wstring((int)resume) + L" path=" + Str(p);
}
std::wstring ResultPath() { LPWSTR p = nullptr; if (op) op->get_ResultFilePath(&p); return Str(p); }
void Watch(int left, const wchar_t* why) {   // the folder and the state, every 100 ms for a while
    Files(why);
    if (left > 0) After(100, [left, why] { Watch(left - 1, why); });
}
// link-cancel-restore: what src/main.cpp FinishKeep does once the engine let go: the link back at the final path, then
// (--aes on) IAttachmentExecute::Save off the UI thread. Every 10 ms: is the restored file still there?
bool restored = false;
void Survives(std::wstring path, int left) {
    if (!Exists(path)) { Say(L"FACT restored file GONE: " + path + L" " + StateText()); Files(L"restored gone"); return; }
    if (left <= 0) { Say(L"FACT restored file still there after the watch, size " + std::to_wstring(SizeOf(path))); return; }
    After(10, [path, left] { Survives(path, left - 1); });
}
void Restore() {
    if (restored || linkPath.empty()) return; restored = true;
    const std::wstring final = ResultPath();
    Say(L"FACT restore: engine file " + std::wstring(Exists(final) ? L"STILL THERE" : L"gone") + L", " + StateText());
    const BOOL moved = MoveFileExW(linkPath.c_str(), final.c_str(), MOVEFILE_COPY_ALLOWED);
    Say(L"FACT link moved back to the final path: " + std::wstring(moved ? L"ok" : L"FAILED err=" + std::to_wstring(GetLastError())));
    SetFileAttributesW(final.c_str(), FILE_ATTRIBUTE_NORMAL);
    Survives(final, 800);
    if (aes == L"on") {
        const std::wstring src = fileUrl;
        CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto* args = static_cast<std::pair<std::wstring, std::wstring>*>(p);
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); HRESULT hr = E_FAIL; IAttachmentExecute* ae = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ae))) && ae) {
                ae->SetLocalPath(args->first.c_str()); ae->SetSource(args->second.c_str()); hr = ae->Save(); ae->Release();
            }
            wchar_t b[16]; swprintf(b, 16, L"%08lx", (unsigned long)hr);
            Say(L"FACT IAttachmentExecute::Save hr=0x" + std::wstring(b) + L", file " + (Exists(args->first) ? L"there" : L"GONE"));
            CoUninitialize(); delete args; return 0; }, new std::pair<std::wstring, std::wstring>(final, src), 0, nullptr);
    }
}
void Observe(const std::wstring& final);
void Act() {
    if (acted) return; acted = true;
    const std::wstring final = ResultPath(), bytes = BytesFile(final);
    if (then == L"observe") { Observe(final); return; }
    Say(L"FACT bytes file: " + (bytes.empty() ? std::wstring(L"(not found)") : bytes) + (bytes == final ? L" (final path)" : L" (NOT the final path)"));
    const bool linking = then == L"link-cancel" || then == L"link-cancel-restore";
    if (linking) {
        linkPath = (bytes.empty() ? final : bytes) + L".probe-link";
        const BOOL linked = CreateHardLinkW(linkPath.c_str(), bytes.c_str(), nullptr);
        Say(L"FACT hard link " + std::wstring(linked ? L"made " : L"FAILED err=" + std::to_wstring(GetLastError()) + L" ") + linkPath);
        SetFileAttributesW(linkPath.c_str(), FILE_ATTRIBUTE_HIDDEN);   // as the browser's « Conserver » does
    }
    if (otherHandleMs > 0) {   // another program reading the file meanwhile (an antivirus scan, an indexer): every sharing allowed
        HANDLE other = CreateFileW((bytes.empty() ? final : bytes).c_str(), GENERIC_READ, otherShare == L"read" ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        Say(L"FACT another handle on the file " + std::wstring(other != INVALID_HANDLE_VALUE ? L"opened" : L"FAILED") + L" for " + std::to_wstring(otherHandleMs) + L" ms");
        if (other != INVALID_HANDLE_VALUE) After((UINT)otherHandleMs, [other] { CloseHandle(other); Say(L"FACT the other handle is closed"); Files(L"other handle closed"); });
    }
    if (then == L"cancel" || linking) {
        const HRESULT hr = op->Cancel();
        Say(L"FACT Cancel() hr=0x" + [&] { wchar_t b[16]; swprintf(b, 16, L"%08lx", (unsigned long)hr); return std::wstring(b); }() + L" then " + StateText());
        Watch(60, L"after cancel");
        After(then == L"link-cancel-restore" ? 15000 : 6500, [] {
            Say(L"FACT end of the watch after cancel: " + StateText());
            if (!linkPath.empty()) Say(L"FACT link " + std::wstring(Exists(linkPath) ? L"still there, size " + std::to_wstring(SizeOf(linkPath)) : L"GONE"));
            Files(L"final"); Finish(); });
    } else After(3000, [] { Say(L"FACT still: " + StateText()); Files(L"final"); Finish(); });
}
// --then observe. Our hard link (hidden or not), Cancel(), the operation released at once or kept, then both names watched
// every 10 ms: their state (gone / pending: listed but cannot be opened / readable). The engine's name normally goes in
// the same tick as the cancel; when it is still readable 1 s later the run is LINGERING, and then: our link deleted at
// --drop-link-ms (0: never), an unrelated file created and deleted in the folder at --touch-ms (0: never), until
// --observe-ms. Each change is printed with its time since the cancel.
ULONGLONG cancelAt = 0;
std::wstring NameState(const std::wstring& p) {
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) { const DWORD e = GetLastError(); return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? L"gone" : L"pending(attr err " + std::to_wstring(e) + L")"; }
    HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"pending(open err " + std::to_wstring(GetLastError()) + L")";
    CloseHandle(h); return L"readable";
}
std::wstring Since() { return std::to_wstring((long long)(GetTickCount64() - cancelAt)) + L" ms after the cancel"; }
void WatchNames(std::wstring final, std::wstring a0, std::wstring l0) {
    if (finished) return;
    const std::wstring a = NameState(final), l = NameState(linkPath);
    if (a != a0) Say(L"FACT engine's name " + a + L" at " + Since());
    if (restoreOnGone == L"on" && a == L"gone" && a0 != L"gone") {
        ++removals; const BOOL back = CreateHardLinkW(final.c_str(), linkPath.c_str(), nullptr);
        Say(L"FACT REMOVAL #" + std::to_wstring(removals) + L" at " + Since() + L", given back: " + (back ? L"ok" : L"FAILED " + std::to_wstring(GetLastError())));
        After(10, [final, l] { WatchNames(final, NameState(final), l); }); return;
    }
    if (l != l0) Say(L"FACT our link " + l + L" at " + Since());
    After(10, [final, a, l] { WatchNames(final, a, l); });
}
void Observe(const std::wstring& final) {
    linkPath = final + L".probe-link";
    if (!CreateHardLinkW(linkPath.c_str(), final.c_str(), nullptr)) { Say(L"FACT hard link FAILED " + std::to_wstring(GetLastError())); Finish(); return; }
    if (linkHidden == L"on") SetFileAttributesW(linkPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    Say(L"FACT observe: link made (" + std::wstring(linkHidden == L"on" ? L"hidden" : L"not hidden") + L"), operation " + (releaseOp == L"now" ? L"released at once" : L"kept"));
    cancelAt = GetTickCount64();
    const HRESULT hr = op->Cancel();
    Say(L"FACT Cancel() hr=" + std::to_wstring((long)hr) + L", engine's name then " + NameState(final));
    if (releaseOp == L"now") { op->Release(); op = nullptr; }
    WatchNames(final, L"readable", L"readable");
    if (restoreOnGone == L"on") { After((UINT)observeMs, [] { Say(L"FACT end of the observation at " + Since() + L": " + std::to_wstring(removals) + L" removal(s)"); Files(L"final"); Finish(); }); return; }
    After(1000, [final] {
        if (NameState(final) == L"gone") { Say(L"FACT NORMAL: the engine's name went within 1 s"); After(500, [] { Files(L"final"); Finish(); }); return; }
        Say(L"FACT LINGERING: the engine's name is " + NameState(final) + L" 1 s after the cancel");
        if (dropLinkMs > 0) After((UINT)std::max(0, dropLinkMs - 1000), [] {
            SetFileAttributesW(linkPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            const BOOL ok = DeleteFileW(linkPath.c_str()); const DWORD e = GetLastError();
            Say(L"FACT our link deleted at " + Since() + (ok ? L": ok" : L": FAILED err " + std::to_wstring(e))); });
        if (touchMs > 0) After((UINT)std::max(0, touchMs - 1000), [] {
            const std::wstring other = dir + L"\\probe-touch.tmp";
            HANDLE h = CreateFileW(other.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr); if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            DeleteFileW(other.c_str()); Say(L"FACT an unrelated file created and deleted in the folder at " + Since()); });
        After((UINT)std::max(0, observeMs - 1000), [] { Say(L"FACT end of the observation at " + Since()); Files(L"final"); Finish(); });
    });
}
void Check() {   // the hold: every byte in, still in progress past --hold-ms (no event says so)
    if (finished || !op || terminal) return;
    COREWEBVIEW2_DOWNLOAD_STATE s = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS; op->get_State(&s);
    INT64 got = 0, total = 0; op->get_BytesReceived(&got); op->get_TotalBytesToReceive(&total);
    Files(L"poll");
    if (s == COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS && total > 0 && got >= total) {
        if (!fullAt) { fullAt = GetTickCount64(); Say(L"FACT every byte in: " + StateText()); }
        if (then == L"timeline") {   // nothing done: when does the zone mark come, when does the engine complete?
            static bool markSeen = false;
            if (!markSeen && Exists(ResultPath() + L":Zone.Identifier")) { markSeen = true; Say(L"FACT TIMELINE zone mark at " + std::to_wstring((long long)(GetTickCount64() - fullAt)) + L" ms after the last byte"); }
            After(100, Check); return;
        }
        if (!held && GetTickCount64() - fullAt >= (ULONGLONG)holdMs) { held = true; Say(L"FACT held: " + StateText()); if (actOn != L"zone") { Act(); return; } }
        if (held && actOn == L"zone") {
            const bool marked = Exists(ResultPath() + L":Zone.Identifier"), capped = GetTickCount64() - fullAt >= (ULONGLONG)capMs;
            if (marked || capped) { Say(std::wstring(marked ? L"FACT zone mark present " : L"FACT cap reached without the zone mark, ") + std::to_wstring((long long)(GetTickCount64() - fullAt)) + L" ms after the last byte"); Act(); return; }
        }
    }
    After(100, Check);
}
void TrackOperation(ICoreWebView2DownloadOperation* o) {
    op = o; op->AddRef(); EventRegistrationToken token{};
    auto state = Callback<ICoreWebView2StateChangedEventHandler, ICoreWebView2DownloadOperation*, IUnknown*>([](ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
        if (!op) return S_OK;
        Say(L"FACT StateChanged " + StateText()); Files(L"state");
        if (then == L"timeline") { COREWEBVIEW2_DOWNLOAD_STATE s0 = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS; op->get_State(&s0);
            if (s0 != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) Say(L"FACT TIMELINE state " + std::to_wstring((int)s0) + L" at " + (fullAt ? std::to_wstring((long long)(GetTickCount64() - fullAt)) : std::wstring(L"?")) + L" ms after the last byte, zone mark " + (Exists(ResultPath() + L":Zone.Identifier") ? L"there" : L"absent")); }
        COREWEBVIEW2_DOWNLOAD_STATE s = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS; op->get_State(&s);
        if (s != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS && then == L"link-cancel-restore" && acted) {
            if (restoreDelay <= 0) Restore(); else After((UINT)restoreDelay, Restore);
        }
        if (s != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS && !acted && !terminal) {   // finished or interrupted by itself
            terminal = true; Watch(40, L"after end"); After(4500, [] { Say(L"FACT 4.5 s after the end: " + StateText()); Files(L"final"); Finish(); });
        }
        return S_OK; });
    op->add_StateChanged(state, &token); state->Release();
    auto bytes = Callback<ICoreWebView2BytesReceivedChangedEventHandler, ICoreWebView2DownloadOperation*, IUnknown*>([](ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
        if (!op) return S_OK;
        INT64 got = 0, total = 0; op->get_BytesReceived(&got); op->get_TotalBytesToReceive(&total);
        if (got >= total && total > 0) { if (!fullAt) fullAt = GetTickCount64(); Say(L"FACT BytesReceivedChanged (all in) " + StateText()); }
        return S_OK; });
    op->add_BytesReceivedChanged(bytes, &token); bytes->Release();
    Check();
}
void TriggerFile() {
    if (fileTriggered) return; fileTriggered = true;
    Say(L"FACT trigger " + trigger + L" " + fileUrl);
    if (trigger == L"navigate") { view->Navigate(fileUrl.c_str()); return; }
    const std::wstring js = L"location.href=" + std::wstring(L"'") + fileUrl + L"'";
    if (trigger == L"script") { view->ExecuteScript(js.c_str(), nullptr); return; }
    // gesture: DevTools Runtime.evaluate with a user gesture (as a click would give)
    const std::wstring params = L"{\"expression\":\"" + js + L"\",\"userGesture\":true}";
    view->CallDevToolsProtocolMethod(L"Runtime.evaluate", params.c_str(), Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }));
}
void OnView() {
    EventRegistrationToken token{};
    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(view->get_Settings(&settings)) && settings) {
        ICoreWebView2Settings8* s8 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(IID_Settings8, (void**)&s8)) && s8) {
            if (reputation == L"off") s8->put_IsReputationCheckingRequired(FALSE);
            BOOL on = TRUE; s8->get_IsReputationCheckingRequired(&on); Say(L"FACT reputation checking (SmartScreen) " + std::wstring(on ? L"on" : L"off"));
            s8->Release();
        }
        settings->Release();
    }
    ICoreWebView2_4* v4 = nullptr;
    if (FAILED(view->QueryInterface(IID_View4, (void**)&v4)) || !v4) { Say(L"FACT no ICoreWebView2_4"); Finish(); return; }
    auto starting = Callback<ICoreWebView2DownloadStartingEventHandler, ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs*>([](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* a) -> HRESULT {
        a->put_Handled(TRUE);   // no engine flyout, as the browser does
        LPWSTR p = nullptr; a->get_ResultFilePath(&p); const std::wstring suggested = Str(p);
        const size_t slash = suggested.find_last_of(L"\\/");
        const std::wstring target = dir + L"\\" + (slash == std::wstring::npos ? suggested : suggested.substr(slash + 1));
        a->put_ResultFilePath(target.c_str());
        ICoreWebView2DownloadOperation* o = nullptr; a->get_DownloadOperation(&o);
        LPWSTR mime = nullptr; if (o) o->get_MimeType(&mime);
        Say(L"FACT DownloadStarting suggested=" + suggested + L" result=" + target + L" mime=" + Str(mime));
        if (o) { TrackOperation(o); o->Release(); }
        return S_OK; });
    v4->add_DownloadStarting(starting, &token); starting->Release(); v4->Release();
    ICoreWebView2_26* v26 = nullptr;
    if (SUCCEEDED(view->QueryInterface(IID_View26, (void**)&v26)) && v26) {
        auto check = Callback<ICoreWebView2SaveFileSecurityCheckStartingEventHandler, ICoreWebView2*, ICoreWebView2SaveFileSecurityCheckStartingEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2SaveFileSecurityCheckStartingEventArgs* a) -> HRESULT {
                LPWSTR ext = nullptr, path = nullptr, origin = nullptr; a->get_FileExtension(&ext); a->get_FilePath(&path); a->get_DocumentOriginUri(&origin);
                Say(L"FACT SaveFileSecurityCheckStarting ext=" + Str(ext) + L" path=" + Str(path) + L" origin=" + Str(origin) + L" (defaults kept)");
                return S_OK; });
        v26->add_SaveFileSecurityCheckStarting(check, &token); check->Release(); v26->Release();
    } else Say(L"FACT no ICoreWebView2_26");
    auto completed = Callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
            BOOL ok = FALSE; a->get_IsSuccess(&ok); COREWEBVIEW2_WEB_ERROR_STATUS e{}; a->get_WebErrorStatus(&e);
            Say(L"FACT NavigationCompleted ok=" + std::to_wstring((int)ok) + L" status=" + std::to_wstring((int)e));
            if (!fileTriggered) After(700, TriggerFile);
            return S_OK; });
    view->add_NavigationCompleted(completed, &token); completed->Release();
    Files(L"before");
    view->Navigate(url.c_str());
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring k = argv[i], v = argv[i + 1];
        if (k == L"--url") url = v; else if (k == L"--file") fileUrl = v; else if (k == L"--udf") udf = v; else if (k == L"--dir") dir = v;
        else if (k == L"--trigger") trigger = v; else if (k == L"--then") then = v; else if (k == L"--reputation") reputation = v;
        else if (k == L"--hold-ms") holdMs = _wtoi(v.c_str()); else if (k == L"--seconds") seconds = _wtoi(v.c_str());
        else if (k == L"--restore-delay") restoreDelay = _wtoi(v.c_str()); else if (k == L"--aes") aes = v;
        else if (k == L"--other-handle-ms") otherHandleMs = _wtoi(v.c_str()); else if (k == L"--other-share") otherShare = v;
        else if (k == L"--drop-link-ms") dropLinkMs = _wtoi(v.c_str()); else if (k == L"--touch-ms") touchMs = _wtoi(v.c_str());
        else if (k == L"--observe-ms") observeMs = _wtoi(v.c_str()); else if (k == L"--link-hidden") linkHidden = v; else if (k == L"--release-op") releaseOp = v;
        else if (k == L"--act-on") actOn = v; else if (k == L"--restore-on-gone") restoreOnGone = v; else if (k == L"--cap-ms") capMs = _wtoi(v.c_str());
    }
    if (url.empty() || fileUrl.empty() || udf.empty() || dir.empty()) { Say(L"usage: --url U --file F --udf DIR --dir DOWNLOADS"); return 2; }
    t0 = GetTickCount64();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DownloadEngineProbe"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"download probe", WS_POPUP, -32000, -32000, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
        LPWSTR version = nullptr; env->get_BrowserVersionString(&version); Say(L"FACT runtime " + Str(version));
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED (controller)"); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 800, 600}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED (view)"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    const HRESULT hr = create(nullptr, udf.c_str(), nullptr, created); created->Release();
    if (FAILED(hr)) { Say(L"ENGINE FAILED"); return 2; }
    After((UINT)seconds * 1000, [] { Say(L"FACT timeout: " + StateText()); Files(L"timeout"); Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (op) op->Release();
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    return 0;
}
