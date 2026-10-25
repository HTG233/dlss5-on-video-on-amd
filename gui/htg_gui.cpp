// HTG DLSS5-AMD 神经渲染 · 图形界面
// 作者: HTG   版本: 1.01
//
// 原生 Win32，不依赖 .NET，也不依赖 PowerShell。
// 它只是 dlss5-convert.exe 的一层壳：真正干活的是那个转换器。
//
// 深色界面：DWM 暗色标题栏 + Win11 圆角 + AMD 红主题。
// 按钮、单选、进度条全部自绘；输入框保留浅色（深色下的黑色光标看不见）。

#define UNICODE
#define _UNICODE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>

#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 老 SDK 没有这两个常量，按文档里的数值兜底
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#define APP_NAME L"HTG DLSS5-AMD 神经渲染 v1.01"

namespace {

const int ID_EDT_FILE = 101;
const int ID_BTN_BROWSE = 102;
const int ID_CB_PASS = 103;
const int ID_CB_FMT = 104;
const int ID_RB_SAME = 105;
const int ID_RB_PICK = 106;
const int ID_EDT_OUT = 107;
const int ID_BTN_OUT = 108;
const int ID_BTN_START = 109;
const int ID_EDT_LOG = 110;
const int ID_LBL_PCT = 112;
const int ID_BTN_OPEN = 113;
const int ID_BTN_STOP = 114;
const int ID_BTN_VERIFY = 115;
const int ID_BTN_DOC = 116;

const UINT_PTR IDT_POLL = 1;
const UINT kPollMs = 400;

// 按钮外观分类
const int K_PRIMARY = 0;
const int K_SECONDARY = 1;
const int K_GHOST = 2;
const int K_RADIO = 3;

// 调色板（深色底 + AMD 红点缀）
const COLORREF C_BG       = RGB(0x1E, 0x1E, 0x1E);
const COLORREF C_HEADER   = RGB(0x25, 0x25, 0x25);
const COLORREF C_CARD     = RGB(0x27, 0x27, 0x27);
const COLORREF C_BORDER   = RGB(0x3C, 0x3C, 0x3C);
const COLORREF C_TEXT     = RGB(0xF2, 0xF2, 0xF2);
const COLORREF C_TEXT2    = RGB(0xAB, 0xAB, 0xAB);
const COLORREF C_HINT     = RGB(0x7E, 0x7E, 0x7E);
const COLORREF C_ACCENT   = RGB(0xED, 0x1C, 0x24);
const COLORREF C_ACCENT_H = RGB(0xFF, 0x40, 0x49);
const COLORREF C_ACCENT_P = RGB(0xB8, 0x12, 0x1B);
const COLORREF C_LOG_BG   = RGB(0x14, 0x14, 0x14);
const COLORREF C_LOG_TX   = RGB(0xD8, 0xD8, 0xD8);
const COLORREF C_FIELD    = RGB(0xF4, 0xF4, 0xF4);
const COLORREF C_FIELD_TX = RGB(0x1A, 0x1A, 0x1A);
const COLORREF C_BTN      = RGB(0x2D, 0x2D, 0x2D);
const COLORREF C_BTN_H    = RGB(0x38, 0x38, 0x38);
const COLORREF C_BTN_P    = RGB(0x22, 0x22, 0x22);
const COLORREF C_BTN_BR   = RGB(0x48, 0x48, 0x48);
const COLORREF C_DIS_BG   = RGB(0x26, 0x26, 0x26);
const COLORREF C_DIS_TX   = RGB(0x66, 0x66, 0x66);
const COLORREF C_DIS_BR   = RGB(0x33, 0x33, 0x33);

HWND g_hwnd = nullptr;
HWND g_edtFile = nullptr;
HWND g_cbPass = nullptr;
HWND g_cbFmt = nullptr;
HWND g_rbSame = nullptr;
HWND g_rbPick = nullptr;
HWND g_edtOut = nullptr;
HWND g_btnOut = nullptr;
HWND g_btnStart = nullptr;
HWND g_edtLog = nullptr;
HWND g_lblPct = nullptr;
HWND g_btnOpen = nullptr;
HWND g_btnStop = nullptr;
HWND g_btnVerify = nullptr;
HWND g_btnDoc = nullptr;

HFONT g_fontUI = nullptr;
HFONT g_fontBig = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontLog = nullptr;

HBRUSH g_brBg = nullptr;
HBRUSH g_brHeader = nullptr;
HBRUSH g_brCard = nullptr;
HBRUSH g_brAccent = nullptr;
HBRUSH g_brLogBg = nullptr;
HBRUSH g_brField = nullptr;

struct BtnVis { HWND h; int kind; bool hover; bool down; };
std::vector<BtnVis> g_btns;

struct LblInfo { HWND h; COLORREF text; HBRUSH bk; };
std::vector<LblInfo> g_labels;

std::wstring g_dir;        // 本 exe 所在目录（包根目录）
std::wstring g_engine;     // engine 目录，兼容旧布局时等于 g_dir
std::wstring g_converter;
std::wstring g_assets;
std::wstring g_guiLog;     // 诊断日志：%TEMP%\HTG-DLSS5-AMD-gui.log
std::wstring g_logPath;
std::wstring g_runnerPath;
std::wstring g_lastOutput;

PROCESS_INFORMATION g_pi = {};
bool g_running = false;
bool g_selftest = false;                 // --selftest <视频>：自动跑一遍，收尾自己退出
std::wstring g_selftestVideo;
size_t g_logPos = 0;
DWORD g_startTick = 0;
int g_progress = 0;
RECT g_progRect = { 16, 364, 604, 380 };

// ---------------------------------------------------------------- 编码转换

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

// cmd.exe 按系统 ANSI 代码页（简体中文是 936/GBK）读批处理，所以写入时
// 必须转成 936，否则中文路径会变成乱码、命令行会被切碎。
std::string WideToAnsi(const std::wstring& s, UINT codePage)
{
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(codePage, 0, s.data(), (int)s.size(),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(codePage, 0, s.data(), (int)s.size(), &out[0], n,
                        nullptr, nullptr);
    return out;
}

// 诊断日志用：把宽字符写成 UTF-8，记事本和 Read 工具都能直接看
std::string WideToUtf8(const std::wstring& s)
{
    return WideToAnsi(s, CP_UTF8);
}

std::wstring WideLower(std::wstring s)
{
    for (auto& c : s) { if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a'); }
    return s;
}

std::wstring TrimQuotes(std::wstring s)
{
    const wchar_t* space = L" \"";
    const size_t b = s.find_first_not_of(space);
    if (b == std::wstring::npos) return std::wstring();
    const size_t e = s.find_last_not_of(space);
    return s.substr(b, e - b + 1);
}

std::wstring GetText(HWND h)
{
    const int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)(n > 0 ? n : 0) + 1, L'\0');
    if (n > 0) GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)(n > 0 ? n : 0));
    return s;
}

