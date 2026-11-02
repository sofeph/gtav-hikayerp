#include "gtml_js.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "json.hpp"
#include "quickjs.h"

#pragma comment(lib, "winhttp.lib")

namespace hikaye::gtml_js {

using nlohmann::json;

namespace {

// Budget per entry into JS; a runaway loop is interrupted instead of freezing the game.
constexpr ULONGLONG kCallBudgetMs = 50;
constexpr ULONGLONG kInitBudgetMs = 250;
constexpr size_t kMemoryLimit = 32u * 1024u * 1024u;
constexpr size_t kMaxResponseBytes = 2u * 1024u * 1024u;
constexpr int kMaxInflightHttp = 8;
constexpr int kMaxJobsPerTick = 1000;

// DOM shim, timers, fetch and gta bridge. Native hooks: __log, __now, __trigger, __http.
constexpr const char* kPrelude = R"JS(
(function () {
  const g = globalThis;
  const nodes = new Map(), elements = new Map(), listeners = new Map(), dirty = new Set();
  const timers = new Map(), pendingHttp = new Map();
  let timerSeq = 1, httpSeq = 1;

  const fmt = v => {
    if (typeof v === 'string') return v;
    if (v instanceof Error) return v.name + ': ' + v.message;
    try { return typeof v === 'object' && v !== null ? JSON.stringify(v) : String(v); } catch (e) { return String(v); }
  };
  const report = e => __log('JS hata: ' + (e && e.message ? e.message + (e.stack ? '\n' + e.stack : '') : String(e)));
  g.__reportError = report;

  function parseColor(v) {
    v = String(v).trim().toLowerCase();
    let m = v.match(/#([0-9a-f]{6})\b/);
    if (m) return parseInt(m[1], 16);
    m = v.match(/#([0-9a-f]{3})\b/);
    if (m) return parseInt(m[1].split('').map(c => c + c).join(''), 16);
    m = v.match(/rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/);
    if (m) return (+m[1] << 16) | (+m[2] << 8) | +m[3];
    const named = { white: 0xffffff, black: 0, red: 0xff0000, green: 0x008000, blue: 0x0000ff, yellow: 0xffff00, gray: 0x808080, grey: 0x808080 };
    return v in named ? named[v] : -1;
  }

  function makeStyle(n) {
    const store = {};
    return new Proxy(store, {
      get(_, k) { return k === 'display' ? (n.visible === false ? 'none' : (store.display || '')) : (store[k] ?? ''); },
      set(_, k, v) {
        v = String(v);
        store[k] = v;
        if (k === 'display') n.visible = v !== 'none';
        else if (k === 'visibility') n.visible = v !== 'hidden';
        else if (k === 'color') { const c = parseColor(v); if (c >= 0) n.color = c; }
        else if (k === 'backgroundColor' || k === 'background') { const c = parseColor(v); if (c >= 0) n.bg = c; }
        else if (k === 'borderColor') { const c = parseColor(v); if (c >= 0) n.border = c; }
        dirty.add(n.id);
        return true;
      }
    });
  }

  class Element {
    constructor(n) { this._n = n; this.style = makeStyle(n); this._onclick = null; }
    get id() { return this._n.id; }
    get textContent() { return this._n.text ?? ''; }
    set textContent(v) { this._n.text = v == null ? '' : String(v); dirty.add(this._n.id); }
    get innerText() { return this.textContent; }
    set innerText(v) { this.textContent = v; }
    get innerHTML() { return this.textContent; }
    set innerHTML(v) { this.textContent = String(v ?? '').replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]*>/g, ''); }
    get value() { return this._n.value ?? ''; }
    set value(v) { this._n.value = String(v ?? ''); dirty.add(this._n.id); }
    get disabled() { return !!this._n.disabled; }
    set disabled(v) { this._n.disabled = !!v; dirty.add(this._n.id); }
    get hidden() { return this._n.visible === false; }
    set hidden(v) { this._n.visible = !v; dirty.add(this._n.id); }
    get onclick() { return this._onclick; }
    set onclick(fn) { this._onclick = typeof fn === 'function' ? fn : null; }
    addEventListener(type, fn) {
      if (typeof fn !== 'function') return;
      let m = listeners.get(this._n.id);
      if (!m) listeners.set(this._n.id, m = {});
      (m[type] ||= []).push(fn);
    }
    removeEventListener(type, fn) {
      const a = listeners.get(this._n.id)?.[type];
      if (a) { const i = a.indexOf(fn); if (i >= 0) a.splice(i, 1); }
    }
    getAttribute(k) { return k === 'id' ? this._n.id : null; }
    click() { g.__dispatch(this._n.id, 'click'); }
    focus() {}
    blur() {}
  }

  function byId(id) {
    const n = nodes.get(String(id));
    if (!n) return null;
    let e = elements.get(n.id);
    if (!e) elements.set(n.id, e = new Element(n));
    return e;
  }

  g.window = g;
  g.document = {
    title: '',
    body: {},
    getElementById: byId,
    querySelector(s) { return typeof s === 'string' && s[0] === '#' ? byId(s.slice(1)) : null; },
    querySelectorAll(s) { const e = this.querySelector(s); return e ? [e] : []; },
    addEventListener(type, fn) { if ((type === 'DOMContentLoaded' || type === 'load') && typeof fn === 'function') g.setTimeout(fn, 0); }
  };
  g.addEventListener = g.document.addEventListener;

  const log = (...a) => __log(a.map(fmt).join(' '));
  g.console = { log, info: log, debug: log, warn: log, error: log };

  g.setTimeout = (fn, ms, ...args) => { const id = timerSeq++; timers.set(id, { fn, args, at: __now() + (+ms || 0), every: 0 }); return id; };
  g.setInterval = (fn, ms, ...args) => { const id = timerSeq++; const every = Math.max(10, +ms || 0); timers.set(id, { fn, args, at: __now() + every, every }); return id; };
  g.clearTimeout = g.clearInterval = id => { timers.delete(id); };

  class Response {
    constructor(status, body, url) { this.status = status; this.ok = status >= 200 && status < 300; this.statusText = ''; this.url = url; this._b = body; this.headers = { get() { return null; } }; }
    text() { return Promise.resolve(this._b); }
    json() { return new Promise((res, rej) => { try { res(JSON.parse(this._b)); } catch (e) { rej(e); } }); }
  }
  g.fetch = function (url, opts) {
    opts = opts || {};
    return new Promise((resolve, reject) => {
      const id = httpSeq++;
      pendingHttp.set(id, { resolve, reject, url: String(url) });
      const body = opts.body == null ? '' : String(opts.body);
      const headers = opts.headers ? JSON.stringify(opts.headers) : '{}';
      if (!__http(id, String(url), String(opts.method || 'GET').toUpperCase(), body, headers)) {
        pendingHttp.delete(id);
        reject(new TypeError('Failed to fetch'));
      }
    });
  };

  g.gta = { trigger(ev, data) { __trigger(String(ev), data === undefined ? 'null' : JSON.stringify(data)); } };

  g.__init = text => { for (const n of JSON.parse(text)) if (n && n.id) nodes.set(String(n.id), n); };
  g.__tick = () => {
    const now = __now();
    for (const [id, t] of [...timers]) {
      if (t.at > now) continue;
      if (t.every) t.at = now + t.every; else timers.delete(id);
      try { if (typeof t.fn === 'function') { const r = t.fn(...t.args); if (r && typeof r.catch === 'function') r.catch(report); } } catch (e) { report(e); }
    }
  };
  g.__dispatch = (id, type) => {
    const e = byId(id);
    if (!e) return;
    const ev = { type, target: e, currentTarget: e, preventDefault() {}, stopPropagation() {} };
    const fns = (listeners.get(e.id)?.[type] || []).slice();
    if (type === 'click' && e._onclick) fns.push(e._onclick);
    for (const fn of fns) {
      try { const r = fn.call(e, ev); if (r && typeof r.catch === 'function') r.catch(report); } catch (err) { report(err); }
    }
  };
  g.__httpDone = (id, status, body, err) => {
    const p = pendingHttp.get(id);
    if (!p) return;
    pendingHttp.delete(id);
    if (err) p.reject(new TypeError(err)); else p.resolve(new Response(status, body, p.url));
  };
  g.__flush = () => {
    if (!dirty.size) return '';
    const out = [];
    for (const id of dirty) {
      const n = nodes.get(id);
      if (n) out.push({ id, text: n.text ?? '', value: n.value, visible: n.visible !== false, disabled: !!n.disabled, color: n.color, bg: n.bg, border: n.border });
    }
    dirty.clear();
    return JSON.stringify(out);
  };
})();
)JS";

struct HttpResult {
    uint64_t generation = 0;
    int id = 0;
    int status = 0;
    std::string body;
    std::string error;
};

JSRuntime* g_rt = nullptr;
JSContext* g_ctx = nullptr;
std::string g_loaded_source;
json g_layout;
std::unordered_map<std::string, size_t> g_index;  // node id -> position in g_layout["nodes"]
ULONGLONG g_deadline = 0;

std::mutex g_out_mu;
std::string g_current_json;

std::mutex g_click_mu;
std::deque<std::string> g_clicks;

std::atomic<uint64_t> g_generation{0};
std::atomic<int> g_inflight{0};
std::mutex g_http_mu;
std::vector<HttpResult> g_http_done;

std::function<void(const std::string&, const std::string&)> g_trigger;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string StripCrLf(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '\r' || c == '\n'; }), s.end());
    return s;
}

