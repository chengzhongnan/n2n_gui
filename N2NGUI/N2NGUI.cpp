// N2NGUI.cpp : 定義應用程序的入口點。
// 整合了 n2n 核心功能和一個完整的 Win32 UI。

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "framework.h"
#include "N2NGUI.h"
#include <windows.h>
#include <process.h>    // For _beginthreadex
#include <vector>
#include <string>
#include <memory>

// 包含n2n的頭文件，並告訴C++编译器它是一個C函数
extern "C" {
    // 假設 n2n.h 是您放置回調函數聲明的地方
    // 如果不是，請修改為正確的頭文件
 #include "./n2n/n2n.h" 

     int n2n_main(int argc, char** argv); // 我們重命名後的main函數
}

#define MAX_LOADSTRING 100
#define WM_APPEND_LOG (WM_APP + 1) // 自定義消息，用於跨線程更新日誌

// --- 全局變量 ---
HINSTANCE hInst;                                // 當前實例
CHAR szTitle[MAX_LOADSTRING];                  // 標題欄文本
CHAR szWindowClass[MAX_LOADSTRING];            // 主窗口類名

static HFONT g_hFont = NULL;

const wchar_t* g_szRegKey = L"Software\\N2NGUI_App\\Settings"; // 子機碼路徑
const wchar_t* g_szDeviceValue = L"DeviceName";
const wchar_t* g_szAddressValue = L"DeviceAddress";
const wchar_t* g_szCommunityValue = L"Community";
const wchar_t* g_szKeyValue = L"Key";
const wchar_t* g_szSupernodeValue = L"Supernode";

// UI 控件句柄
HWND hEditDevice, hEditAddress, hEditCommunity, hEditKey, hEditSupernode;
HWND hStartButton, hStopButton, hLogEdit;

// 線程管理
HANDLE hN2NThread = NULL;                       // n2n工作線程的句柄
volatile bool g_is_n2n_running = false;         // 控制n2n循環的全局標誌

// --- 此代碼模塊中包含的函數的前向聲明 ---
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void SaveSettingsToRegistry(HWND hWnd);
void LoadSettingsFromRegistry(HWND hWnd);

// --- 線程與回調函數 ---

void GuiLogCallback(const char* log_message, int len)
{
    if (hLogEdit && IsWindow(hLogEdit)) 
    {
        // 使用 len 精确分配内存，防止 log_message 没有 \0 结尾导致读取越界
        char* msgCopy = (char*)malloc(len + 1);
        if (msgCopy) 
        {
            memcpy(msgCopy, log_message, len);
            msgCopy[len] = '\0'; // 手动加上字符串结束符

            // 关键修复：如果消息队列满了导致 PostMessage 失败，必须立刻释放内存！
            if (!PostMessage(GetParent(hLogEdit), WM_APPEND_LOG, 0, (LPARAM)msgCopy)) 
            {
                free(msgCopy);
            }
        }
    }
}

// 這是n2n工作線程的入口函數
unsigned __stdcall N2NThreadFunc(void* pArguments)
{
    // 使用智能指針接管，這樣無論函數從哪裡退出，都會自動 delete args
    std::unique_ptr<std::vector<std::string>> args(static_cast<std::vector<std::string>*>(pArguments));

    std::vector<char*> argv_vec;
    for (const auto& s : *args) {
        argv_vec.push_back((char*)s.c_str());
    }

    g_is_n2n_running = true;
    n2n_set_log_callback(GuiLogCallback);
    n2n_start(NULL);

    int result = n2n_main(argv_vec.size(), argv_vec.data());

    // --- 不需要手動 delete args 了，智能指針會幫你做 ---

    g_is_n2n_running = false;
    PostMessage(GetParent(hLogEdit), WM_COMMAND, MAKEWPARAM(IDC_BUTTON_STOP, BN_CLICKED), (LPARAM)TRUE);

    return result;
}


// --- 主程序入口 ---
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_N2NGUI, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_N2NGUI));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

// --- 窗口類註冊 ---
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_N2NGUI));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); // 標準對話框背景色
    wcex.lpszMenuName = MAKEINTRESOURCE(IDC_N2NGUI);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