// ---------------------------------------------------------------- 小工具

void AppendLog(const std::wstring& text)
{
    if (text.empty()) return;
    // EM_REPLACESEL 在 ES_READONLY 的编辑框上是无效的，只能整段重设。
    const int len = GetWindowTextLengthW(g_edtLog);
    // GetWindowTextW 会写 len+1 个字符（含结尾 null），缓冲必须多留一个。
    std::wstring all((size_t)len + 1, L'\0');
    if (len > 0) GetWindowTextW(g_edtLog, &all[0], len + 1);
    all.resize((size_t)len);
    all += text;
    SetWindowTextW(g_edtLog, all.c_str());
    SendMessageW(g_edtLog, EM_SETSEL, (WPARAM)all.size(), (LPARAM)all.size());
    SendMessageW(g_edtLog, EM_SCROLLCARET, 0, 0);
}

void AppendLogLine(const std::wstring& text)
{
    AppendLog(text + L"\r\n");
}

std::wstring TempPath(const wchar_t* prefix, const wchar_t* ext)
{
    wchar_t tmp[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, tmp);
    const DWORD stamp = GetTickCount();
    wchar_t name[MAX_PATH + 1];
    wsprintfW(name, L"%s%s%lu%s", tmp, prefix, stamp, ext);
    return std::wstring(name);
}

// ---------------------------------------------------------------- 诊断日志