void HttpWorker(uint64_t generation, int id, std::string url, std::string method, std::string body, std::string headers_json) {
    HttpResult r{generation, id, 0, {}, {}};
    HINTERNET session = nullptr, conn = nullptr, req = nullptr;
    do {
        const std::wstring wurl = Widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { r.error = "Invalid URL"; break; }
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring object(uc.lpszUrlPath, uc.dwUrlPathLength);
        object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (object.empty()) object = L"/";

        session = WinHttpOpen(L"HikayeTurkRP-GTML/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { r.error = "Failed to fetch"; break; }
        WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
        conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
        if (!conn) { r.error = "Failed to fetch"; break; }
        req = WinHttpOpenRequest(conn, Widen(method).c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { r.error = "Failed to fetch"; break; }

        std::wstring header_block;
        try {
            const json h = json::parse(headers_json);
            if (h.is_object()) {
                for (const auto& [k, v] : h.items()) {
                    const std::string value = v.is_string() ? v.get<std::string>() : v.dump();
                    header_block += Widen(StripCrLf(k) + ": " + StripCrLf(value) + "\r\n");
                }
            }
        } catch (...) {}

        const BOOL sent = WinHttpSendRequest(
            req, header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_block.c_str(),
            header_block.empty() ? 0 : static_cast<DWORD>(-1L),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data(), static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0);
        if (!sent || !WinHttpReceiveResponse(req, nullptr)) { r.error = "Failed to fetch"; break; }

        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        r.status = static_cast<int>(status);

        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
            if (r.body.size() + avail > kMaxResponseBytes) { r.error = "Response too large"; break; }
            const size_t old = r.body.size();
            r.body.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, r.body.data() + old, avail, &read)) { r.error = "Failed to fetch"; break; }
            r.body.resize(old + read);
        }
    } while (false);
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
    {
        std::lock_guard<std::mutex> lock(g_http_mu);
        g_http_done.push_back(std::move(r));
    }
    g_inflight.fetch_sub(1);
}