// --- 實例初始化與窗口創建 ---
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    // 創建一個固定大小的窗口
    HWND hWnd = CreateWindowA(szWindowClass, szTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // 固定大小樣式
        CW_USEDEFAULT, 0, 800, 500, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// --- 主窗口消息處理過程 ---
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        // --- 1. 建立現代化的 UI 字型 ---
        // 刪除舊的: HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // 使用 SystemParametersInfo 獲取系統推薦的 UI 字型資訊
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(NONCLIENTMETRICSW);

        // 這個 API 呼叫會填寫 ncm 結構，其中包含了 menus, title bars 等的字型資訊
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0)) {
            // 從獲取的資訊中建立字型。lfMessageFont 是適用於對話方塊和 UI 的字型
            g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        }
        else {
            // 如果 API 呼叫失敗，則退回使用預設 GUI 字型
            g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }

        // --- 創建所有UI控件 (這部分不變) ---
        int current_y = 10;
        int label_width = 100;
        int edit_width = 200;
        int vertical_spacing = 30;

        CreateWindowW(L"STATIC", L"设备名 (-d):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, current_y + 3, label_width, 20, hWnd, (HMENU)IDC_STATIC_DEVICE, hInst, NULL);
        hEditDevice = CreateWindowW(L"EDIT", L"edge0", WS_BORDER | WS_CHILD | WS_VISIBLE, 10 + label_width + 5, current_y, edit_width, 20, hWnd, (HMENU)IDC_EDIT_DEVICE, hInst, NULL);
        current_y += vertical_spacing;

        CreateWindowW(L"STATIC", L"设备地址 (-a):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, current_y + 3, label_width, 20, hWnd, (HMENU)IDC_STATIC_ADDRESS, hInst, NULL);
        hEditAddress = CreateWindowW(L"EDIT", L"10.0.0.140", WS_BORDER | WS_CHILD | WS_VISIBLE, 10 + label_width + 5, current_y, edit_width, 20, hWnd, (HMENU)IDC_EDIT_ADDRESS, hInst, NULL);
        current_y += vertical_spacing;

        CreateWindowW(L"STATIC", L"社区名 (-c):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, current_y + 3, label_width, 20, hWnd, (HMENU)IDC_STATIC_COMMUNITY, hInst, NULL);
        hEditCommunity = CreateWindowW(L"EDIT", L"nan", WS_BORDER | WS_CHILD | WS_VISIBLE, 10 + label_width + 5, current_y, edit_width, 20, hWnd, (HMENU)IDC_EDIT_COMMUNITY, hInst, NULL);
        current_y += vertical_spacing;

        CreateWindowW(L"STATIC", L"加密密钥 (-k):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, current_y + 3, label_width, 20, hWnd, (HMENU)IDC_STATIC_KEY, hInst, NULL);
        hEditKey = CreateWindowW(L"EDIT", L"aliyunservices", WS_BORDER | WS_CHILD | WS_VISIBLE, 10 + label_width + 5, current_y, edit_width, 20, hWnd, (HMENU)IDC_EDIT_KEY, hInst, NULL);
        current_y += vertical_spacing;

        CreateWindowW(L"STATIC", L"超级节点 (-l):", WS_CHILD | WS_VISIBLE | SS_RIGHT, 10, current_y + 3, label_width, 20, hWnd, (HMENU)IDC_STATIC_SUPERNODE, hInst, NULL);
        hEditSupernode = CreateWindowW(L"EDIT", L"8.137.61.152:819", WS_BORDER | WS_CHILD | WS_VISIBLE, 10 + label_width + 5, current_y, edit_width, 20, hWnd, (HMENU)IDC_EDIT_SUPERNODE, hInst, NULL);
        current_y += vertical_spacing + 10;

        const int panel_start_x = 10; // 左側面板的起始 X 座標
        const int panel_width = label_width + 5 + edit_width; // 左側面板的總寬度 (100 + 5 + 200 = 305)
        const int button_width = 100; // 每個按鈕的寬度
        const int button_height = 30; // 每個按鈕的高度
        const int num_buttons = 2;

        // 2. 計算總剩餘空間和每個間距的大小
        // 總空間減去所有按鈕佔用的空間，就是留給間距的總空間
        const int total_gap_space = panel_width - (num_buttons * button_width);
        // 有2個按鈕，所以會有3個間距 (按鈕左側、兩按鈕之間、按鈕右側)
        const int gap_size = total_gap_space / (num_buttons + 1);

        // 3. 計算每個按鈕的 X 座標
        const int button1_x = panel_start_x + gap_size;
        const int button2_x = button1_x + button_width + gap_size;

        // 4. 使用計算出的新座標來創建按鈕
        hStartButton = CreateWindowW(L"BUTTON", L"启动 Edge", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            button1_x, current_y, button_width, button_height,
            hWnd, (HMENU)IDC_BUTTON_START, hInst, NULL);

        hStopButton = CreateWindowW(L"BUTTON", L"停止 Edge", WS_TABSTOP | WS_VISIBLE | WS_CHILD,
            button2_x, current_y, button_width, button_height,
            hWnd, (HMENU)IDC_BUTTON_STOP, hInst, NULL);

        EnableWindow(hStopButton, FALSE);

        hLogEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 330, 10, 440, 390, hWnd, (HMENU)IDC_EDIT_LOG, hInst, NULL);

        // --- 2. 將新建立的字型應用到所有控件 ---
        // 注意: 這裡的變數名從 hFont 改為 g_hFont
        SendMessage(hEditDevice, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hEditAddress, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hEditCommunity, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hEditKey, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hEditSupernode, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hStartButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hStopButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(hLogEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 同時將此新字型也應用到標籤 (STATIC) 控件上
        // 需要獲取視窗的所有子視窗並判斷類型
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            WCHAR className[256];
            GetClassNameW(hChild, className, 256);
            if (wcscmp(className, L"Static") == 0) {
                SendMessage(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            }
            return TRUE;
            }, (LPARAM)g_hFont);

        LoadSettingsFromRegistry(hWnd);

        break;
    }
    case WM_COMMAND:
    {
        // ... 您原有的 WM_COMMAND 邏輯保持不變 ...
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;

        case IDC_BUTTON_START:
        {
            auto* args = new std::vector<std::string>();
            char buffer[512];
            args->push_back("edge");
            GetWindowTextA(hEditDevice, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) { args->push_back("-d"); args->push_back(buffer); }
            GetWindowTextA(hEditAddress, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) { args->push_back("-a"); args->push_back(buffer); }
            GetWindowTextA(hEditCommunity, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) { args->push_back("-c"); args->push_back(buffer); }
            GetWindowTextA(hEditKey, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) { args->push_back("-k"); args->push_back(buffer); }
            GetWindowTextA(hEditSupernode, buffer, sizeof(buffer));
            if (strlen(buffer) > 0) { args->push_back("-l"); args->push_back(buffer); }
            else {
                MessageBox(hWnd, "超級節點地址(-l)不能為空。", "錯誤", MB_OK | MB_ICONERROR);
                delete args;
                break;
            }
            EnableWindow(hStartButton, FALSE);
            EnableWindow(hStopButton, TRUE);
            EnableWindow(hEditDevice, FALSE);
            EnableWindow(hEditAddress, FALSE);
            EnableWindow(hEditCommunity, FALSE);
            EnableWindow(hEditKey, FALSE);
            EnableWindow(hEditSupernode, FALSE);
            SetWindowText(hLogEdit, "");
            hN2NThread = (HANDLE)_beginthreadex(NULL, 0, &N2NThreadFunc, args, 0, NULL);
            break;
        }
        case IDC_BUTTON_STOP:
        {
            bool triggeredByThread = (lParam != 0);
            if (g_is_n2n_running) {
                SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)L"\r\n### 正在請求停止... ###\r\n");
                g_is_n2n_running = false;
                n2n_stop(NULL);
            }
            if (triggeredByThread) {
                SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)L"\r\n### n2n 核心已停止 ###\r\n");
                if (hN2NThread) CloseHandle(hN2NThread);
                hN2NThread = NULL;
            }
            EnableWindow(hStartButton, TRUE);
            EnableWindow(hStopButton, FALSE);
            EnableWindow(hEditDevice, TRUE);
            EnableWindow(hEditAddress, TRUE);
            EnableWindow(hEditCommunity, TRUE);
            EnableWindow(hEditKey, TRUE);
            EnableWindow(hEditSupernode, TRUE);
            break;
        }
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_APPEND_LOG:
    {
        char* msg = (char*)lParam;

        // --- 核心防卡顿机制：限制日志框的最大字符数 ---
        int len = GetWindowTextLength(hLogEdit);
        if (len > 30000) 
        { 
            // 如果文本超过约 3万 字符
            SendMessage(hLogEdit, EM_SETSEL, 0, -1); // 全选
            SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)L"--- 日志过长，自动清空 ---\r\n");
            len = GetWindowTextLength(hLogEdit); // 重新获取长度
        }

        // 追加新日志
        SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(hLogEdit, EM_REPLACESEL, 0, (LPARAM)msg);
        SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)L"\r\n");

        free(msg); // 正常释放
        break;
    }
    case WM_DESTROY:
    {
        SaveSettingsToRegistry(hWnd);

        if (g_hFont) {
            DeleteObject(g_hFont);
        }

        if (g_is_n2n_running) {
            g_is_n2n_running = false;
            if (hN2NThread) {
                WaitForSingleObject(hN2NThread, 1000);
                CloseHandle(hN2NThread);
            }
        }

        // --- 关键清理：把积压在消息队列中没处理完的日志消息全部释放 ---
        MSG peekMsg;
        while (PeekMessage(&peekMsg, hWnd, WM_APPEND_LOG, WM_APPEND_LOG, PM_REMOVE)) 
        {
            free((char*)peekMsg.lParam);
        }

        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// “關於”框的消息處理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void SaveSettingsToRegistry(HWND hWnd)
{
    HKEY hKey;
    // RegCreateKeyEx 會打開一個現有的機碼，如果不存在則會建立它
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_szRegKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        wchar_t buffer[512];

        // 儲存每個編輯框的內容
        GetWindowTextW(hEditDevice, buffer, 512);
        RegSetValueExW(hKey, g_szDeviceValue, 0, REG_SZ, (const BYTE*)buffer, (wcslen(buffer) + 1) * sizeof(wchar_t));

        GetWindowTextW(hEditAddress, buffer, 512);
        RegSetValueExW(hKey, g_szAddressValue, 0, REG_SZ, (const BYTE*)buffer, (wcslen(buffer) + 1) * sizeof(wchar_t));

        GetWindowTextW(hEditCommunity, buffer, 512);
        RegSetValueExW(hKey, g_szCommunityValue, 0, REG_SZ, (const BYTE*)buffer, (wcslen(buffer) + 1) * sizeof(wchar_t));

        GetWindowTextW(hEditKey, buffer, 512);
        RegSetValueExW(hKey, g_szKeyValue, 0, REG_SZ, (const BYTE*)buffer, (wcslen(buffer) + 1) * sizeof(wchar_t));

        GetWindowTextW(hEditSupernode, buffer, 512);
        RegSetValueExW(hKey, g_szSupernodeValue, 0, REG_SZ, (const BYTE*)buffer, (wcslen(buffer) + 1) * sizeof(wchar_t));

        RegCloseKey(hKey); // 完成後務必關閉機碼
    }
}

