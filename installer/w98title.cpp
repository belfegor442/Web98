// w98title.cpp — custom Windows 98 style title bar for the Web98 NSIS
// installer/uninstaller window.
//
// The NSIS window is made borderless (WS_CAPTION|WS_THICKFRAME stripped),
// this DLL subclasses its WNDPROC and paints a Win98 title strip: navy
// horizontal gradient (#000080 -> #1084D0), white bold text + window icon,
// and three classic owner-drawn buttons (min / [disabled] max / close).
// WM_NCHITTEST returns HTCAPTION over the strip so dragging works through
// DefWindowProc.
//
// Built automatically by scripts\package.ps1 (x86 — NSIS installers are
// 32-bit processes):
//   cl /nologo /LD /O2 /W3 /DUNICODE /D_UNICODE w98title.cpp
//      /Fo:%TEMP%\w98title.obj /Fe:<Release>\w98title.dll
//      /link /NOIMPLIB user32.lib gdi32.lib msimg32.lib

#define WIN32_LEAN_AND_MEAN
#define WINVER    0x0601
#define _WIN32_WINNT 0x0601
#include <windows.h>

// x86 stdcall exports are decorated (_Name@N); publish undecorated aliases so
// NSIS System::Call '"...w98title.dll"::Name' / GetProcAddress("Name") work.
#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:W98TitleAttach=_W98TitleAttach@4")
#pragma comment(linker, "/EXPORT:W98TitleStripH=_W98TitleStripH@0")
#endif

#define ID_MIN   9801
#define ID_MAX   9802
#define ID_CLOSE 9803

static WNDPROC  g_orig     = NULL;
static HWND     g_hwnd     = NULL;
static HWND     g_btnMin   = NULL;
static HWND     g_btnMax   = NULL;
static HWND     g_btnClose = NULL;
static HFONT    g_font     = NULL;
static UINT     g_dpi      = 96;
static int      g_stripH   = 0;
static int      g_bw = 0, g_bh = 0, g_gap = 0;
static BOOL     g_active   = TRUE;

static int S(int v)
{
  return MulDiv(v, (int)g_dpi, 96);
}