std::string ToStdString(JSContext* ctx, JSValueConst v) {
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, v);
    if (!s) return {};
    std::string out(s, len);
    JS_FreeCString(ctx, s);
    return out;
}

void LogException(JSContext* ctx) {
    JSValue exc = JS_GetException(ctx);
    std::string msg = ToStdString(ctx, exc);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stack)) msg += "\n" + ToStdString(ctx, stack);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    Log("[gtml js] hata: " + msg);
}

int InterruptHandler(JSRuntime*, void*) { return GetTickCount64() > g_deadline ? 1 : 0; }

JSValue NativeLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc > 0) Log("[gtml js] " + ToStdString(ctx, argv[0]));
    return JS_UNDEFINED;
}

JSValue NativeNow(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx, static_cast<double>(GetTickCount64()));
}

JSValue NativeTrigger(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc >= 2 && g_trigger) g_trigger(ToStdString(ctx, argv[0]), ToStdString(ctx, argv[1]));
    return JS_UNDEFINED;
}

// __http(id, url, method, body, headersJson) -> bool (false = rejected synchronously)
JSValue NativeHttp(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 5) return JS_FALSE;
    int32_t id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    std::string url = ToStdString(ctx, argv[1]);
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return JS_FALSE;
    if (g_inflight.fetch_add(1) >= kMaxInflightHttp) {
        g_inflight.fetch_sub(1);
        return JS_FALSE;
    }
    std::thread(HttpWorker, g_generation.load(), id, std::move(url), ToStdString(ctx, argv[2]),
                ToStdString(ctx, argv[3]), ToStdString(ctx, argv[4])).detach();
    return JS_TRUE;
}

