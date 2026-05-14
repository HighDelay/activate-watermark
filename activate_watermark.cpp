#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

struct WatermarkSettings {
    std::wstring line1 = L"Activate Windows";
    std::wstring line2 = L"Go to Settings to activate Windows.";
    float fontSize1    = 22.0f;
    float fontSize2    = 17.0f;
    int   margin       = 40;
    int   marginBottom = 80;
    float lineSpacing  = 2.0f;
    BYTE  alpha        = 130;
    int   position     = 0;
};

static WatermarkSettings g_settings;
static ULONG_PTR         g_gdiplusToken;
static NOTIFYICONDATAW   g_nid = {};
static HWND              g_hwnd = NULL;

enum MenuID {
    ID_EXIT = 1001,
    ID_POS_BOTTOM_RIGHT, ID_POS_BOTTOM_LEFT, ID_POS_TOP_RIGHT, ID_POS_TOP_LEFT,
    ID_OPACITY_25, ID_OPACITY_50, ID_OPACITY_75, ID_OPACITY_100,
    ID_FONT_SMALL, ID_FONT_MEDIUM, ID_FONT_LARGE, ID_FONT_XLARGE,
    ID_EDIT_LINE1, ID_EDIT_LINE2,
};

#define WM_TRAYICON (WM_USER + 1)

static std::wstring g_dlgTitle;
static std::wstring g_dlgValue;

