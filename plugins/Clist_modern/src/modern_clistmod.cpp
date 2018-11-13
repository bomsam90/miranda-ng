/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (c) 2012-18 Miranda NG team (https://miranda-ng.org),
Copyright (c) 2000-08 Miranda ICQ/IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"
#include "modern_awaymsg.h"
#include "modern_sync.h"
#include "modern_clui.h"

int OnLoadLangpack(WPARAM, LPARAM);
void UnloadFavoriteContactMenu();

int CListMod_HideWindow();

void UninitCListEvents(void);
int ContactAdded(WPARAM wParam, LPARAM lParam);
int CListOptInit(WPARAM wParam, LPARAM lParam);
int SkinOptInit(WPARAM wParam, LPARAM lParam);

// returns normal icon or combined with status overlay. Needs to be destroyed.

HICON cliGetIconFromStatusMode(MCONTACT hContact, const char *szProto, int status)
{
	// check if options is turned on
	BYTE trayOption = db_get_b(0, "CLUI", "XStatusTray", SETTING_TRAYOPTION_DEFAULT);
	if ((trayOption & 3) && szProto != nullptr) {
		if (ProtoServiceExists(szProto, PS_GETCUSTOMSTATUSICON)) {
			// check status is online
			if (status > ID_STATUS_OFFLINE) {
				// get xicon
				HICON hXIcon = (HICON)CallProtoService(szProto, PS_GETCUSTOMSTATUSICON, 0, 0);
				if (hXIcon) {
					// check overlay mode
					if (trayOption & 2) {
						// get overlay
						HICON MainOverlay = (HICON)GetMainStatusOverlay(status);
						HICON hIcon = ske_CreateJoinedIcon(hXIcon, MainOverlay, (trayOption & 4) ? 192 : 0);
						DestroyIcon_protect(hXIcon);
						DestroyIcon_protect(MainOverlay);
						return hIcon;
					}
					return hXIcon;
				}
			}
		}
	}

	return ske_ImageList_GetIcon(g_himlCListClc, g_clistApi.pfnIconFromStatusMode(szProto, status, hContact));
}

int cli_IconFromStatusMode(const char *szProto, int nStatus, MCONTACT hContact)
{
	if (hContact && szProto) {
		char *szActProto = (char*)szProto;
		int nActStatus = nStatus;
		MCONTACT hActContact = hContact;
		if (!db_get_b(0, "CLC", "Meta", SETTING_USEMETAICON_DEFAULT) && !mir_strcmp(szActProto, META_PROTO)) {
			// substitute params by mostonline contact datas
			MCONTACT hMostOnlineContact = db_mc_getMostOnline(hActContact);
			if (hMostOnlineContact) {
				ClcCacheEntry *cacheEntry = Clist_GetCacheEntry(hMostOnlineContact);
				if (cacheEntry && cacheEntry->szProto) {
					szActProto = cacheEntry->szProto;
					nActStatus = cacheEntry->m_iStatus;
					hActContact = hMostOnlineContact;
				}
			}
		}

		int result = -1;
		if (ProtoServiceExists(szActProto, PS_GETADVANCEDSTATUSICON))
			result = CallProtoService(szActProto, PS_GETADVANCEDSTATUSICON, (WPARAM)hActContact, 0);

		if (result == -1 || !(LOWORD(result))) {
			//Get normal Icon
			int  basicIcon = corecli.pfnIconFromStatusMode(szActProto, nActStatus, 0);
			if (result != -1 && basicIcon != 1)
				result |= basicIcon;
			else
				result = basicIcon;
		}
		return result;
	}

	return corecli.pfnIconFromStatusMode(szProto, nStatus, 0);
}

int GetContactIconC(ClcCacheEntry *p)
{
	return g_clistApi.pfnIconFromStatusMode(p->szProto, p->szProto == nullptr ? ID_STATUS_OFFLINE : p->m_iStatus, p->hContact);
}

//lParam
// 0 - default - return icon id in order: transport status icon, protostatus icon, meta is affected

void UnLoadContactListModule()  //unhooks noncritical events
{
	UnloadFavoriteContactMenu();
}

int CListMod_ContactListShutdownProc(WPARAM, LPARAM)
{
	UninitAwayMsgModule();
	return 0;
}

HRESULT PreLoadContactListModule()
{
	/* Global data initialization */
	g_CluiData.fOnDesktop = false;
	g_CluiData.dwKeyColor = RGB(255, 0, 255);
	g_CluiData.bCurrentAlpha = 255;

	// catch langpack events
	HookEvent(ME_LANGPACK_CHANGED, OnLoadLangpack);
	OnLoadLangpack(0, 0);
	return S_OK;
}

INT_PTR SvcActiveSkin(WPARAM wParam, LPARAM lParam);
INT_PTR SvcPreviewSkin(WPARAM wParam, LPARAM lParam);
INT_PTR SvcApplySkin(WPARAM wParam, LPARAM lParam);

HRESULT CluiLoadModule()
{
	InitAwayMsgModule();

	HookEvent(ME_SYSTEM_SHUTDOWN, CListMod_ContactListShutdownProc);
	HookEvent(ME_OPT_INITIALISE, CListOptInit);
	HookEvent(ME_OPT_INITIALISE, SkinOptInit);

	CreateServiceFunction("ModernSkinSel/Active", SvcActiveSkin);
	CreateServiceFunction("ModernSkinSel/Preview", SvcPreviewSkin);
	CreateServiceFunction("ModernSkinSel/Apply", SvcApplySkin);

	HookEvent(ME_DB_CONTACT_ADDED, ContactAdded);

	CreateServiceFunction(MS_CLIST_TOGGLEHIDEOFFLINE, ToggleHideOffline);
	CreateServiceFunction(MS_CLIST_TOGGLESOUNDS, ToggleSounds);
	CreateServiceFunction(MS_CLIST_SETUSEGROUPS, SetUseGroups);

	InitCustomMenus();
	
	CLUI::InitClui();
	return S_OK;
}

#define GWVS_HIDDEN 1
#define GWVS_VISIBLE 2
#define GWVS_COVERED 3
#define GWVS_PARTIALLY_COVERED 4

int GetWindowVisibleState(HWND, int, int);
__inline DWORD GetDIBPixelColor(int X, int Y, int Width, int Height, int ByteWidth, BYTE * ptr)
{
	DWORD res = 0;
	if (X >= 0 && X < Width && Y >= 0 && Y < Height && ptr)
		res = *((DWORD*)(ptr + ByteWidth*(Height - Y - 1) + X * 4));
	return res;
}

int GetWindowVisibleState(HWND hWnd, int iStepX, int iStepY)
{
	if (hWnd == nullptr) {
		SetLastError(0x00000006); // Wrong handle
		return -1;
	}

	if (IsIconic(hWnd) || !IsWindowVisible(hWnd))
		return GWVS_HIDDEN;

	if (g_plugin.getByte("OnDesktop", SETTING_ONDESKTOP_DEFAULT) || !g_plugin.getByte("BringToFront", SETTING_BRINGTOFRONT_DEFAULT))
		return GWVS_VISIBLE;

	HWND hwndFocused = GetFocus();
	if (hwndFocused == g_clistApi.hwndContactList || GetParent(hwndFocused) == g_clistApi.hwndContactList)
		return GWVS_VISIBLE;

	// Some defaults now. The routine is designed for thin and tall windows.
	if (iStepX <= 0) iStepX = 8;
	if (iStepY <= 0) iStepY = 16;

	int maxx = 0;
	int maxy = 0;
	int wx = 0;
	BYTE *ptr = nullptr;
	HRGN rgn = nullptr;
	HBITMAP WindowImage = g_CluiData.fLayered ? ske_GetCurrentWindowImage() : nullptr;
	if (WindowImage && g_CluiData.fLayered) {
		BITMAP bmp;
		GetObject(WindowImage, sizeof(BITMAP), &bmp);
		ptr = (BYTE*)bmp.bmBits;
		maxx = bmp.bmWidth;
		maxy = bmp.bmHeight;
		wx = bmp.bmWidthBytes;
	}
	else {
		RECT rc;
		rgn = CreateRectRgn(0, 0, 1, 1);
		GetWindowRect(hWnd, &rc);
		GetWindowRgn(hWnd, rgn);
		OffsetRgn(rgn, rc.left, rc.top);
		GetRgnBox(rgn, &rc);
		//maxx = rc.right;
		//maxy = rc.bottom;
	}

	RECT rc;
	GetWindowRect(hWnd, &rc);

	RECT rcMonitor = { 0 };
	Docking_GetMonitorRectFromWindow(hWnd, &rcMonitor);

	rc.top = rc.top < rcMonitor.top ? rcMonitor.top : rc.top;
	rc.left = rc.left < rcMonitor.left ? rcMonitor.left : rc.left;
	rc.bottom = rc.bottom > rcMonitor.bottom ? rcMonitor.bottom : rc.bottom;
	rc.right = rc.right > rcMonitor.right ? rcMonitor.right : rc.right;

	int width = rc.right - rc.left;
	int height = rc.bottom - rc.top;
	int dx = -rc.left;
	int dy = -rc.top;
	int hstep = width / iStepX;
	int vstep = height / iStepY;
	hstep = hstep > 0 ? hstep : 1;
	vstep = vstep > 0 ? vstep : 1;

	int iCountedDots = 0, iNotCoveredDots = 0;
	for (int i = rc.top; i < rc.bottom; i += vstep) {
		POINT pt;
		pt.y = i;
		for (int j = rc.left; j < rc.right; j += hstep) {
			BOOL po = FALSE;
			pt.x = j;
			if (rgn)
				po = PtInRegion(rgn, j, i);
			else {
				DWORD a = (GetDIBPixelColor(j + dx, i + dy, maxx, maxy, wx, ptr) & 0xFF000000) >> 24;
				a = ((a*g_CluiData.bCurrentAlpha) >> 8);
				po = (a > 16);
			}

			if (po || (!rgn && ptr == nullptr)) {
				BOOL hWndFound = FALSE;
				HWND hAuxOld = nullptr;
				HWND hAux = WindowFromPoint(pt);
				do {
					if (hAux == hWnd) {
						hWndFound = TRUE;
						break;
					}

					hAuxOld = hAux;
					hAux = GetAncestor(hAux, GA_ROOTOWNER);
					if (hAuxOld == hAux) {
						wchar_t buf[255];
						GetClassName(hAux, buf, _countof(buf));
						if (!mir_wstrcmp(buf, CLUIFrameSubContainerClassName)) {
							hWndFound = TRUE;
							break;
						}
					}
				} while (hAux != nullptr  && hAuxOld != hAux);

				if (hWndFound)        // There's  window!
					iNotCoveredDots++; // Let's count the not covered dots.
				iCountedDots++;       // Let's keep track of how many dots we checked.
			}
		}
	}
	if (rgn)
		DeleteObject(rgn);

	if (iCountedDots - iNotCoveredDots < 2) // Every dot was not covered: the window is visible.
		return GWVS_VISIBLE;

	if (iNotCoveredDots == 0) // They're all covered!
		return GWVS_COVERED;

	// There are dots which are visible, but they are not as many as the ones we counted: it's partially covered.
	return GWVS_PARTIALLY_COVERED;
}

BYTE g_bCalledFromShowHide = 0;

int cliShowHide(bool bAlwaysShow)
{
	BOOL bShow = FALSE;

	int iVisibleState = GetWindowVisibleState(g_clistApi.hwndContactList, 0, 0);
	int method = db_get_b(0, "ModernData", "HideBehind", SETTING_HIDEBEHIND_DEFAULT); //(0-none, 1-leftedge, 2-rightedge);
	if (method) {
		if (db_get_b(0, "ModernData", "BehindEdge", SETTING_BEHINDEDGE_DEFAULT) == 0 && !bAlwaysShow)
			CLUI_HideBehindEdge(); //hide
		else
			CLUI_ShowFromBehindEdge();

		bShow = TRUE;
		iVisibleState = GWVS_HIDDEN;
	}

	if (!method && db_get_b(0, "ModernData", "BehindEdge", SETTING_BEHINDEDGE_DEFAULT) > 0) {
		g_CluiData.bBehindEdgeSettings = db_get_b(0, "ModernData", "BehindEdge", SETTING_BEHINDEDGE_DEFAULT);
		CLUI_ShowFromBehindEdge();
		g_CluiData.bBehindEdgeSettings = 0;
		g_CluiData.nBehindEdgeState = 0;
		db_unset(0, "ModernData", "BehindEdge");
	}

	// bShow is FALSE when we enter the switch if no hide behind edge.
	switch (iVisibleState) {
	case GWVS_PARTIALLY_COVERED:
		bShow = TRUE; break;
	case GWVS_COVERED: //Fall through (and we're already falling)
		bShow = TRUE; break;
	case GWVS_HIDDEN:
		bShow = TRUE; break;
	case GWVS_VISIBLE: //This is not needed, but goes for readability.
		bShow = FALSE; break;
	case -1: //We can't get here, both g_clistApi.hwndContactList and iStepX and iStepY are right.
		return 0;
	}

	if (bShow || bAlwaysShow) {
		Sync(CLUIFrames_ActivateSubContainers, TRUE);
		CLUI_ShowWindowMod(g_clistApi.hwndContactList, SW_RESTORE);

		if (!g_plugin.getByte("OnDesktop", SETTING_ONDESKTOP_DEFAULT)) {
			Sync(CLUIFrames_OnShowHide, 1);	//TO BE PROXIED
			SetWindowPos(g_clistApi.hwndContactList, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			g_bCalledFromShowHide = 1;
			if (!g_plugin.getByte("OnTop", SETTING_ONTOP_DEFAULT))
				SetWindowPos(g_clistApi.hwndContactList, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
			g_bCalledFromShowHide = 0;
		}
		else {
			SetWindowPos(g_clistApi.hwndContactList, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
			Sync(CLUIFrames_OnShowHide, 1);
			SetForegroundWindow(g_clistApi.hwndContactList);
		}
		g_plugin.setByte("State", SETTING_STATE_NORMAL);

		RECT rcWindow;
		GetWindowRect(g_clistApi.hwndContactList, &rcWindow);
		if (Utils_AssertInsideScreen(&rcWindow) == 1)
			MoveWindow(g_clistApi.hwndContactList, rcWindow.left, rcWindow.top,
			rcWindow.right - rcWindow.left, rcWindow.bottom - rcWindow.top, TRUE);
	}
	else { // It needs to be hidden
		if (GetWindowLongPtr(g_clistApi.hwndContactList, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
			CListMod_HideWindow();
			g_plugin.setByte("State", SETTING_STATE_HIDDEN);
		}
		else if (g_plugin.getByte("Min2Tray", SETTING_MIN2TRAY_DEFAULT)) {
			CLUI_ShowWindowMod(g_clistApi.hwndContactList, SW_HIDE);
			g_plugin.setByte("State", SETTING_STATE_HIDDEN);
		}
		else {
			CLUI_ShowWindowMod(g_clistApi.hwndContactList, SW_MINIMIZE);
			g_plugin.setByte("State", SETTING_STATE_MINIMIZED);
		}

		SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
	}
	return 0;
}

int CListMod_HideWindow()
{
	KillTimer(g_clistApi.hwndContactList, 1);

	if (!CLUI_HideBehindEdge())
		return CLUI_SmoothAlphaTransition(g_clistApi.hwndContactList, 0, 1);
	return 0;
}