void AddFunction(JSContext* ctx, JSValueConst global, const char* name, JSCFunction* fn, int length) {
    JS_SetPropertyStr(ctx, global, name, JS_NewCFunction(ctx, fn, name, length));
}

// Calls a prelude function by name; returns its result (caller frees) or JS_EXCEPTION after logging.
JSValue CallGlobal(const char* name, int argc, JSValueConst* argv, ULONGLONG budget = kCallBudgetMs) {
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue fn = JS_GetPropertyStr(g_ctx, global, name);
    g_deadline = GetTickCount64() + budget;
    JSValue result = JS_Call(g_ctx, fn, global, argc, argv);
    if (JS_IsException(result)) LogException(g_ctx);
    JS_FreeValue(g_ctx, fn);
    JS_FreeValue(g_ctx, global);
    return result;
}

void RunJobs() {
    for (int i = 0; i < kMaxJobsPerTick; ++i) {
        JSContext* job_ctx = nullptr;
        g_deadline = GetTickCount64() + kCallBudgetMs;
        const int r = JS_ExecutePendingJob(g_rt, &job_ctx);
        if (r == 0) break;
        if (r < 0 && job_ctx) LogException(job_ctx);
    }
}

void Publish() {
    std::string out = g_layout.is_null() ? std::string() : g_layout.dump();
    std::lock_guard<std::mutex> lock(g_out_mu);
    g_current_json = std::move(out);
}

void Reset() {
    g_generation.fetch_add(1);
    if (g_ctx) JS_FreeContext(g_ctx);
    if (g_rt) JS_FreeRuntime(g_rt);
    g_ctx = nullptr;
    g_rt = nullptr;
    g_layout = nullptr;
    g_index.clear();
    {
        std::lock_guard<std::mutex> lock(g_click_mu);
        g_clicks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_http_mu);
        g_http_done.clear();
    }
    Publish();
}

void Load(const std::string& source) {
    try {
        g_layout = json::parse(source);
    } catch (const std::exception& e) {
        Log(std::string("[gtml js] layout parse hatası: ") + e.what());
        g_layout = nullptr;
        Publish();
        return;
    }
    if (g_layout.contains("nodes") && g_layout["nodes"].is_array()) {
        auto& list = g_layout["nodes"];
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].contains("id") && list[i]["id"].is_string()) g_index[list[i]["id"].get<std::string>()] = i;
        }
    }
    Publish();

    std::vector<std::string> scripts;
    if (g_layout.contains("scripts") && g_layout["scripts"].is_array()) {
        for (const auto& s : g_layout["scripts"]) {
            if (s.is_string() && !s.get<std::string>().empty()) scripts.push_back(s.get<std::string>());
        }
    }
    if (scripts.empty()) return;

    g_rt = JS_NewRuntime();
    if (!g_rt) return;
    JS_SetMemoryLimit(g_rt, kMemoryLimit);
    JS_SetInterruptHandler(g_rt, InterruptHandler, nullptr);
    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) {
        JS_FreeRuntime(g_rt);
        g_rt = nullptr;
        return;
    }

    JSValue global = JS_GetGlobalObject(g_ctx);
    AddFunction(g_ctx, global, "__log", NativeLog, 1);
    AddFunction(g_ctx, global, "__now", NativeNow, 0);
    AddFunction(g_ctx, global, "__trigger", NativeTrigger, 2);
    AddFunction(g_ctx, global, "__http", NativeHttp, 5);
    JS_FreeValue(g_ctx, global);

    g_deadline = GetTickCount64() + kInitBudgetMs;
    JSValue r = JS_Eval(g_ctx, kPrelude, std::strlen(kPrelude), "<gtml-prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        LogException(g_ctx);
        JS_FreeValue(g_ctx, r);
        return;
    }
    JS_FreeValue(g_ctx, r);

    const std::string nodes_text = g_layout.contains("nodes") ? g_layout["nodes"].dump() : "[]";
    JSValue arg = JS_NewStringLen(g_ctx, nodes_text.data(), nodes_text.size());
    JS_FreeValue(g_ctx, CallGlobal("__init", 1, &arg, kInitBudgetMs));
    JS_FreeValue(g_ctx, arg);

    const std::string page = g_layout.value("url", std::string("page.gtml"));
    for (size_t i = 0; i < scripts.size(); ++i) {
        const std::string name = page + "#script" + std::to_string(i + 1);
        g_deadline = GetTickCount64() + kInitBudgetMs;
        JSValue v = JS_Eval(g_ctx, scripts[i].data(), scripts[i].size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(v)) LogException(g_ctx);
        JS_FreeValue(g_ctx, v);
    }
}