static void Layout(HWND hwnd)
{
  RECT rc;
  if (!g_btnMin) return;
  GetClientRect(hwnd, &rc);
  const int y = (g_stripH - g_bh) / 2;
  const int m = S(2);
  int x = rc.right - m - g_bw;
  SetWindowPos(g_btnClose, NULL, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  x -= (g_bw + g_gap);
  SetWindowPos(g_btnMax, NULL, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  x -= (g_bw + g_gap);
  SetWindowPos(g_btnMin, NULL, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void InvalidateStrip(HWND hwnd)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  rc.bottom = g_stripH;
  InvalidateRect(hwnd, &rc, TRUE);
}

static void PaintStrip(HWND hwnd)
{
  if (!g_hwnd || g_stripH <= 0) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const int w = rc.right, h = g_stripH;
  if (w <= 0) return;

  HDC dc = GetDC(hwnd);
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
  HGDIOBJ oldBmp = SelectObject(mem, bmp);

  // horizontal gradient, active (navy->blue) or inactive (grays)
  TRIVERTEX v[2];
  v[0].x = 0;  v[0].y = 0;  v[0].Alpha = 0;
  v[1].x = w;  v[1].y = h;  v[1].Alpha = 0;
  if (g_active) {
    v[0].Red = 0x0000; v[0].Green = 0x0000; v[0].Blue = 0x8000;
    v[1].Red = 0x1000; v[1].Green = 0x8400; v[1].Blue = 0xD000;
  } else {
    v[0].Red = 0x8080; v[0].Green = 0x8080; v[0].Blue = 0x8080;
    v[1].Red = 0xA0A0; v[1].Green = 0xA0A0; v[1].Blue = 0xA0A0;
  }
  GRADIENT_RECT gr = {0, 1};
  GradientFill(mem, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);

  int tx = S(4);
  HICON hi = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
  if (!hi) hi = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
  if (!hi) hi = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
  if (hi) {
    const int isz = S(16);
    DrawIconEx(mem, tx, (h - isz) / 2, hi, isz, isz, 0, NULL, DI_NORMAL);
    tx += isz + S(5);
  }

  wchar_t title[160];
  GetWindowTextW(hwnd, title, 160);
  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, g_active ? RGB(255, 255, 255) : RGB(224, 224, 224));
  HFONT oldFont = NULL;
  if (g_font) oldFont = (HFONT)SelectObject(mem, g_font);
  RECT tr;
  tr.left = tx; tr.top = 0;
  tr.right = w - (g_bw * 3 + g_gap * 3) - S(6); tr.bottom = h;
  if (tr.right > tr.left) {
    DrawTextW(mem, title, -1, &tr,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  if (oldFont) SelectObject(mem, oldFont);

  BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldBmp);
  DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(hwnd, dc);
}

// classic raised/sunken Win98 button face + glyph
static void DrawBtn(const DRAWITEMSTRUCT* ds)
{
  HDC dc = ds->hDC;
  RECT rc = ds->rcItem;
  const BOOL sel = (ds->itemState & ODS_SELECTED) != 0;
  const BOOL dis = (ds->itemState & ODS_DISABLED) != 0;
  const int off = sel ? 1 : 0;
  const int cx = (rc.left + rc.right) / 2 + off;
  const int cy = (rc.top + rc.bottom) / 2 + off;

  HBRUSH face = CreateSolidBrush(RGB(192, 192, 192));
  FillRect(dc, &rc, face);
  DeleteObject(face);
  if (dis)
    DrawEdge(dc, &rc, BDR_SUNKENOUTER, BF_RECT | BF_FLAT);
  else if (sel)
    DrawEdge(dc, &rc, BDR_SUNKENOUTER | BDR_SUNKENINNER, BF_RECT);
  else
    DrawEdge(dc, &rc, BDR_RAISEDOUTER | BDR_RAISEDINNER, BF_RECT);

  const COLORREF cl = dis ? RGB(140, 140, 140) : RGB(0, 0, 0);

  if (ds->CtlID == ID_MIN) {
    RECT bar = {cx - S(4), cy + S(3), cx + S(4), cy + S(4)};
    if (bar.bottom <= bar.top) bar.bottom = bar.top + 1;
    HBRUSH b = CreateSolidBrush(cl);
    FillRect(dc, &bar, b);
    DeleteObject(b);
  } else if (ds->CtlID == ID_MAX) {
    const int s = S(4);
    HPEN pen = CreatePen(PS_SOLID, 1, cl);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, cx - s, cy - s, cx + s, cy + s);
    RECT top = {cx - s, cy - s, cx + s, cy - s + S(2)};
    HBRUSH b = CreateSolidBrush(cl);
    FillRect(dc, &top, b);
    DeleteObject(b);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  } else { // ID_CLOSE: X
    const int s = S(4);
    HPEN pen = CreatePen(PS_SOLID, 1, cl);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, cx - s, cy - s, NULL);
    LineTo(dc, cx + s + 1, cy + s + 1);
    MoveToEx(dc, cx + s, cy - s, NULL);
    LineTo(dc, cx - s - 1, cy + s + 1);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  }
}

static HWND MakeBtn(HWND parent, int id, BOOL enabled)
{
  HWND b = CreateWindowExW(0, L"Button", L"",
                           WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                           0, 0, g_bw, g_bh, parent,
                           (HMENU)(UINT_PTR)id, GetModuleHandleW(NULL), NULL);
  if (b && !enabled) EnableWindow(b, FALSE);
  return b;
}

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  if (!g_orig) return DefWindowProcW(hwnd, msg, wp, lp);

  switch (msg) {
  case WM_NCHITTEST: {
    LRESULT r = CallWindowProcW(g_orig, hwnd, msg, wp, lp);
    POINT pt;
    pt.x = (short)LOWORD(lp);
    pt.y = (short)HIWORD(lp);
    ScreenToClient(hwnd, &pt);
    if (pt.y >= 0 && pt.y < g_stripH) return HTCAPTION;
    return r;
  }

  // drag must go straight to DefWindowProc — NSIS's dialog proc may swallow
  // unhandled NC messages before they reach it.
  case WM_NCLBUTTONDOWN:
    if (wp == HTCAPTION) return DefWindowProcW(hwnd, msg, wp, lp);
    break;
  case WM_NCLBUTTONDBLCLK:
    if (wp == HTCAPTION) return DefWindowProcW(hwnd, msg, wp, lp);
    break;

  case WM_PAINT: {
    LRESULT r = CallWindowProcW(g_orig, hwnd, msg, wp, lp);
    PaintStrip(hwnd);
    return r;
  }

  case WM_DRAWITEM: {
    const DRAWITEMSTRUCT* ds = (const DRAWITEMSTRUCT*)lp;
    if (ds->CtlType == ODT_BUTTON &&
        (ds->CtlID == ID_MIN || ds->CtlID == ID_MAX || ds->CtlID == ID_CLOSE)) {
      DrawBtn(ds);
      return TRUE;
    }
    break;
  }

  case WM_COMMAND:
    if (HIWORD(wp) == BN_CLICKED) {
      switch (LOWORD(wp)) {
      case ID_MIN:
        ShowWindow(hwnd, SW_MINIMIZE);
        return 0;
      case ID_MAX:
        return 0; // intentionally disabled
      case ID_CLOSE:
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }
    }
    break;

  case WM_SIZE: {
    LRESULT r = CallWindowProcW(g_orig, hwnd, msg, wp, lp);
    Layout(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);
    return r;
  }

  case WM_NCACTIVATE:
    g_active = (wp != 0);
    InvalidateStrip(hwnd);
    return CallWindowProcW(g_orig, hwnd, msg, wp, lp);

  case WM_ACTIVATE:
    g_active = (LOWORD(wp) != WA_INACTIVE);
    InvalidateStrip(hwnd);
    break;

  case WM_DESTROY: {
    LRESULT r = CallWindowProcW(g_orig, hwnd, msg, wp, lp);
    g_orig = NULL;
    g_hwnd = NULL;
    g_btnMin = g_btnMax = g_btnClose = NULL;
    if (g_font) {
      DeleteObject(g_font);
      g_font = NULL;
    }
    return r;
  }
  }

  return CallWindowProcW(g_orig, hwnd, msg, wp, lp);
}