// 不管成功还是失败都记一行。以前的版本只有转换器真正启动后才有日志，
// 启动前的失败（找不到引擎、参数不合法）什么都不留，事后无从查起。
void GuiLog(const std::wstring& msg)
{
    if (g_guiLog.empty()) return;
    HANDLE h = CreateFileW(g_guiLog.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size = {};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 2 * 1024 * 1024)
    {
        CloseHandle(h);   // 超过 2 MB 重开一个，免得文件无限长大
        DeleteFileW(g_guiLog.c_str());
        h = CreateFileW(g_guiLog.c_str(), FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t ts[64];
    wsprintfW(ts, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::string line = WideToUtf8(std::wstring(ts) + msg + L"\r\n");
    SetFilePointer(h, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(h, line.data(), (DWORD)line.size(), &written, nullptr);
    CloseHandle(h);
}

void TerminatePid(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) { TerminateProcess(h, 1); CloseHandle(h); }
}

// 只杀 cmd.exe 没用：转换器和 NeuralWorker 是它的子进程，会继续烧 GPU。
void KillTree(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    std::vector<DWORD> children;
    if (snap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe))
        {
            do { if (pe.th32ParentProcessID == pid) children.push_back(pe.th32ProcessID); }
            while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    for (DWORD child : children) KillTree(child);
    TerminatePid(pid);
}

int ParsePercent(const std::string& chunk)
{
    int last = -1;
    size_t pos = 0;
    while ((pos = chunk.find('%', pos)) != std::string::npos)
    {
        size_t d = pos;
        while (d > 0 && chunk[d - 1] >= '0' && chunk[d - 1] <= '9') --d;
        if (d < pos && d > 0 && chunk[d - 1] == '(')
        {
            const int v = atoi(chunk.substr(d, pos - d).c_str());
            if (v >= 0 && v <= 100) last = v;
        }
        ++pos;
    }
    return last;
}

// ---------------------------------------------------------------- 图标（程序里画一个，不带资源文件）

HICON MakeAppIcon(int px)
{
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = px;
    bi.bV5Height = -px;   // 自上而下
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(screen, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                     &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) { if (color) DeleteObject(color); DeleteDC(mem); return nullptr; }

    HGDIOBJ oldBmp = SelectObject(mem, color);

    const int r = px / 5;
    HBRUSH br = CreateSolidBrush(C_ACCENT);
    HPEN noPen = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ ob = SelectObject(mem, br);
    HGDIOBJ op = SelectObject(mem, noPen);
    RoundRect(mem, 1, 1, px - 1, px - 1, r, r);
    SelectObject(mem, ob);
    SelectObject(mem, op);

    HFONT f = CreateFontW(-px * 7 / 10, 0, 0, 0, FW_BLACK, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HGDIOBJ of = SelectObject(mem, f);
    SetTextColor(mem, RGB(0xFF, 0xFF, 0xFF));
    SetBkMode(mem, TRANSPARENT);
    RECT rc = { 0, 0, px, px };
    DrawTextW(mem, L"H", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, of);
    DeleteObject(f);
    DeleteObject(br);

    // GDI 不写 alpha 通道：凡是画过的像素（RGB 非 0）补成不透明，
    // 没画的保持全透明，圆角外面就透出来了。
    UINT32* px32 = (UINT32*)bits;
    for (int i = 0; i < px * px; ++i)
        if (px32[i] & 0x00FFFFFFu) px32[i] |= 0xFF000000u;

    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);

    SelectObject(mem, oldBmp);
    DeleteDC(mem);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

// ---------------------------------------------------------------- 绘制

BtnVis* FindBtn(HWND h)
{
    for (auto& b : g_btns) if (b.h == h) return &b;
    return nullptr;
}

void RepaintButtons()
{
    for (auto& b : g_btns) InvalidateRect(b.h, nullptr, FALSE);
}

void PaintText(HDC hdc, const wchar_t* s, int x, int y, int w, int h,
               COLORREF c, HFONT f, UINT align = DT_LEFT)
{
    RECT rc = { x, y, x + w, y + h };
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    HGDIOBJ old = SelectObject(hdc, f);
    DrawTextW(hdc, s, -1, &rc, align | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old);
}

void Card(HDC hdc, int l, int t, int r, int b)
{
    HBRUSH br = CreateSolidBrush(C_CARD);
    HPEN pen = CreatePen(PS_INSIDEFRAME, 1, C_BORDER);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, l, t, r, b, 8, 8);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

void Ring(HDC hdc, int l, int t, int r, int b)
{
    HPEN pen = CreatePen(PS_INSIDEFRAME, 1, C_BORDER);
    HGDIOBJ op = SelectObject(hdc, pen);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, l, t, r, b);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

void CardTitle(HDC hdc, const wchar_t* s, int ty)
{
    RECT tick = { 32, ty + 3, 35, ty + 15 };
    FillRect(hdc, &tick, g_brAccent);
    PaintText(hdc, s, 42, ty, 140, 18, C_TEXT2, g_fontSmall);
}

void DrawProgress(HDC hdc)
{
    const int x = g_progRect.left, y = g_progRect.top;
    const int w = g_progRect.right - x, h = g_progRect.bottom - y;
    const int rad = h / 2;

    HBRUSH br = CreateSolidBrush(C_LOG_BG);
    HPEN pen = CreatePen(PS_INSIDEFRAME, 1, C_BORDER);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, x, y, x + w, y + h, rad, rad);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);

    if (g_progress > 0)
    {
        int fw = (int)((double)w * g_progress / 100.0);
        if (fw < h) fw = h;   // 至少一个圆点，别让早期进度看不见
        HBRUSH fill = CreateSolidBrush(C_ACCENT);
        HGDIOBJ ob2 = SelectObject(hdc, fill);
        HGDIOBJ op2 = SelectObject(hdc, GetStockObject(NULL_PEN));
        RoundRect(hdc, x, y, x + fw, y + h, rad, rad);
        SelectObject(hdc, ob2);
        SelectObject(hdc, op2);
        DeleteObject(fill);
    }
}

void PaintChrome(HDC hdc)
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);

    // 头部
    RECT band = { 0, 0, rc.right, 68 };
    FillRect(hdc, &band, g_brHeader);
    RECT accent = { 0, 0, 4, 68 };
    FillRect(hdc, &accent, g_brAccent);
    RECT line = { 0, 68, rc.right, 69 };
    FillRect(hdc, &line, g_brCard);
    PaintText(hdc, L"HTG DLSS5-AMD", 22, 10, 420, 30, C_TEXT, g_fontTitle);
    PaintText(hdc, L"神经渲染转换器 · v1.01 · 作者 HTG", 23, 42, 420, 18, C_TEXT2, g_fontSmall);

    // 卡片
    Card(hdc, 16, 84, 684, 164);
    Card(hdc, 16, 176, 684, 288);
    Card(hdc, 16, 392, 684, 584);
    CardTitle(hdc, L"输入视频", 96);
    CardTitle(hdc, L"转换选项", 188);
    CardTitle(hdc, L"运行日志", 404);
    PaintText(hdc, L"也可直接把视频拖进窗口", 420, 96, 248, 16, C_HINT, g_fontSmall, DT_RIGHT);
    PaintText(hdc, L"网络遍数", 32, 222, 56, 22, C_TEXT2, g_fontUI);
    PaintText(hdc, L"输出格式", 208, 222, 56, 22, C_TEXT2, g_fontUI);
    PaintText(hdc, L"输出位置", 352, 222, 60, 22, C_TEXT2, g_fontUI);

    // 输入框描边
    Ring(hdc, 31, 121, 569, 150);
    Ring(hdc, 31, 251, 577, 280);
    Ring(hdc, 27, 427, 673, 569);

    // 进度条
    DrawProgress(hdc);

    // 底部提示
    PaintText(hdc, L"首次运行需现场编译内核 · 同名输出会覆盖 · 日志末尾即失败原因",
              16, 592, 424, 24, C_HINT, g_fontSmall);
}

