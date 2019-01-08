#include "targetver.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <WtsApi32.h>
#include <objbase.h>

#include <malloc.h>
#include <tchar.h>

#include "rdesktop-lock.h"

#define MAX_STRING				64
#define WMAPP_NOTIFYCALLBACK	(WM_APP + 1)
#define TIMER_ID				0
#define RETRY_COUNT				10

// Use different GUID between builds to avoid ERROR_NO_TOKEN
#ifdef NDEBUG
const GUID APPLICATION_GUID = { 0x5000bd78, 0x2248, 0x4f3d, { 0xbc, 0x0c, 0x31, 0x5f, 0x9a, 0x39, 0xf6, 0xa0 } };
#else
const GUID APPLICATION_GUID = { 0x5000bd78, 0x2248, 0x4f3d, { 0xbc, 0x0c, 0x31, 0x5f, 0x9a, 0x39, 0xf6, 0xa1 } };
#endif
enum CONNECTION_TYPE { None, Local, Remote };

// Global Variables
HINSTANCE g_hInstance;
HMENU g_hMenu;
TCHAR g_szTitle[MAX_STRING];
TCHAR g_szWindowClass[MAX_STRING];

// Functions
BOOL ErrorHandler(HWND hWnd, BOOL bReturn) {
	UINT uError = GetLastError();
	if (!bReturn && uError != ERROR_SUCCESS) {
		TCHAR *Buffer;
		if ((bReturn = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, uError, 0, (LPTSTR)&Buffer, 0, NULL))) {
			bReturn = MessageBox(hWnd, Buffer, NULL, MB_OK | MB_ICONSTOP | MB_SYSTEMMODAL | MB_SETFOREGROUND);
			LocalFree(Buffer);
		}

		PostQuitMessage(uError);
	}

	return bReturn;
}

BOOL ErrorRepostHandler(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, BOOL bReturn) {
	static DWORD dwCount = 0;
	// If a timeout occurred, try to repost the message, up to the retry count
	if (!bReturn && GetLastError() == ERROR_TIMEOUT && dwCount++ < RETRY_COUNT) {
		if (dwCount++ < RETRY_COUNT) {
			return ErrorHandler(hWnd, PostMessage(hWnd, Msg, wParam, lParam));
		} else {
			return ErrorHandler(hWnd, FALSE);
		}
	} else if (bReturn) {
		dwCount = 0;
	}

	return bReturn;
}

BOOL IsScreenSaverEnabled(BOOL *bIsEnabled) {
	return SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, bIsEnabled, 0);
}

BOOL SetScreenSaver(BOOL bEnable) {
	return SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, bEnable, NULL, SPIF_SENDCHANGE);
}

BOOL SetScreenSaverTimeout(INT bTimeout) {
	return SystemParametersInfo(SPI_SETSCREENSAVETIMEOUT, bTimeout, NULL, SPIF_SENDCHANGE);
}

BOOL GetScreenSaverTimeout(INT *bTimeout) {
	return SystemParametersInfo(SPI_GETSCREENSAVETIMEOUT, 0, bTimeout, 0);
}

BOOL IsCurrentSessionRemote(BOOL *bIsEnabled) {
	LPTSTR lpOutput;
	SIZE_T sNumBytes;
	if (!WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSIsRemoteSession, &lpOutput, &sNumBytes))
		return FALSE;
	if (sNumBytes != 1)
		return FALSE;
	*bIsEnabled = lpOutput[0] & 0xFF;
	return TRUE;
}

BOOL SendMouseInput() {
	INPUT Inputs[] = { {
			.type = INPUT_MOUSE,
			.mi = {
				.dx = 0,
				.dy = 0,
				.dwFlags = MOUSEEVENTF_MOVE,
				.time = 0,
			},
		} };

	return SendInput(ARRAYSIZE(Inputs), Inputs, sizeof(*Inputs));
}