// Called by NSIS (.onGUIInit / un.onGUIInit). Returns 1 on success.
extern "C" __declspec(dllexport) int __stdcall W98TitleAttach(HWND hwnd)
{
  if (!hwnd) return 0;
  if (g_orig && g_hwnd == hwnd) return 1;
  if (g_orig) return 0; // one window at a time

  g_hwnd = hwnd;
  g_dpi = 96;
  typedef UINT (WINAPI *GetDpiFn)(HWND);
  HMODULE u = GetModuleHandleW(L"user32.dll");
  if (u) {
    GetDpiFn f = (GetDpiFn)GetProcAddress(u, "GetDpiForWindow");
    if (f) {
      UINT d = f(hwnd);
      if (d) g_dpi = d;
    }
  }

  g_stripH = S(22);
  g_bw = S(16);
  g_bh = S(14);
  g_gap = S(2);

  g_font = CreateFontW(-MulDiv(8, (int)g_dpi, 72), 0, 0, 0, FW_BOLD,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"MS Sans Serif");

  LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
  SetWindowLongW(hwnd, GWL_STYLE, style);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
               SWP_FRAMECHANGED);

  g_orig = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
  if (!g_orig) {
    g_hwnd = NULL;
    if (g_font) { DeleteObject(g_font); g_font = NULL; }
    return 0;
  }

  g_btnMin   = MakeBtn(hwnd, ID_MIN, TRUE);
  g_btnMax   = MakeBtn(hwnd, ID_MAX, FALSE); // maximized layout not supported
  g_btnClose = MakeBtn(hwnd, ID_CLOSE, TRUE);
  Layout(hwnd);
  InvalidateRect(hwnd, NULL, TRUE);
  return 1;
}

// Strip height in client pixels (0 until attached) — NSIS layout must match.
extern "C" __declspec(dllexport) int __stdcall W98TitleStripH(void)
{
  return g_stripH;
}