void DrawButton(const DRAWITEMSTRUCT* d)
{
    BtnVis* v = FindBtn(d->hwndItem);
    if (!v) return;
    HDC hdc = d->hDC;
    const RECT rc = d->rcItem;
    const bool disabled = (d->itemState & ODS_DISABLED) != 0;
    SetBkMode(hdc, TRANSPARENT);

    if (v->kind == K_RADIO)
    {
        const int cx = rc.left + 8;
        const int cy = (rc.top + rc.bottom) / 2;
        const int r = 7;
        HPEN pen = CreatePen(PS_SOLID, 1, disabled ? C_DIS_TX : C_TEXT2);
        HBRUSH bg = CreateSolidBrush(C_CARD);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, bg);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        if (SendMessageW(d->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            HBRUSH dot = CreateSolidBrush(disabled ? C_DIS_TX : C_ACCENT);
            SelectObject(hdc, dot);
            SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, cx - 4, cy - 4, cx + 4, cy + 4);
            DeleteObject(dot);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(bg);
        DeleteObject(pen);
        SetTextColor(hdc, disabled ? C_DIS_TX : C_TEXT);
        RECT tr = { rc.left + 24, rc.top, rc.right, rc.bottom };
        HGDIOBJ of = SelectObject(hdc, g_fontUI);
        DrawTextW(hdc, GetText(d->hwndItem).c_str(), -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        return;
    }

    COLORREF fill, text, border;
    if (disabled)
    {
        fill = C_DIS_BG; text = C_DIS_TX; border = C_DIS_BR;
    }
    else if (v->kind == K_PRIMARY)
    {
        fill = v->down ? C_ACCENT_P : (v->hover ? C_ACCENT_H : C_ACCENT);
        text = RGB(0xFF, 0xFF, 0xFF);
        border = fill;
    }
    else if (v->kind == K_GHOST)
    {
        fill = v->hover ? RGB(0x33, 0x33, 0x33) : C_HEADER;
        text = C_TEXT2;
        border = RGB(0x46, 0x46, 0x46);
    }
    else
    {
        fill = v->down ? C_BTN_P : (v->hover ? C_BTN_H : C_BTN);
        text = C_TEXT;
        border = C_BTN_BR;
    }

    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_INSIDEFRAME, 1, border);
    HGDIOBJ ob = SelectObject(hdc, br);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(br);
    DeleteObject(pen);

    HFONT f = (v->kind == K_PRIMARY) ? g_fontBig
             : (v->kind == K_GHOST)   ? g_fontSmall
                                      : g_fontUI;
    SetTextColor(hdc, text);
    HGDIOBJ of = SelectObject(hdc, f);
    DrawTextW(hdc, GetText(d->hwndItem).c_str(), -1, (RECT*)&rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

LRESULT CALLBACK BtnSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                             UINT_PTR, DWORD_PTR)
{
    BtnVis* v = FindBtn(h);
    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (v && !v->hover)
        {
            v->hover = true;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    case WM_MOUSELEAVE:
        if (v && v->hover) { v->hover = false; InvalidateRect(h, nullptr, FALSE); }
        break;
    case WM_LBUTTONDOWN:
        if (v) { v->down = true; InvalidateRect(h, nullptr, FALSE); }
        break;
    case WM_LBUTTONUP:
        if (v) { v->down = false; InvalidateRect(h, nullptr, FALSE); }
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------- 控件工厂

HWND MakeButton(HWND parent, const wchar_t* text, int x, int y, int w, int h,
                int id, int kind, HFONT font, bool group = false)
{
    DWORD style = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    if (group) style |= WS_GROUP;
    if (kind == K_RADIO) style |= BS_AUTORADIOBUTTON | BS_NOTIFY;
    HWND hw = CreateWindowW(L"BUTTON", text, style, x, y, w, h, parent,
                            (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    g_btns.push_back({ hw, kind, false, false });
    SetWindowSubclass(hw, BtnSubclass, 1, 0);
    SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

HWND MakeEdit(HWND parent, int x, int y, int w, int h, int id, DWORD extra)
{
    return CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | extra,
                         x, y, w, h, parent, (HMENU)(INT_PTR)id,
                         GetModuleHandleW(nullptr), nullptr);
}

HWND MakeCombo(HWND parent, int x, int y, int w, int h, int id)
{
    HWND hw = CreateWindowW(WC_COMBOBOXW, L"",
                            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                            x, y, w, h, parent, (HMENU)(INT_PTR)id,
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    return hw;
}

HWND MakeLabelCtl(HWND parent, const wchar_t* text, int x, int y, int w, int h,
                  COLORREF color, HBRUSH bk)
{
    HWND hw = CreateWindowW(L"STATIC", text,
                            WS_CHILD | WS_VISIBLE | SS_RIGHT,
                            x, y, w, h, parent, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hw, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    g_labels.push_back({ hw, color, bk });
    return hw;
}

// ---------------------------------------------------------------- 找 cmd.exe

// CreateProcess 的 lpApplicationName 给裸文件名时不会走 PATH 搜索，只按
// 当前目录拼。工作目录是 engine\，那里没有 cmd.exe，结果就是
// ERROR_FILE_NOT_FOUND(2)。所以必须自己解析出完整路径。
std::wstring FindCmdExe()
{
    wchar_t buf[MAX_PATH + 1] = {};
    const DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES)
        return std::wstring(buf);

    wchar_t sys[MAX_PATH + 1] = {};
    if (GetSystemDirectoryW(sys, MAX_PATH))
    {
        const std::wstring p = std::wstring(sys) + L"\\cmd.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }

    wchar_t win[MAX_PATH + 1] = {};
    if (GetWindowsDirectoryW(win, MAX_PATH))
    {
        const std::wstring p = std::wstring(win) + L"\\System32\\cmd.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";   // 交给上层报错，别硬塞个裸名再撞一次
}

// ---------------------------------------------------------------- 运行

void SetInputsEnabled(bool on)
{
    EnableWindow(g_edtFile, on);
    EnableWindow(g_btnStart, on);
    EnableWindow(g_cbPass, on);
    EnableWindow(g_cbFmt, on);
    EnableWindow(g_rbSame, on);
    EnableWindow(g_rbPick, on);
    EnableWindow(g_btnStop, !on);
    const bool pick = SendMessageW(g_rbPick, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_edtOut, on && pick);
    EnableWindow(g_btnOut, on && pick);
    RepaintButtons();
}

void PumpLog()
{
    if (g_logPath.empty()) return;
    HANDLE h = CreateFileW(g_logPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(h, &li)) { CloseHandle(h); return; }
    if ((size_t)li.QuadPart <= g_logPos) { CloseHandle(h); return; }

    const size_t want = (size_t)li.QuadPart - g_logPos;
    SetFilePointer(h, (LONG)g_logPos, nullptr, FILE_BEGIN);
    std::string buf(want, '\0');
    DWORD read = 0;
    ReadFile(h, &buf[0], (DWORD)want, &read, nullptr);
    CloseHandle(h);
    if (read == 0) return;
    buf.resize(read);

    // 只在整行边界上消费，避免把一行（或一个 UTF-8 字符）切成两半。
    const size_t nl = buf.rfind('\n');
    if (nl == std::string::npos) return;
    const std::string use = buf.substr(0, nl + 1);
    g_logPos += use.size();

    AppendLog(Utf8ToWide(use));

    const int pct = ParsePercent(use);
    if (pct >= 0)
    {
        g_progress = pct;
        InvalidateRect(g_hwnd, &g_progRect, FALSE);
        SetWindowTextW(g_lblPct, (std::to_wstring(pct) + L"%").c_str());
    }
}

void FinishRun()
{
    KillTimer(g_hwnd, IDT_POLL);
    DWORD code = 0;
    if (g_pi.hProcess)
    {
        WaitForSingleObject(g_pi.hProcess, 5000);
        GetExitCodeProcess(g_pi.hProcess, &code);
        CloseHandle(g_pi.hProcess);
        CloseHandle(g_pi.hThread);
    }
    g_pi = PROCESS_INFORMATION{};
    g_running = false;

    PumpLog();

    if (!g_runnerPath.empty())
    {
        DeleteFileW(g_runnerPath.c_str());
        g_runnerPath.clear();
    }

    SetWindowTextW(g_hwnd, APP_NAME);
    SetWindowTextW(g_btnStart, L"开始转换");
    SetInputsEnabled(true);

    if (code == 0)
    {
        g_progress = 100;
        InvalidateRect(g_hwnd, &g_progRect, FALSE);
        SetWindowTextW(g_lblPct, L"100%");
        const DWORD secs = (GetTickCount() - g_startTick) / 1000;
        AppendLogLine(L"");
        AppendLogLine(L"=== 完成，用时 " + std::to_wstring(secs) + L" 秒 ===");
        GuiLog(L"转换完成: 退出码 0，用时 " + std::to_wstring(secs) + L" 秒，输出 " +
               g_lastOutput);
        EnableWindow(g_btnOpen, TRUE);
    }
    else
    {
        AppendLogLine(L"");
        AppendLogLine(L"=== 失败，退出码 " + std::to_wstring(code) + L" ===");
        AppendLogLine(L"上面日志末尾几行就是失败原因。");
        GuiLog(L"转换失败: 退出码 " + std::to_wstring(code) + L"，转换日志 " + g_logPath);
    }
    if (g_selftest) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    RepaintButtons();
}

bool StartRun()
{
    const std::wstring src = TrimQuotes(GetText(g_edtFile));
    if (src.empty())
    {
        GuiLog(L"校验失败: 没选视频");
        MessageBoxW(g_hwnd, L"先选一个视频。", APP_NAME, MB_ICONINFORMATION);
        return false;
    }
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        GuiLog(L"校验失败: 文件不存在 " + src);
        MessageBoxW(g_hwnd, (L"文件不存在：\n" + src).c_str(), APP_NAME, MB_ICONWARNING);
        return false;
    }
    // 拖进来的是文件夹时，--out 没法用：直接说清楚，别让转换器去猜。
    const DWORD srcAttr = GetFileAttributesW(src.c_str());
    if (srcAttr != INVALID_FILE_ATTRIBUTES && (srcAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
        GuiLog(L"校验失败: 输入是文件夹 " + src);
        MessageBoxW(g_hwnd, L"这是文件夹，不是视频文件。\n\n"
                            L"单个视频请点「浏览...」选中文件；\n"
                            L"整个文件夹批量处理请把文件夹拖到包里的「一键转换.cmd」上。",
                    APP_NAME, MB_ICONWARNING);
        return false;
    }
    if (GetFileAttributesW(g_converter.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        GuiLog(L"校验失败: 找不到转换器，找过 " + g_converter);
        MessageBoxW(g_hwnd, (L"找不到 dlss5-convert.exe，无法开始。\n\n"
                             L"我在这两个位置找过：\n  " + g_engine + L"\\dlss5-convert.exe\n  " +
                             g_dir + L"\\dlss5-convert.exe\n\n"
                             L"请把整个包（含 engine 目录）一起拷贝，不要只拷 exe。\n\n"
                             L"诊断日志：" + g_guiLog).c_str(),
                    APP_NAME, MB_ICONERROR);
        return false;
    }
    if (GetFileAttributesW((g_assets + L"\\dlssnr-amd").c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        GuiLog(L"校验失败: 找不到运行时资产 " + g_assets);
        MessageBoxW(g_hwnd, (L"找不到 AMD 运行时资产，无法开始。\n\n"
                             L"应该在：\n  " + g_assets + L"\\dlssnr-amd\n\n"
                             L"请确认整个包是完整拷贝的（engine\\amd-nr-assets 不能缺）。\n\n"
                             L"诊断日志：" + g_guiLog).c_str(),
                    APP_NAME, MB_ICONERROR);
        return false;
    }

    std::wstring out;
    if (SendMessageW(g_rbPick, BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        out = TrimQuotes(GetText(g_edtOut));
        if (out.empty())
        {
            MessageBoxW(g_hwnd, L"选了“指定文件”但没填路径。", APP_NAME, MB_ICONINFORMATION);
            return false;
        }
    }
    else
    {
        std::wstring dir = src;
        const size_t slash = dir.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos) ? dir : dir.substr(slash + 1);
        dir = (slash == std::wstring::npos) ? L"" : dir.substr(0, slash);
        const size_t dot = name.rfind(L'.');
        if (dot != std::wstring::npos) name = name.substr(0, dot);
        // 容器只由扩展名决定：转换器不接受 --format 和 --out 同时使用。
        const bool mp4 = SendMessageW(g_cbFmt, CB_GETCURSEL, 0, 0) == 1;
        out = dir + L"\\" + name + L"-nr." + (mp4 ? L"mp4" : L"mkv");
    }

    std::wstring ext = L"";
    const size_t dot = out.rfind(L'.');
    if (dot != std::wstring::npos) ext = WideLower(out.substr(dot + 1));
    if (ext != L"mkv" && ext != L"mp4")
    {
        MessageBoxW(g_hwnd, (L"输出只支持 mkv 或 mp4，现在是 ." + ext + L"，请改一下。").c_str(),
                    APP_NAME, MB_ICONWARNING);
        return false;
    }

    if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES && !g_selftest)
    {
        const int ask = MessageBoxW(g_hwnd, (L"输出文件已经存在，要覆盖吗？\n" + out).c_str(),
                                    L"覆盖确认", MB_YESNO | MB_ICONQUESTION);
        if (ask != IDYES)
        {
            GuiLog(L"用户取消: 输出已存在，选择不覆盖 " + out);
            return false;
        }
    }
    g_lastOutput = out;

    const int passes = (int)SendMessageW(g_cbPass, CB_GETCURSEL, 0, 0) + 1;

    g_logPos = 0;
    SetWindowTextW(g_edtLog, L"");
    g_progress = 0;
    InvalidateRect(g_hwnd, &g_progRect, FALSE);
    SetWindowTextW(g_lblPct, L"");
    EnableWindow(g_btnOpen, FALSE);
    g_startTick = GetTickCount();

    g_logPath = TempPath(L"htg_nr_", L".log");
    g_runnerPath = TempPath(L"htg_nr_", L".cmd");
    DeleteFileW(g_logPath.c_str());

    AppendLogLine(L"源文件 : " + src);
    AppendLogLine(L"输出   : " + out);
    AppendLogLine(L"网络   : " + std::to_wstring(passes) + L" 遍");
    AppendLogLine(L"");
    GuiLog(L"开始转换: 输入=" + src + L" | 输出=" + out + L" | 遍数=" +
           std::to_wstring(passes) + L" | 转换日志=" + g_logPath);

    const std::wstring q = L"\"";
    // 资产目录显式指到 engine\amd-nr-assets：转换器自己也会找，但显式指定
    // 不依赖它的解析方式（exe 目录还是当前目录），双保险。
    std::wstring batch =
        L"@echo off\r\n"
        L"if not defined AMDLSS_NR_ASSETS set \"AMDLSS_NR_ASSETS=" + g_assets + L"\"\r\n"
        L"set \"AMDNR_PASSES=" + std::to_wstring(passes) + L"\"\r\n" +
        q + g_converter + L"\" \"" + src + L"\" --stages nr --out \"" + out +
        L"\" --overwrite > \"" + g_logPath + L"\" 2>&1\r\n"
        L"exit /b %ERRORLEVEL%\r\n";

    // 936 = 简体中文 GBK，cmd 读批处理用的就是它。
    const std::string ansi = WideToAnsi(batch, 936);
    HANDLE hf = CreateFileW(g_runnerPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE)
    {
        const std::wstring why = L"写不出临时批处理 " + g_runnerPath +
                                 L"，GetLastError=" + std::to_wstring(GetLastError());
        AppendLogLine(L"启动失败：" + why);
        GuiLog(L"启动失败: " + why);
        return false;
    }
    DWORD written = 0;
    WriteFile(hf, ansi.data(), (DWORD)ansi.size(), &written, nullptr);
    CloseHandle(hf);

    // cmd 负责把输出重定向进文件：这样既能实时读进度，也不会因为
    // 管道缓冲写满而把子进程卡死。工作目录设成 engine，日志和缓存都落在那里。
    const std::wstring cmd = FindCmdExe();
    if (cmd.empty())
    {
        const std::wstring why = L"找不到 cmd.exe（ComSpec、System32 都没有）";
        AppendLogLine(L"启动失败：" + why);
        GuiLog(L"启动失败: " + why);
        DeleteFileW(g_runnerPath.c_str());
        g_runnerPath.clear();
        return false;
    }
    const std::wstring args = L"/d /c \"" + g_runnerPath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // lpCommandLine 按约定以 argv[0] 开头（带引号，防路径里有空格）
    std::wstring mutableArgs = L"\"" + cmd + L"\" " + args;
    if (!CreateProcessW(cmd.c_str(), &mutableArgs[0], nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, g_engine.c_str(), &si, &g_pi))
    {
        const DWORD err = GetLastError();
        AppendLogLine(L"启动失败：CreateProcess 返回 " + std::to_wstring(err));
        GuiLog(L"启动失败: CreateProcess 返回 " + std::to_wstring(err) + L"，程序 " +
               cmd + L"，命令行 " + args + L"，工作目录 " + g_engine);
        DeleteFileW(g_runnerPath.c_str());
        g_runnerPath.clear();
        return false;
    }

    GuiLog(L"已启动: " + cmd + L" PID=" + std::to_wstring(g_pi.dwProcessId));
    g_running = true;
    SetWindowTextW(g_btnStart, L"正在转换…");
    SetInputsEnabled(false);
    SetTimer(g_hwnd, IDT_POLL, kPollMs, nullptr);
    return true;
}

void StopRun()
{
    if (!g_running) return;
    KillTimer(g_hwnd, IDT_POLL);
    if (g_pi.hProcess) KillTree(g_pi.dwProcessId);
    if (g_pi.hProcess) CloseHandle(g_pi.hProcess);
    if (g_pi.hThread) CloseHandle(g_pi.hThread);
    g_pi = PROCESS_INFORMATION{};
    g_running = false;
    if (!g_runnerPath.empty()) { DeleteFileW(g_runnerPath.c_str()); g_runnerPath.clear(); }
    SetWindowTextW(g_hwnd, APP_NAME);
    SetWindowTextW(g_btnStart, L"开始转换");
    SetInputsEnabled(true);
    AppendLogLine(L"已手动停止。");
    GuiLog(L"用户手动停止");
    RepaintButtons();
}

// ---------------------------------------------------------------- 界面

void PickInput()
{
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(MAX_PATH * 4);
    ofn.lpstrFilter = L"视频文件\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.ts;*.flv;*.m4v\0所有文件\0*.*\0";
    ofn.lpstrTitle = L"选择要处理的视频";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(g_edtFile, file);
}

void PickOutput()
{
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(MAX_PATH * 4);
    ofn.lpstrFilter = L"Matroska 视频\0*.mkv\0MP4 视频\0*.mp4\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"输出文件";
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn)) SetWindowTextW(g_edtOut, file);
}

void Layout(HWND parent)
{
    // 输入
    g_edtFile = MakeEdit(parent, 32, 122, 536, 27, ID_EDT_FILE, ES_AUTOHSCROLL);
    MakeButton(parent, L"浏览...", 578, 121, 90, 29, ID_BTN_BROWSE, K_SECONDARY, g_fontUI);

    // 选项
    g_cbPass = MakeCombo(parent, 88, 218, 104, 120, ID_CB_PASS);
    SendMessageW(g_cbPass, CB_ADDSTRING, 0, (LPARAM)L"1 遍（默认）");
    SendMessageW(g_cbPass, CB_ADDSTRING, 0, (LPARAM)L"2 遍（更细）");
    SendMessageW(g_cbPass, CB_ADDSTRING, 0, (LPARAM)L"3 遍（最慢）");
    SendMessageW(g_cbPass, CB_SETCURSEL, 0, 0);

    g_cbFmt = MakeCombo(parent, 264, 218, 88, 100, ID_CB_FMT);
    SendMessageW(g_cbFmt, CB_ADDSTRING, 0, (LPARAM)L"MKV");
    SendMessageW(g_cbFmt, CB_ADDSTRING, 0, (LPARAM)L"MP4");
    SendMessageW(g_cbFmt, CB_SETCURSEL, 0, 0);

    g_rbSame = MakeButton(parent, L"源视频旁边，加 -nr", 416, 220, 168, 24,
                          ID_RB_SAME, K_RADIO, g_fontUI, true);
    g_rbPick = MakeButton(parent, L"指定文件", 588, 220, 92, 24,
                          ID_RB_PICK, K_RADIO, g_fontUI);
    SendMessageW(g_rbSame, BM_SETCHECK, BST_CHECKED, 0);

    g_edtOut = MakeEdit(parent, 32, 252, 544, 27, ID_EDT_OUT, ES_AUTOHSCROLL);
    g_btnOut = MakeButton(parent, L"选择...", 584, 251, 84, 29, ID_BTN_OUT, K_SECONDARY, g_fontUI);

    // 主按钮
    g_btnStart = MakeButton(parent, L"开始转换", 16, 300, 668, 48,
                            ID_BTN_START, K_PRIMARY, g_fontBig);

    // 进度
    g_lblPct = MakeLabelCtl(parent, L"", 612, 361, 72, 22, C_TEXT, g_brBg);

    // 日志
    g_edtLog = MakeEdit(parent, 28, 428, 644, 140, ID_EDT_LOG,
                        WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY);

    // 头部快捷操作
    g_btnVerify = MakeButton(parent, L"校验完整性", 524, 18, 88, 30,
                             ID_BTN_VERIFY, K_GHOST, g_fontSmall);
    g_btnDoc = MakeButton(parent, L"说明", 620, 18, 64, 30,
                          ID_BTN_DOC, K_GHOST, g_fontSmall);

    // 底部
    g_btnOpen = MakeButton(parent, L"打开输出目录", 448, 590, 132, 32,
                           ID_BTN_OPEN, K_SECONDARY, g_fontUI);
    g_btnStop = MakeButton(parent, L"停止", 590, 590, 94, 32,
                           ID_BTN_STOP, K_SECONDARY, g_fontUI);

    SendMessageW(g_edtFile, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(g_edtOut, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
    SendMessageW(g_edtLog, WM_SETFONT, (WPARAM)g_fontLog, TRUE);

    EnableWindow(g_edtOut, FALSE);
    EnableWindow(g_btnOut, FALSE);
    EnableWindow(g_btnOpen, FALSE);
    EnableWindow(g_btnStop, FALSE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        Layout(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_DROPFILES:
    {
        wchar_t file[MAX_PATH * 4] = {};
        if (DragQueryFileW((HDROP)wp, 0, file, (UINT)(MAX_PATH * 4)))
            SetWindowTextW(g_edtFile, file);
        DragFinish((HDROP)wp);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wp;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_brBg);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintChrome(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wp;
        // 用控件 ID 判断而不是 HWND：CreateWindowW 创建编辑框时会同步发
        // WM_CTLCOLOREDIT，那一刻全局 HWND 还没赋值，会走错分支。
        const int id = GetDlgCtrlID((HWND)lp);
        if (id == ID_EDT_LOG)
        {
            SetTextColor(hdc, C_LOG_TX);
            SetBkColor(hdc, C_LOG_BG);
            return (LRESULT)g_brLogBg;
        }
        SetTextColor(hdc, C_FIELD_TX);
        SetBkColor(hdc, C_FIELD);
        return (LRESULT)g_brField;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, C_FIELD_TX);
        SetBkColor(hdc, C_FIELD);
        return (LRESULT)g_brField;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        // 只读编辑框（日志框）按静态控件走这条路发颜色，WM_CTLCOLOREDIT 收不到。
        if (GetDlgCtrlID(ctrl) == ID_EDT_LOG)
        {
            SetTextColor(hdc, C_LOG_TX);
            SetBkColor(hdc, C_LOG_BG);
            return (LRESULT)g_brLogBg;
        }
        for (const auto& l : g_labels)
        {
            if (l.h == ctrl)
            {
                SetTextColor(hdc, l.text);
                SetBkColor(hdc, C_BG);   // 百分比标签坐在窗口底色上
                return (LRESULT)l.bk;
            }
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d && FindBtn(d->hwndItem))
        {
            DrawButton(d);
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
        if (wp == IDT_POLL)
        {
            PumpLog();
            if (g_pi.hProcess && WaitForSingleObject(g_pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                FinishRun();
            }
            else
            {
                const DWORD secs = (GetTickCount() - g_startTick) / 1000;
                SetWindowTextW(hwnd, (std::wstring(APP_NAME) + L" — 已运行 " +
                                      std::to_wstring(secs) + L" 秒").c_str());
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_BTN_BROWSE: PickInput(); return 0;
        case ID_BTN_OUT: PickOutput(); return 0;
        case ID_BTN_START:
            StartRun();
            // 自检模式下起不来也要退出，否则脚本会一直等
            if (g_selftest && !g_running) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case ID_BTN_STOP: StopRun(); return 0;
        case ID_RB_SAME:
        case ID_RB_PICK:
        {
            const bool pick = (LOWORD(wp) == ID_RB_PICK);
            CheckRadioButton(hwnd, ID_RB_SAME, ID_RB_PICK,
                             pick ? ID_RB_PICK : ID_RB_SAME);
            EnableWindow(g_edtOut, pick);
            EnableWindow(g_btnOut, pick);
            RepaintButtons();
            return 0;
        }
        case ID_BTN_OPEN:
        {
            if (!g_lastOutput.empty())
            {
                const std::wstring arg = L"/select,\"" + g_lastOutput + L"\"";
                ShellExecuteW(hwnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOW);
            }
            return 0;
        }
        case ID_BTN_VERIFY:
            ShellExecuteW(hwnd, L"open", (g_dir + L"\\校验完整性.cmd").c_str(),
                          nullptr, nullptr, SW_SHOW);
            return 0;
        case ID_BTN_DOC:
            ShellExecuteW(hwnd, L"open", (g_dir + L"\\README.txt").c_str(),
                          nullptr, nullptr, SW_SHOW);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        if (g_running) StopRun();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int)
{
    // 隐藏开关 --selftest <视频>：诊断用，自动填路径、自动开始、跑完自己退出。
    // 只写日志，不弹框，方便脚本化验证。
    if (cmdLine && *cmdLine)
    {
        const std::wstring cl(cmdLine);
        const std::wstring key(L"--selftest");
        if (cl.size() > key.size() && cl.compare(0, key.size(), key) == 0)
        {
            g_selftest = true;
            g_selftestVideo = TrimQuotes(cl.substr(key.size()));
        }
    }
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
    OleInitialize(nullptr);

    // 路径：exe 在包根目录，引擎在 engine\ 下；兼容没建 engine 的旧布局
    wchar_t self[MAX_PATH + 1] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_dir = std::wstring(self);
    const size_t slash = g_dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) g_dir = g_dir.substr(0, slash);
    g_engine = g_dir + L"\\engine";
    if (GetFileAttributesW(g_engine.c_str()) == INVALID_FILE_ATTRIBUTES) g_engine = g_dir;
    g_assets = g_engine + L"\\amd-nr-assets";
    g_converter = g_engine + L"\\dlss5-convert.exe";
    if (GetFileAttributesW(g_converter.c_str()) == INVALID_FILE_ATTRIBUTES)
        g_converter = g_dir + L"\\dlss5-convert.exe";

    // 诊断日志固定放在 %TEMP%，不往发布包里写文件
    wchar_t tempDir[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH, tempDir);
    g_guiLog = std::wstring(tempDir) + L"HTG-DLSS5-AMD-gui.log";
    const bool hasConv = GetFileAttributesW(g_converter.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool hasAssets =
        GetFileAttributesW((g_assets + L"\\dlssnr-amd").c_str()) != INVALID_FILE_ATTRIBUTES;
    GuiLog(L"==== HTG DLSS5-AMD v1.01 启动 ====");
    GuiLog(L"程序目录: " + g_dir);
    GuiLog(L"引擎目录: " + g_engine);
    GuiLog(L"转换器  : " + g_converter + (hasConv ? L"  [存在]" : L"  [缺失]"));
    GuiLog(L"资产目录: " + g_assets + (hasAssets ? L"  [存在]" : L"  [缺失]"));
    GuiLog(L"命令解释器: " + FindCmdExe());
    if (!hasConv || !hasAssets)
        GuiLog(L"警告: 引擎不完整，开始转换会被拦下并提示");

    g_fontUI = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");
    g_fontBig = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Microsoft YaHei UI");
    g_fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Microsoft YaHei UI");
    g_fontTitle = CreateFontW(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                              L"Microsoft YaHei UI");
    g_fontLog = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    g_brBg = CreateSolidBrush(C_BG);
    g_brHeader = CreateSolidBrush(C_HEADER);
    g_brCard = CreateSolidBrush(C_CARD);
    g_brAccent = CreateSolidBrush(C_ACCENT);
    g_brLogBg = CreateSolidBrush(C_LOG_BG);
    g_brField = CreateSolidBrush(C_FIELD);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // 背景由 WM_ERASEBKGND 自己画
    wc.lpszClassName = L"HtgNrGui";
    // 图标在注册前塞进去：类持有它，进程退出时一起释放
    wc.hIcon = MakeAppIcon(32);
    wc.hIconSm = MakeAppIcon(16);
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, 700, 630 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    const int ww = rc.right - rc.left;
    const int wh = rc.bottom - rc.top;
    const int sx = GetSystemMetrics(SM_CXSCREEN);
    const int sy = GetSystemMetrics(SM_CYSCREEN);
    int px = (sx - ww) / 2; if (px < 0) px = 0;
    int py = (sy - wh) / 4; if (py < 0) py = 0;

    g_hwnd = CreateWindowW(L"HtgNrGui", APP_NAME,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                           px, py, ww, wh,
                           nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // Win10 2004+ 暗色标题栏；Win11 圆角。老系统上调用会失败，无所谓。
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // 引擎缺失时立刻在日志框里说清楚，不用等用户点了开始才发现
    if (!hasConv || !hasAssets)
    {
        AppendLogLine(L"警告：引擎不完整，无法开始转换。");
        if (!hasConv)
            AppendLogLine(L"  找不到转换器：" + g_converter);
        if (!hasAssets)
            AppendLogLine(L"  找不到运行时资产：" + g_assets);
        AppendLogLine(L"  请把整个包（含 engine 目录）一起拷贝，不要只拷 exe。");
        AppendLogLine(L"");
    }

    if (g_selftest)
    {
        GuiLog(L"自检模式: " + g_selftestVideo);
        SetWindowTextW(g_edtFile, g_selftestVideo.c_str());
        PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_BTN_START, BN_CLICKED), 0);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return 0;
}