BOOL EnableTimeoutPrevention(HWND hWnd, UINT eInput) {
	BOOL bIsScreenSaverActive;
	INT uTimeout;

	if (!IsScreenSaverEnabled(&bIsScreenSaverActive))
		return FALSE;

	if (!bIsScreenSaverActive)
		return TRUE;

	return GetScreenSaverTimeout(&uTimeout) && SetTimer(hWnd, TIMER_ID, uTimeout * (1000 - 1), NULL);
}

BOOL RunTimeoutPrevention(UINT eInput) {
	switch (eInput) {
	case IDM_OPTIONS_MOUSE:
		return SendMouseInput();
	case IDM_OPTIONS_SCREENSAVERTIMEOUT: {
		INT uTimeout;
		return GetScreenSaverTimeout(&uTimeout) && SetScreenSaverTimeout(uTimeout);
	}
	default:
		return FALSE;
	}
}

BOOL DisableTimeoutPrevention(HWND hWnd, UINT eInput) {
	// Timer may not have been enabled
	KillTimer(hWnd, TIMER_ID);
	return TRUE;
}

BOOL RegisterSessionChange(HWND hWnd) {
	return WTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);
}

BOOL UnregisterSessionChange(HWND hWnd) {
	return WTSUnRegisterSessionNotification(hWnd);
}

BOOL AddNotificationIcon(HWND hWnd) {
	NOTIFYICONDATA IconData = {
		.cbSize = sizeof(IconData),
		.hWnd = hWnd,
		.uFlags = NIF_GUID | NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
		.uCallbackMessage = WMAPP_NOTIFYCALLBACK,
		.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON)),
		.uVersion = NOTIFYICON_VERSION_4,
		.guidItem = APPLICATION_GUID,
	};

	if (!LoadString(g_hInstance, IDS_STATUS_NONE, IconData.szTip, ARRAYSIZE(IconData.szTip)))
		return FALSE;

	// Add the icon first
	if (!Shell_NotifyIcon(NIM_ADD, &IconData))
		return FALSE;
	// Set the API version
	return Shell_NotifyIcon(NIM_SETVERSION, &IconData);
}

BOOL ChangeNotificationIcon(enum ConnectionType eType) {
	NOTIFYICONDATA IconData = {
		.cbSize = sizeof(IconData),
		.uFlags = NIF_GUID | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
		.guidItem = APPLICATION_GUID,
	};


	switch (eType) {
	case None:
		IconData.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON));
		if (!LoadString(g_hInstance, IDS_STATUS_NONE, IconData.szTip, ARRAYSIZE(IconData.szTip)))
			return FALSE;
		break;
	case Local:
		IconData.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_LOCAL));
		if (!LoadString(g_hInstance, IDS_STATUS_LOCAL, IconData.szTip, ARRAYSIZE(IconData.szTip)))
			return FALSE;
		break;
	case Remote:
		IconData.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_REMOTE));
		if (!LoadString(g_hInstance, IDS_STATUS_REMOTE, IconData.szTip, ARRAYSIZE(IconData.szTip)))
			return FALSE;
		break;
	}

	return Shell_NotifyIcon(NIM_MODIFY, &IconData);
}

BOOL RemoveNotificationIcon() {
	NOTIFYICONDATA IconData = {
		.cbSize = sizeof(IconData),
		.uFlags = NIF_GUID,
		.guidItem = APPLICATION_GUID,
	};

	return Shell_NotifyIcon(NIM_DELETE, &IconData);
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	UNREFERENCED_PARAMETER(lParam);

	switch (message) {
	case WM_INITDIALOG: {
		TCHAR FileName[MAX_PATH + 1] = { 0 };
		DWORD szVersionInfo;
		UINT szName, szVersion;
		VOID *VersionInfo, *Name, *Version;

		ErrorHandler(hDlg, GetModuleFileName(NULL, FileName, MAX_PATH));
		ErrorHandler(hDlg, (BOOL) (szVersionInfo = GetFileVersionInfoSize(FileName, NULL)));
		ErrorHandler(hDlg, (BOOL) (VersionInfo = malloc(szVersionInfo)));
		ErrorHandler(hDlg, GetFileVersionInfo(FileName, 0, szVersionInfo, VersionInfo));
		ErrorHandler(hDlg, VerQueryValue(VersionInfo, _T("\\StringFileInfo\\040904b0\\ProductName"), &Name, &szName));
		ErrorHandler(hDlg, VerQueryValue(VersionInfo, _T("\\StringFileInfo\\040904b0\\ProductVersion"), &Version, &szVersion));

		SetDlgItemText(hDlg, IDC_ABOUT_NAME, Name);
		SetDlgItemText(hDlg, IDC_ABOUT_VERSION, Version);
		SetDlgItemText(hDlg, IDC_EDIT, _T("https://github.com/ddcc/rdesktop-lock"));

		free(VersionInfo);
		return (INT_PTR) TRUE;
	}
	case WM_CLOSE:
		EndDialog(hDlg, 0);
		return (INT_PTR) TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK) {
			EndDialog(hDlg, 0);
			return (INT_PTR) TRUE;
		}
	default:
		return (INT_PTR) FALSE;
	}
}