void LoadSettingsFromRegistry(HWND hWnd)
{
    HKEY hKey;
    // RegOpenKeyEx 只會嘗試打開現有機碼，不會建立
    if (RegOpenKeyExW(HKEY_CURRENT_USER, g_szRegKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t buffer[512];
        DWORD bufferSize = sizeof(buffer);

        // 讀取每個值並設定到對應的編輯框
        if (RegQueryValueExW(hKey, g_szDeviceValue, NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            SetWindowTextW(hEditDevice, buffer);
        }

        bufferSize = sizeof(buffer); // 每次查詢前重設大小
        if (RegQueryValueExW(hKey, g_szAddressValue, NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            SetWindowTextW(hEditAddress, buffer);
        }

        bufferSize = sizeof(buffer);
        if (RegQueryValueExW(hKey, g_szCommunityValue, NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            SetWindowTextW(hEditCommunity, buffer);
        }

        bufferSize = sizeof(buffer);
        if (RegQueryValueExW(hKey, g_szKeyValue, NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            SetWindowTextW(hEditKey, buffer);
        }

        bufferSize = sizeof(buffer);
        if (RegQueryValueExW(hKey, g_szSupernodeValue, NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
            SetWindowTextW(hEditSupernode, buffer);
        }

        RegCloseKey(hKey); // 完成後務必關閉機碼
    }
}