void ApplyChanges(const std::string& changes_text) {
    if (changes_text.empty()) return;
    json changes;
    try {
        changes = json::parse(changes_text);
    } catch (...) {
        return;
    }
    auto& list = g_layout["nodes"];
    for (const auto& c : changes) {
        const auto it = g_index.find(c.value("id", std::string()));
        if (it == g_index.end()) continue;
        json& node = list[it->second];
        for (const char* key : {"text", "value", "visible", "disabled", "color", "bg", "border"}) {
            if (c.contains(key)) node[key] = c[key];
        }
    }
    Publish();
}

}  // namespace

void Tick(const std::string& source_layout_json) {
    if (source_layout_json != g_loaded_source) {
        g_loaded_source = source_layout_json;
        Reset();
        if (!source_layout_json.empty()) Load(source_layout_json);
    }
    if (!g_ctx) return;

    std::vector<HttpResult> done;
    {
        std::lock_guard<std::mutex> lock(g_http_mu);
        done.swap(g_http_done);
    }
    const uint64_t generation = g_generation.load();
    for (auto& r : done) {
        if (r.generation != generation) continue;
        JSValue args[4] = {
            JS_NewInt32(g_ctx, r.id),
            JS_NewInt32(g_ctx, r.status),
            JS_NewStringLen(g_ctx, r.body.data(), r.body.size()),
            r.error.empty() ? JS_NULL : JS_NewStringLen(g_ctx, r.error.data(), r.error.size()),
        };
        JS_FreeValue(g_ctx, CallGlobal("__httpDone", 4, args));
        for (auto& a : args) JS_FreeValue(g_ctx, a);
    }

    std::deque<std::string> clicks;
    {
        std::lock_guard<std::mutex> lock(g_click_mu);
        clicks.swap(g_clicks);
    }
    for (const auto& id : clicks) {
        JSValue args[2] = {JS_NewStringLen(g_ctx, id.data(), id.size()), JS_NewString(g_ctx, "click")};
        JS_FreeValue(g_ctx, CallGlobal("__dispatch", 2, args));
        for (auto& a : args) JS_FreeValue(g_ctx, a);
    }

    JS_FreeValue(g_ctx, CallGlobal("__tick", 0, nullptr));
    RunJobs();

    JSValue changes = CallGlobal("__flush", 0, nullptr);
    if (!JS_IsException(changes)) ApplyChanges(ToStdString(g_ctx, changes));
    JS_FreeValue(g_ctx, changes);
}

std::string CurrentJson() {
    std::lock_guard<std::mutex> lock(g_out_mu);
    return g_current_json;
}

void QueueClick(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(g_click_mu);
    if (g_clicks.size() < 32) g_clicks.push_back(node_id);
}

void SetTriggerHandler(std::function<void(const std::string&, const std::string&)> handler) {
    g_trigger = std::move(handler);
}

void Shutdown() {
    g_loaded_source.clear();
    Reset();
}

}  // namespace hikaye::gtml_js