BOOL CheckContextMenuItem(UINT uID, BOOL isChecked) {
	MENUITEMINFO Item = {
		.cbSize = sizeof(Item),
		.fMask = MIIM_STATE,
		.fState = isChecked ? MFS_CHECKED : MFS_UNCHECKED,
	};

	if (g_hMenu) {
		HMENU hSubMenu = GetSubMenu(g_hMenu, 0);
		if (hSubMenu) {
			return SetMenuItemInfo(hSubMenu, uID, FALSE, &Item);
		}
	}
	return FALSE;
}

BOOL ShowContextMenu(HWND hWnd, POINT pPt) {
    if (g_hMenu) {
        HMENU hSubMenu = GetSubMenu(g_hMenu, 0);
        if (hSubMenu) {
            // Set foreground to ensure it disappears on loss of focus
			if (!SetForegroundWindow(hWnd))
				return FALSE;

            // Respect menu left/right alignment
			return TrackPopupMenuEx(hSubMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN), pPt.x, pPt.y, hWnd, NULL);
        }
    }
	return FALSE;
}

BOOL HandleContextMenuSelection(HWND hWnd, BOOL bIsRemote, UINT *eInput, UINT uID) {
	if (!CheckContextMenuItem(*eInput, FALSE))
		return FALSE;
	if (bIsRemote && !DisableTimeoutPrevention(hWnd, *eInput))
		return FALSE;

	*eInput = uID;

	if (bIsRemote && !EnableTimeoutPrevention(hWnd, *eInput))
		return FALSE;
	return CheckContextMenuItem(uID, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT nMessage, WPARAM wParam, LPARAM lParam) {
	static BOOL bIsRemote = FALSE;
	static UINT WM_TASKBAR_CREATED = 0;
	static UINT eInput = IDM_OPTIONS_SCREENSAVERTIMEOUT;

	switch (nMessage) {
	case WMAPP_NOTIFYCALLBACK:
		switch (LOWORD(lParam)) {
		case WM_LBUTTONUP:
		case WM_CONTEXTMENU: {
			POINT Point = {
				.x = LOWORD(wParam),
				.y = HIWORD(wParam),
			};
			// Show context menu at the click position
			ErrorHandler(hWnd, ShowContextMenu(hWnd, Point));
			return 0;
		}
		default:
			return 0;
		}
	case WM_CREATE:
		// Remove icon if still present from previous unclean exit
		RemoveNotificationIcon();

		// Add notification icon and register handlers
		ErrorHandler(hWnd, (WM_TASKBAR_CREATED = RegisterWindowMessage(_T("TaskbarCreated"))));
		ErrorHandler(hWnd, RegisterSessionChange(hWnd));
		ErrorHandler(hWnd, AddNotificationIcon(hWnd));

		// Check if currently remote
		ErrorHandler(hWnd, IsCurrentSessionRemote(&bIsRemote));
		ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(bIsRemote ? Remote : Local));
		ErrorHandler(hWnd, HandleContextMenuSelection(hWnd, bIsRemote, &eInput, IDM_OPTIONS_SCREENSAVERTIMEOUT));
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDM_TRAY_ABOUT:
			ErrorHandler(hWnd, DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, DialogProc));
			return 0;
		case IDM_TRAY_EXIT:
			ErrorHandler(hWnd, DestroyWindow(hWnd));
			return 0;
		case IDM_OPTIONS_MOUSE:
		case IDM_OPTIONS_SCREENSAVERTIMEOUT:
			ErrorHandler(hWnd, HandleContextMenuSelection(hWnd, bIsRemote, &eInput, LOWORD(wParam)));
			return 0;
		default:
			return 0;
		}
	case WM_DESTROY:
		DisableTimeoutPrevention(hWnd, eInput);
		RemoveNotificationIcon();
		DestroyMenu(g_hMenu);
		UnregisterSessionChange(hWnd);
		PostQuitMessage(0);
		return 0;
	case WM_WTSSESSION_CHANGE:
		switch (LOWORD(wParam)) {
			case WTS_CONSOLE_CONNECT:
				bIsRemote = FALSE;
				ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(Local));
				return 0;
			case WTS_CONSOLE_DISCONNECT:
				ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(None));
				return 0;
			case WTS_REMOTE_CONNECT:
				bIsRemote = TRUE;
				ErrorHandler(hWnd, EnableTimeoutPrevention(hWnd, eInput));
				ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(Remote));
				return 0;
			case WTS_REMOTE_DISCONNECT:
				bIsRemote = FALSE;
				ErrorHandler(hWnd, DisableTimeoutPrevention(hWnd, eInput));
				ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(None));
				return 0;
			default:
				return 0;
		}
	case WM_TIMER:
		if (wParam == TIMER_ID) {
			BOOL hResult = RunTimeoutPrevention(eInput);
			if (!hResult && GetLastError() != ERROR_OPERATION_IN_PROGRESS)
				ErrorHandler(hWnd, hResult);
			return 0;
		}
	default:
		// Check if taskbar notification icon needs to be re-created
		if (nMessage == WM_TASKBAR_CREATED) {
			// Ignore error, may already exist
			AddNotificationIcon(hWnd);
			ErrorRepostHandler(hWnd, nMessage, wParam, lParam, ChangeNotificationIcon(bIsRemote ? Remote : Local));
			return 0;
		}

		return DefWindowProc(hWnd, nMessage, wParam, lParam);
	}
}