static INT_PTR CALLBACK EditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowTextW(hDlg, g_dlgTitle.c_str());
        SetDlgItemTextW(hDlg, 101, g_dlgValue.c_str());
        SendDlgItemMessageW(hDlg, 101, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[512] = {};
            GetDlgItemTextW(hDlg, 101, buf, 511);
            g_dlgValue = buf;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static INT_PTR ShowEditDialog(HWND parent, const wchar_t* title, std::wstring& value) {
    g_dlgTitle = title;
    g_dlgValue = value;

    alignas(4) BYTE buf[1024] = {};
    DLGTEMPLATE* dlg = (DLGTEMPLATE*)buf;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dlg->cdit = 3;
    dlg->cx = 220;
    dlg->cy = 55;

    WORD* p = (WORD*)(dlg + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;

    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    DLGITEMTEMPLATE* item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    item->x = 8; item->y = 8; item->cx = 204; item->cy = 14;
    item->id = 101;
    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0081;
    *p++ = 0; *p++ = 0;

    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON;
    item->x = 60; item->y = 30; item->cx = 45; item->cy = 14;
    item->id = IDOK;
    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    *p++ = L'O'; *p++ = L'K'; *p++ = 0;
    *p++ = 0;

    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    item->x = 115; item->y = 30; item->cx = 45; item->cy = 14;
    item->id = IDCANCEL;
    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    *p++ = L'C'; *p++ = L'a'; *p++ = L'n'; *p++ = L'c'; *p++ = L'e'; *p++ = L'l'; *p++ = 0;
    *p++ = 0;

    INT_PTR result = DialogBoxIndirectW(GetModuleHandle(NULL), dlg, parent, EditDlgProc);
    if (result == IDOK) value = g_dlgValue;
    return result;
}

static void UpdateLayeredWatermark(HWND hwnd) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int canvasW = 800, canvasH = 120;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = canvasW;
    bmi.bmiHeader.biHeight = -canvasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    memset(pBits, 0, canvasW * canvasH * 4);

    float maxW, totalH;
    {
        Graphics graphics(hdcMem);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);

        FontFamily fontFamily(L"Segoe UI");
        Font font1(&fontFamily, g_settings.fontSize1, FontStyleRegular, UnitPixel);
        Font font2(&fontFamily, g_settings.fontSize2, FontStyleRegular, UnitPixel);
        SolidBrush brush(Color(g_settings.alpha, 255, 255, 255));

        RectF bounds1, bounds2;
        graphics.MeasureString(g_settings.line1.c_str(), -1, &font1, PointF(0, 0), &bounds1);
        graphics.MeasureString(g_settings.line2.c_str(), -1, &font2, PointF(0, 0), &bounds2);

        totalH = bounds1.Height + g_settings.lineSpacing + bounds2.Height;
        maxW = max(bounds1.Width, bounds2.Width);

        graphics.DrawString(g_settings.line1.c_str(), -1, &font1, PointF(0, 0), &brush);
        graphics.DrawString(g_settings.line2.c_str(), -1, &font2, PointF(0, bounds1.Height + g_settings.lineSpacing), &brush);
    }

    int winW = (int)(maxW + 8);
    int winH = (int)(totalH + 4);
    int winX, winY;
    int m = g_settings.margin, mb = g_settings.marginBottom;

    switch (g_settings.position) {
    case 0: winX = screenW - winW - m; winY = screenH - winH - mb; break;
    case 1: winX = m;                  winY = screenH - winH - mb; break;
    case 2: winX = screenW - winW - m; winY = m;                   break;
    case 3: winX = m;                  winY = m;                   break;
    default: winX = screenW - winW - m; winY = screenH - winH - mb; break;
    }

    POINT ptSrc = { 0, 0 };
    SIZE sizeWnd = { winW, winH };
    POINT ptDst = { winX, winY };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    SetWindowPos(hwnd, HWND_TOPMOST, winX, winY, winW, winH, SWP_NOACTIVATE);
    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &sizeWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

static void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    wcscpy_s(g_nid.szTip, L"Activate Windows Watermark");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();

    HMENU hPosMenu = CreatePopupMenu();
    AppendMenuW(hPosMenu, MF_STRING | (g_settings.position == 0 ? MF_CHECKED : 0), ID_POS_BOTTOM_RIGHT, L"Bottom Right");
    AppendMenuW(hPosMenu, MF_STRING | (g_settings.position == 1 ? MF_CHECKED : 0), ID_POS_BOTTOM_LEFT,  L"Bottom Left");
    AppendMenuW(hPosMenu, MF_STRING | (g_settings.position == 2 ? MF_CHECKED : 0), ID_POS_TOP_RIGHT,    L"Top Right");
    AppendMenuW(hPosMenu, MF_STRING | (g_settings.position == 3 ? MF_CHECKED : 0), ID_POS_TOP_LEFT,     L"Top Left");

    HMENU hAlphaMenu = CreatePopupMenu();
    AppendMenuW(hAlphaMenu, MF_STRING | (g_settings.alpha == 64  ? MF_CHECKED : 0), ID_OPACITY_25,  L"25%");
    AppendMenuW(hAlphaMenu, MF_STRING | (g_settings.alpha == 130 ? MF_CHECKED : 0), ID_OPACITY_50,  L"50%  (default)");
    AppendMenuW(hAlphaMenu, MF_STRING | (g_settings.alpha == 191 ? MF_CHECKED : 0), ID_OPACITY_75,  L"75%");
    AppendMenuW(hAlphaMenu, MF_STRING | (g_settings.alpha == 255 ? MF_CHECKED : 0), ID_OPACITY_100, L"100%");

    HMENU hFontMenu = CreatePopupMenu();
    auto fs = [](float s) { return g_settings.fontSize1 == s; };
    AppendMenuW(hFontMenu, MF_STRING | (fs(16.0f) ? MF_CHECKED : 0), ID_FONT_SMALL,  L"Small");
    AppendMenuW(hFontMenu, MF_STRING | (fs(22.0f) ? MF_CHECKED : 0), ID_FONT_MEDIUM, L"Medium  (default)");
    AppendMenuW(hFontMenu, MF_STRING | (fs(30.0f) ? MF_CHECKED : 0), ID_FONT_LARGE,  L"Large");
    AppendMenuW(hFontMenu, MF_STRING | (fs(40.0f) ? MF_CHECKED : 0), ID_FONT_XLARGE, L"Extra Large");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPosMenu,   L"Position");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAlphaMenu,  L"Opacity");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFontMenu,   L"Font Size");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_EDIT_LINE1, L"Edit Line 1...");
    AppendMenuW(hMenu, MF_STRING, ID_EDIT_LINE2, L"Edit Line 2...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        UpdateLayeredWatermark(hwnd);
        SetTimer(hwnd, 1, 5000, NULL);
        return 0;

    case WM_TIMER:
    case WM_DISPLAYCHANGE:
        UpdateLayeredWatermark(hwnd);
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_EXIT: DestroyWindow(hwnd); break;

        case ID_POS_BOTTOM_RIGHT: g_settings.position = 0; UpdateLayeredWatermark(hwnd); break;
        case ID_POS_BOTTOM_LEFT:  g_settings.position = 1; UpdateLayeredWatermark(hwnd); break;
        case ID_POS_TOP_RIGHT:    g_settings.position = 2; UpdateLayeredWatermark(hwnd); break;
        case ID_POS_TOP_LEFT:     g_settings.position = 3; UpdateLayeredWatermark(hwnd); break;

        case ID_OPACITY_25:  g_settings.alpha = 64;  UpdateLayeredWatermark(hwnd); break;
        case ID_OPACITY_50:  g_settings.alpha = 130; UpdateLayeredWatermark(hwnd); break;
        case ID_OPACITY_75:  g_settings.alpha = 191; UpdateLayeredWatermark(hwnd); break;
        case ID_OPACITY_100: g_settings.alpha = 255; UpdateLayeredWatermark(hwnd); break;

        case ID_FONT_SMALL:  g_settings.fontSize1 = 16.0f; g_settings.fontSize2 = 13.0f; UpdateLayeredWatermark(hwnd); break;
        case ID_FONT_MEDIUM: g_settings.fontSize1 = 22.0f; g_settings.fontSize2 = 17.0f; UpdateLayeredWatermark(hwnd); break;
        case ID_FONT_LARGE:  g_settings.fontSize1 = 30.0f; g_settings.fontSize2 = 23.0f; UpdateLayeredWatermark(hwnd); break;
        case ID_FONT_XLARGE: g_settings.fontSize1 = 40.0f; g_settings.fontSize2 = 30.0f; UpdateLayeredWatermark(hwnd); break;

        case ID_EDIT_LINE1:
            if (ShowEditDialog(hwnd, L"Edit Line 1", g_settings.line1) == IDOK) UpdateLayeredWatermark(hwnd);
            break;
        case ID_EDIT_LINE2:
            if (ShowEditDialog(hwnd, L"Edit Line 2", g_settings.line2) == IDOK) UpdateLayeredWatermark(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ActivateWatermark";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"ActivateWatermark", L"", WS_POPUP,
        0, 0, 1, 1, NULL, NULL, hInstance, NULL);

    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