BOOL RegisterWindowClass() {
	WNDCLASSEX WindowClass = {
		.cbSize = sizeof(WindowClass),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = g_hInstance,
		.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON)),
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
		.lpszClassName = g_szWindowClass,
		.hIconSm = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON)),
	};

	return RegisterClassEx(&WindowClass);
}

BOOL InitInstance(int nCmdShow) {
	UNREFERENCED_PARAMETER(nCmdShow);

	// Window is not visible because WS_VISIBLE is not set
	HWND hWnd = CreateWindowEx(WS_EX_APPWINDOW, g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, g_hInstance, NULL);
	if (!hWnd)
		return FALSE;

	// Register for taskbar re-creation message
	return UpdateWindow(hWnd);
}

int APIENTRY _tWinMain(_In_ HINSTANCE hWindowInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ INT nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	g_hInstance = hWindowInstance;

	// Load taskbar menu and application/window class strings
	ErrorHandler(NULL, (BOOL) (g_hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDC_MENU))));
	ErrorHandler(NULL, LoadString(g_hInstance, IDS_APP_TITLE, g_szTitle, ARRAYSIZE(g_szTitle)));
	ErrorHandler(NULL, LoadString(g_hInstance, IDS_WINDOW_CLASS_NAME, g_szWindowClass, ARRAYSIZE(g_szWindowClass)));

	ErrorHandler(NULL, RegisterWindowClass());
	ErrorHandler(NULL, InitInstance(nCmdShow));

	MSG msg;
	while (ErrorHandler(NULL, (BOOL) (GetMessage(&msg, NULL, 0, 0) != 0))) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int) msg.wParam;
}
