// -----------------------------------------------------------------------------
// ICQ plugin for Miranda NG
// -----------------------------------------------------------------------------
// Copyright � 2018-19 Miranda NG team
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
// -----------------------------------------------------------------------------

#include "stdafx.h"

#pragma comment(lib, "libeay32.lib")

void CIcqProto::CheckAvatarChange(MCONTACT hContact, const JSONNode &ev)
{
	CMStringW wszIconId(ev["iconId"].as_mstring());
	if (!wszIconId.IsEmpty()) {
		CMStringW oldIconID(getMStringW(hContact, "IconId"));
		if (wszIconId == oldIconID) {
			wchar_t wszFullName[MAX_PATH];
			GetAvatarFileName(hContact, wszFullName, _countof(wszFullName));
			if (_waccess(wszFullName, 0) == 0)
				return;
		}

		setWString(hContact, "IconId", wszIconId);
	}
	else delSetting(hContact, "IconId");

	CMStringA szUrl(ev["buddyIcon"].as_mstring());
	if (!szUrl.IsEmpty()) {
		auto *pReq = new AsyncHttpRequest(CONN_MAIN, REQUEST_GET, szUrl, &CIcqProto::OnReceiveAvatar);
		pReq->hContact = hContact;
		Push(pReq);
	}
}

void CIcqProto::CheckLastId(MCONTACT hContact, const JSONNode &ev)
{
	__int64 msgId = _wtoi64(ev["histMsgId"].as_mstring());
	__int64 lastId = getId(hContact, DB_KEY_LASTMSGID);
	if (msgId > lastId)
		setId(hContact, DB_KEY_LASTMSGID, msgId);
}

MCONTACT CIcqProto::CheckOwnMessage(const CMStringA &reqId, const CMStringA &msgId, bool bRemove)
{
	IcqOwnMessage *pOwn = nullptr;
	{
		mir_cslock lck(m_csOwnIds);
		for (auto &it : m_arOwnIds) {
			if (reqId == it->m_guid) {
				pOwn = it;
				break;
			}
		}
	}

	if (pOwn == nullptr)
		return 0;

	ProtoBroadcastAck(pOwn->m_hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, (HANDLE)pOwn->m_msgid, (LPARAM)msgId.c_str());

	MCONTACT ret = pOwn->m_hContact;
	if (bRemove) {
		// here we filter service messages for SecureIM, OTR etc, i.e. messages that 
		// weren't initialized by SRMM (we identify it by missing server id)
		if (db_event_getById(m_szModuleName, msgId) == 0)
			db_event_setId(m_szModuleName, 1, msgId);

		mir_cslock lck(m_csOwnIds);
		m_arOwnIds.remove(pOwn);
	}
	return ret;
}

void CIcqProto::CheckPassword()
{
	char mirVer[100];
	Miranda_GetVersionText(mirVer, _countof(mirVer));

	m_szAToken = getMStringA(DB_KEY_ATOKEN);
	m_iRClientId = getDword(DB_KEY_RCLIENTID);
	m_szSessionKey = getMStringA(DB_KEY_SESSIONKEY);
	if (m_szAToken.IsEmpty() || m_szSessionKey.IsEmpty()) {
		auto *pReq = new AsyncHttpRequest(CONN_MAIN, REQUEST_POST, "https://api.login.icq.net/auth/clientLogin", &CIcqProto::OnCheckPassword);
		pReq << CHAR_PARAM("clientName", "Miranda NG") << CHAR_PARAM("clientVersion", mirVer) << CHAR_PARAM("devId", ICQ_APP_ID)
			<< CHAR_PARAM("f", "json") << CHAR_PARAM("tokenType", "longTerm") << WCHAR_PARAM("s", m_szOwnId) << CHAR_PARAM("pwd", m_szPassword);
		pReq->flags |= NLHRF_NODUMPSEND;
		Push(pReq);
	}
	else StartSession();
}

void CIcqProto::CheckStatus()
{
	time_t now = time(0);
	int diff1 = m_iTimeDiff1, diff2 = m_iTimeDiff2;

	for (auto &it : m_arCache) {
		// this contact is really offline and is on the first timer
		// if the first timer is expired, we clear it and look for the second status
		if (diff1 && it->m_timer1 && now - it->m_timer1 > diff1) {
			it->m_timer1 = 0;

			// if the second timer is set up, activate it
			if (m_iTimeDiff2) {
				setDword(it->m_hContact, "Status", m_iStatus2);
				it->m_timer2 = now;
			}
			// if the second timer is not set, simply mark a contact as offline
			else setDword(it->m_hContact, "Status", ID_STATUS_OFFLINE);
			continue;
		}

		// if the second timer is expired, set status to offline
		if (diff2 && it->m_timer2 && now - it->m_timer2 > m_iTimeDiff2) {
			it->m_timer2 = 0;
			setDword(it->m_hContact, "Status", ID_STATUS_OFFLINE);
		}
	}
}

void CIcqProto::ConnectionFailed(int iReason, int iErrorCode)
{
	debugLogA("ConnectionFailed -> reason %d", iReason);

	if (m_bErrorPopups) {
		POPUPDATAW Popup = {};
		Popup.lchIcon = IcoLib_GetIconByHandle(Skin_GetIconHandle(SKINICON_ERROR), true);
		wcscpy_s(Popup.lpwzContactName, m_tszUserName);
		switch (iReason) {
		case LOGINERR_BADUSERID:
			mir_snwprintf(Popup.lpwzText, TranslateT("You have not entered an ICQ number or password.\nConfigure this in Options -> Network -> ICQ and try again."));
			break;
		case LOGINERR_WRONGPASSWORD:
			mir_snwprintf(Popup.lpwzText, TranslateT("Connection failed.\nYour ICQ number or password was rejected (%d)."), iErrorCode);
			break;
		case LOGINERR_NONETWORK:
		case LOGINERR_NOSERVER:
			mir_snwprintf(Popup.lpwzText, TranslateT("Connection failed.\nThe server is temporarily unavailable (%d)."), iErrorCode);
			break;
		default:
			mir_snwprintf(Popup.lpwzText, TranslateT("Connection failed.\nUnknown error during sign on: %d"), iErrorCode);
			break;
		}
		PUAddPopupW(&Popup);
	}

	ProtoBroadcastAck(0, ACKTYPE_LOGIN, ACKRESULT_FAILED, nullptr, iReason);
	ShutdownSession();
}

void CIcqProto::MoveContactToGroup(MCONTACT hContact, const wchar_t *pwszGroup, const wchar_t *pwszNewGroup)
{
	Push(new AsyncHttpRequest(CONN_MAIN, REQUEST_GET, ICQ_API_SERVER "/buddylist/moveBuddy")
		<< AIMSID(this) << WCHAR_PARAM("buddy", GetUserId(hContact)) << GROUP_PARAM("group", pwszGroup) << GROUP_PARAM("newGroup", pwszNewGroup));
}

/////////////////////////////////////////////////////////////////////////////////////////

static void CALLBACK CheckStatusTimerProc(HWND, UINT, UINT_PTR id, DWORD)
{
	CIcqProto *ppro = (CIcqProto*)(id - 1);
	ppro->CheckStatus();
}

void CIcqProto::OnLoggedIn()
{
	debugLogA("CIcqProto::OnLoggedIn");
	m_bOnline = true;

	::SetTimer(g_hwndHeartbeat, UINT_PTR(this)+1, 1000, CheckStatusTimerProc);
	for (auto &it : m_arCache)
		it->m_timer1 = it->m_timer2 = 0;

	SetServerStatus(m_iDesiredStatus);
	RetrieveUserInfo(0);
	GetPermitDeny();
}

/////////////////////////////////////////////////////////////////////////////////////////

void CIcqProto::OnLoggedOut()
{
	debugLogA("CIcqProto::OnLoggedOut");
	m_bOnline = false;

	::KillTimer(g_hwndHeartbeat, UINT_PTR(this)+1);
	for (auto &it : m_arCache)
		it->m_timer1 = it->m_timer2 = 0;

	ProtoBroadcastAck(0, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)m_iStatus, ID_STATUS_OFFLINE);
	m_iStatus = m_iDesiredStatus = ID_STATUS_OFFLINE;

	setAllContactStatuses(ID_STATUS_OFFLINE, false);
}

/////////////////////////////////////////////////////////////////////////////////////////

MCONTACT CIcqProto::ParseBuddyInfo(const JSONNode &buddy, MCONTACT hContact)
{
	// user chat?
	CMStringW wszId(buddy["aimId"].as_mstring());
	if (IsChat(wszId)) {
		CMStringW wszChatId(buddy["aimId"].as_mstring());
		CMStringW wszChatName(buddy["friendly"].as_mstring());

		SESSION_INFO *si = Chat_NewSession(GCW_CHATROOM, m_szModuleName, wszChatId, wszChatName);
		if (si == nullptr)
			return INVALID_CONTACT_ID;

		Chat_AddGroup(si, TranslateT("admin"));
		Chat_AddGroup(si, TranslateT("member"));
		Chat_Control(m_szModuleName, wszChatId, m_bHideGroupchats ? WINDOW_HIDDEN : SESSION_INITDONE);
		Chat_Control(m_szModuleName, wszChatId, SESSION_ONLINE);
		return si->hContact;
	}

	if (hContact == -1) {
		hContact = CreateContact(wszId, false);
		FindContactByUIN(wszId)->m_bInList = true;
	}

	CMStringA szVer;
	bool bVersionDetected = false, bSecureIM = false;

	for (auto &it : buddy["capabilities"]) {
		CMStringW wszCap(it.as_mstring());
		if (wszCap.GetLength() != 32)
			continue;

		BYTE cap[16];
		hex2binW(wszCap, cap, sizeof(cap));
		if (!memcmp(cap, "MiNG", 4)) { // Miranda
			int v[4];
			if (4 == swscanf(wszCap.c_str() + 16, L"%04x%04x%04x%04x", &v[0], &v[1], &v[2], &v[3])) {
				szVer.Format("Miranda NG %d.%d.%d.%d (ICQ %d.%d.%d.%d)", v[0], v[1], v[2], v[3], cap[4], cap[5], cap[6], cap[7]);
				setString(hContact, "MirVer", szVer);
				bVersionDetected = true;
			}
		}
		else if (wszCap == _A2W(NG_CAP_SECUREIM)) {
			bSecureIM = bVersionDetected = true;
		}
		else if (!memcmp(cap, "Mod by Mikanoshi", 16)) {
			szVer = "R&Q build by Mikanoshi";
			bVersionDetected = true;
		}
		else if (!memcmp(cap, "Mandarin IM", 11)) {
			szVer = "Mandarin IM";
			bVersionDetected = true;
		}
	}

	if (bVersionDetected) {
		if (bSecureIM)
			szVer.Append(" + SecureIM");
		setString(hContact, "MirVer", szVer);
	}
	else delSetting(hContact, "MirVer");

	CMStringW str(buddy["state"].as_mstring());
	setDword(hContact, "Status", StatusFromString(str));

	const JSONNode &var = buddy["friendly"];
	if (var)
		setWString(hContact, "Nick", var.as_mstring());

	Json2string(hContact, buddy, "emailId", "Email");
	Json2string(hContact, buddy, "cellNumber", "Cellular");
	Json2string(hContact, buddy, "phoneNumber", "Phone");
	Json2string(hContact, buddy, "workNumber", "CompanyPhone");

	// zero here means that a contact is currently online
	int lastSeen = buddy["lastseen"].as_int();
	setDword(hContact, DB_KEY_LASTSEEN, lastSeen ? lastSeen : time(0));

	Json2int(hContact, buddy, "official", "Official");
	Json2int(hContact, buddy, "onlineTime", DB_KEY_ONLINETS);
	Json2int(hContact, buddy, "idleTime", "IdleTS");
	Json2int(hContact, buddy, "memberSince", DB_KEY_MEMBERSINCE);

	const JSONNode &profile = buddy["profile"];
	if (profile) {
		Json2string(hContact, profile, "friendlyName", DB_KEY_ICQNICK);
		Json2string(hContact, profile, "firstName", "FirstName");
		Json2string(hContact, profile, "lastName", "LastName");
		Json2string(hContact, profile, "aboutMe", DB_KEY_ABOUT);

		ptrW wszNick(getWStringA(hContact, "Nick"));
		if (!wszNick) {
			CMStringW srvNick = profile["friendlyName"].as_mstring();
			if (!srvNick.IsEmpty())
				setWString(hContact, "Nick", srvNick);
		}

		time_t birthDate = profile["birthDate"].as_int();
		if (birthDate != 0) {
			struct tm *timeinfo = localtime(&birthDate);
			if (timeinfo != nullptr) {
				setWord(hContact, "BirthDay", timeinfo->tm_mday);
				setWord(hContact, "BirthMonth", timeinfo->tm_mon+1);
				setWord(hContact, "BirthYear", timeinfo->tm_year+1900);
			}
		}

		str = profile["gender"].as_mstring();
		if (!str.IsEmpty()) {
			if (str == "male")
				setByte(hContact, "Gender", 'M');
			else if (str == "female")
				setByte(hContact, "Gender", 'F');
		}

		for (auto &it : profile["homeAddress"]) {
			Json2string(hContact, it, "city", "City");
			Json2string(hContact, it, "state", "State");
			Json2string(hContact, it, "country", "Country");
		}
	}

	str = buddy["statusMsg"].as_mstring();
	if (str.IsEmpty())
		db_unset(hContact, "CList", "StatusMsg");
	else
		db_set_ws(hContact, "CList", "StatusMsg", str);

	CheckAvatarChange(hContact, buddy);
	return hContact;
}

void CIcqProto::ParseMessage(MCONTACT hContact, __int64 &lastMsgId, const JSONNode &it, bool bFromHistory)
{
	CMStringA szMsgId(it["msgId"].as_mstring());
	__int64 msgId = _atoi64(szMsgId);
	if (msgId > lastMsgId)
		lastMsgId = msgId;

	CMStringW wszText;
	const JSONNode &sticker = it["sticker"];
	if (sticker) {
		CMStringW wszUrl, wszSticker(sticker["id"].as_mstring());
		int iCollectionId, iStickerId;
		if (2 == swscanf(wszSticker, L"ext:%d:sticker:%d", &iCollectionId, &iStickerId))
			wszUrl.Format(L"https://c.icq.com/store/stickers/%d/%d/medium", iCollectionId, iStickerId);
		else
			wszUrl = TranslateT("Unknown sticker");
		wszText.Format(L"%s\n%s", TranslateT("User sent a sticker:"), wszUrl.c_str());
	}
	else wszText = it["text"].as_mstring();

	int iMsgTime = (bFromHistory) ? it["time"].as_int() : time(0);

	if (isChatRoom(hContact)) {
		CMStringA reqId(it["reqId"].as_mstring());
		CheckOwnMessage(reqId, szMsgId, true);

		CMStringW wszSender(it["chat"]["sender"].as_mstring());
		CMStringW wszChatId(getMStringW(hContact, "ChatRoomID"));

		GCEVENT gce = { m_szModuleName, 0, GC_EVENT_MESSAGE };
		gce.pszID.w = wszChatId;
		gce.dwFlags = GCEF_ADDTOLOG;
		gce.pszUID.w = wszSender;
		gce.pszText.w = wszText;
		gce.time = iMsgTime;
		gce.bIsMe = wszSender == m_szOwnId;
		Chat_Event(&gce);
	}
	else {
		// skip own messages, just set the server msgid
		CMStringA reqId(it["reqId"].as_mstring());
		if (CheckOwnMessage(reqId, szMsgId, true))
			return;

		// ignore duplicates
		MEVENT hDbEvent = db_event_getById(m_szModuleName, szMsgId);
		if (hDbEvent != 0)
			return;

		bool bIsOutgoing = it["outgoing"].as_bool();
		ptrA szUtf(mir_utf8encodeW(wszText));

		PROTORECVEVENT pre = {};
		pre.flags = (bIsOutgoing) ? PREF_SENT : 0;
		pre.szMsgId = szMsgId;
		pre.timestamp = iMsgTime;
		pre.szMessage = szUtf;
		ProtoChainRecvMsg(hContact, &pre);
	}
}

bool CIcqProto::RefreshRobustToken()
{
	if (!m_szRToken.IsEmpty())
		return true;

	bool bRet = false;
	auto *tmp = new AsyncHttpRequest(CONN_RAPI, REQUEST_POST, ICQ_ROBUST_SERVER "/genToken");

	int ts = TS();
	tmp << CHAR_PARAM("a", m_szAToken) << CHAR_PARAM("k", ICQ_APP_ID) << CHAR_PARAM("nonce", CMStringA(FORMAT, "%d-%d", ts, rand() % 10)) << INT_PARAM("ts", TS());
	CalcHash(tmp);
	tmp->flags |= NLHRF_PERSISTENT;
	tmp->nlc = m_ConnPool[CONN_RAPI].s;
	tmp->dataLength = tmp->m_szParam.GetLength();
	tmp->pData = tmp->m_szParam.Detach();
	tmp->szUrl = tmp->m_szUrl.GetBuffer();

	CMStringA szAgent(FORMAT, "%S Mail.ru Windows ICQ (version 10.0.1999)", (wchar_t*)m_szOwnId);
	tmp->AddHeader("User-Agent", szAgent);

	NLHR_PTR reply(Netlib_HttpTransaction(m_hNetlibUser, tmp));
	if (reply != nullptr) {
		m_ConnPool[CONN_RAPI].s = reply->nlc;

		RobustReply result(reply);
		if (result.error() == 20000) {
			const JSONNode &results = result.results();
			m_szRToken = results["authToken"].as_mstring();

			// now add this token
			auto *add = new AsyncHttpRequest(CONN_RAPI, REQUEST_POST, ICQ_ROBUST_SERVER, &CIcqProto::OnAddClient);
			JSONNode request, params; params.set_name("params");
			request << CHAR_PARAM("method", "addClient") << CHAR_PARAM("reqId", add->m_reqId) << CHAR_PARAM("authToken", m_szRToken) << params;
			add->m_szParam = ptrW(json_write(&request));
			add->pUserInfo = &bRet;
			ExecuteRequest(add);
		}
	}
	else m_ConnPool[CONN_RAPI].s = nullptr;

	delete tmp;
	return bRet;
}

void CIcqProto::RetrieveUserInfo(MCONTACT hContact)
{
	auto *pReq = UserInfoRequest(hContact);

	if (hContact == INVALID_CONTACT_ID) {
		int i = 0;
		for (auto &it : m_arCache) {
			if (i == 0)
				pReq = UserInfoRequest(hContact);

			pReq << WCHAR_PARAM("t", GetUserId(it->m_hContact));
			if (i == 100) {
				i = 0;
				Push(pReq);

				pReq = UserInfoRequest(hContact);
			}
			else i++;
		}
	}
	else pReq << WCHAR_PARAM("t", GetUserId(hContact));

	Push(pReq);
}

AsyncHttpRequest* CIcqProto::UserInfoRequest(MCONTACT hContact)
{
	auto *pReq = new AsyncHttpRequest(CONN_MAIN, REQUEST_GET, ICQ_API_SERVER "/presence/get", &CIcqProto::OnGetUserInfo);
	pReq->hContact = hContact;
	pReq << AIMSID(this) << INT_PARAM("mdir", 1) << INT_PARAM("capabilities", 1);
	return pReq;
}

void CIcqProto::RetrieveUserHistory(MCONTACT hContact, __int64 startMsgId, __int64 endMsgId)
{
	if (startMsgId == 0)
		startMsgId = -1;
	if (endMsgId == 0)
		endMsgId = -1;

	if (endMsgId != -1 && startMsgId >= endMsgId)
		return;

	auto *pReq = new AsyncHttpRequest(CONN_RAPI, REQUEST_POST, ICQ_ROBUST_SERVER, &CIcqProto::OnGetUserHistory);
	pReq->flags |= NLHRF_NODUMPSEND;
	pReq->hContact = hContact;

	JSONNode request, params; params.set_name("params");
	params << WCHAR_PARAM("sn", GetUserId(hContact)) << INT64_PARAM("fromMsgId", startMsgId);
	if (endMsgId != -1)
		params << INT64_PARAM("tillMsgId", endMsgId);
	params << INT_PARAM("count", 1000) << CHAR_PARAM("aimSid", m_aimsid) << CHAR_PARAM("patchVersion", "1") << CHAR_PARAM("language", "ru-ru");
	request << CHAR_PARAM("method", "getHistory") << CHAR_PARAM("reqId", pReq->m_reqId) << params;
	pReq->m_szParam = ptrW(json_write(&request));
	Push(pReq);
}

void CIcqProto::SetServerStatus(int iStatus)
{
	const char *szStatus = "online";
	int invisible = 0;

	switch (iStatus) {
	case ID_STATUS_OFFLINE: szStatus = "offline"; break;
	case ID_STATUS_NA: szStatus = "occupied"; break;
	case ID_STATUS_AWAY:
	case ID_STATUS_DND: szStatus = "away"; break;
	case ID_STATUS_INVISIBLE:
		invisible = 1;
	}

	Push(new AsyncHttpRequest(CONN_MAIN, REQUEST_GET, ICQ_API_SERVER "/presence/setState")
		<< AIMSID(this) << CHAR_PARAM("view", szStatus) << INT_PARAM("invisible", invisible));

	if (iStatus == ID_STATUS_OFFLINE)
		Push(new AsyncHttpRequest(CONN_MAIN, REQUEST_GET, ICQ_API_SERVER "/aim/endSession") << AIMSID(this));

	int iOldStatus = m_iStatus; m_iStatus = iStatus;
	ProtoBroadcastAck(0, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)iOldStatus, m_iStatus);
}

void CIcqProto::ShutdownSession()
{
	if (m_bTerminated)
		return;

	debugLogA("CIcqProto::ShutdownSession");

	// shutdown all resources
	while (!IsQueueEmpty())
		Sleep(50);

	if (m_hWorkerThread)
		SetEvent(m_evRequestsQueue);

	OnLoggedOut();

	for (auto &it : m_ConnPool) {
		if (it.s) {
			Netlib_Shutdown(it.s);
			it.s = nullptr;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

#define EVENTS "myInfo,presence,buddylist,typing,dataIM,userAddedToBuddyList,mchat,hist,hiddenChat,diff,permitDeny,imState,notification,apps"
#define FIELDS "aimId,buddyIcon,bigBuddyIcon,iconId,bigIconId,largeIconId,displayId,friendly,offlineMsg,state,statusMsg,userType,phoneNumber,cellNumber,smsNumber,workNumber,otherNumber,capabilities,ssl,abPhoneNumber,moodIcon,lastName,abPhones,abContactName,lastseen,mute,livechat,official"

void CIcqProto::StartSession()
{
	ptrA szDeviceId(getStringA("DeviceId"));
	if (szDeviceId == nullptr) {
		UUID deviceId;
		UuidCreate(&deviceId);
		RPC_CSTR szId;
		UuidToStringA(&deviceId, &szId);
		szDeviceId = mir_strdup((char*)szId);
		setString("DeviceId", szDeviceId);
		RpcStringFreeA(&szId);
	}

	int ts = TS();
	CMStringA nonce(FORMAT, "%d-2", ts);
	CMStringA caps(WIM_CAP_UNIQ_REQ_ID "," WIM_CAP_EMOJI "," WIM_CAP_MAIL_NOTIFICATIONS "," WIM_CAP_INTRO_DLG_STATE);
	if (g_bSecureIM) {
		caps.AppendChar(',');
		caps.Append(NG_CAP_SECUREIM);
	}

	MFileVersion v;
	Miranda_GetFileVersion(&v);
	caps.AppendFormat(",%02x%02x%02x%02x%02x%02x%02x%02x%04x%04x%04x%04x", 'M', 'i', 'N', 'G',
		__MAJOR_VERSION, __MINOR_VERSION, __RELEASE_NUM, __BUILD_NUM, v[0], v[1], v[2], v[3]);

	auto *pReq = new AsyncHttpRequest(CONN_MAIN, REQUEST_POST, ICQ_API_SERVER "/aim/startSession", &CIcqProto::OnStartSession);
	pReq << CHAR_PARAM("a", m_szAToken) << INT_PARAM("activeTimeout", 180) << CHAR_PARAM("assertCaps", caps)
		<< INT_PARAM("buildNumber", __BUILD_NUM) << CHAR_PARAM("deviceId", szDeviceId) << CHAR_PARAM("events", EVENTS)
		<< CHAR_PARAM("f", "json") << CHAR_PARAM("imf", "plain") << CHAR_PARAM("inactiveView", "offline")
		<< CHAR_PARAM("includePresenceFields", FIELDS) << CHAR_PARAM("invisible", "false")
		<< CHAR_PARAM("k", ICQ_APP_ID) << INT_PARAM("mobile", 0) << CHAR_PARAM("nonce", nonce) << CHAR_PARAM("r", pReq->m_reqId)
		<< INT_PARAM("rawMsg", 0) << INT_PARAM("sessionTimeout", 7776000) << INT_PARAM("ts", ts) << CHAR_PARAM("view", "online");

	CalcHash(pReq);

	Push(pReq);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CIcqProto::OnAddBuddy(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	JsonReply root(pReply);
	if (root.error() == 200) {
		RetrieveUserInfo(pReq->hContact);
		db_unset(pReq->hContact, "CList", "NotOnList");
	}
}

void CIcqProto::OnAddClient(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	bool *pRet = (bool*)pReq->pUserInfo;

	RobustReply reply(pReply);
	if (reply.error() != 20000) {
		*pRet = false;
		return;
	}

	const JSONNode &results = reply.results();
	m_iRClientId = results["clientId"].as_int();
	setDword(DB_KEY_RCLIENTID, m_iRClientId);
	*pRet = true;
}

void CIcqProto::OnCheckPassword(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest*)
{
	JsonReply root(pReply);
	switch (root.error()) {
	case 200:
		break;

	case 330:
	case 440:
		ConnectionFailed(LOGINERR_WRONGPASSWORD, root.error());
		return;

	default:
		ConnectionFailed(LOGINERR_WRONGPROTOCOL, root.error());
		return;
	}

	JSONNode &data = root.data();
	m_szAToken = data["token"]["a"].as_mstring();
	mir_urlDecode(m_szAToken.GetBuffer());
	setString(DB_KEY_ATOKEN, m_szAToken);

	CMStringA szSessionSecret = data["sessionSecret"].as_mstring();
	CMStringA szPassTemp = m_szPassword;

	unsigned int len;
	BYTE hashOut[MIR_SHA256_HASH_SIZE];
	HMAC(EVP_sha256(), szPassTemp, szPassTemp.GetLength(), (BYTE*)szSessionSecret.c_str(), szSessionSecret.GetLength(), hashOut, &len);
	m_szSessionKey = ptrA(mir_base64_encode(hashOut, sizeof(hashOut)));
	setString(DB_KEY_SESSIONKEY, m_szSessionKey);

	CMStringW szUin = data["loginId"].as_mstring();
	if (szUin)
		m_szOwnId = szUin;

	int srvTS = data["hostTime"].as_int();
	m_iTimeShift = (srvTS) ? time(0) - srvTS : 0;

	StartSession();
}

void CIcqProto::OnFileContinue(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pOld)
{
	IcqFileTransfer *pTransfer = (IcqFileTransfer*)pOld->pUserInfo;

	switch (pReply->resultCode) {
	case 200: // final ok
	case 206: // partial ok
		break;

	default:
		ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_FAILED, pTransfer);
		delete pTransfer;
		return;
	}

	// file transfer succeeded?
	if (pTransfer->pfts.currentFileProgress == pTransfer->pfts.currentFileSize) {
		FileReply root(pReply);
		if (root.error() == 200) {
			ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_SUCCESS, pTransfer);

			const JSONNode &data = root.data();
			CMStringW wszUrl(data["static_url"].as_mstring());

			JSONNode bundle, contents; contents.set_name("captionedContent");
			contents << WCHAR_PARAM("caption", pTransfer->m_wszDescr) << WCHAR_PARAM("url", wszUrl);
			bundle << CHAR_PARAM("mediaType", "text") << CHAR_PARAM("text", "") << contents;
			CMStringW wszParts(FORMAT, L"[%s]", ptrW(json_write(&bundle)).get());

			if (!pTransfer->m_wszDescr.IsEmpty())
				wszUrl += L" " + pTransfer->m_wszDescr;

			int id = InterlockedIncrement(&m_msgId);
			auto *pReq = new AsyncHttpRequest(CONN_MAIN, REQUEST_POST, ICQ_API_SERVER "/im/sendIM", &CIcqProto::OnSendMessage);

			auto *pOwn = new IcqOwnMessage(pTransfer->pfts.hContact, id, pReq->m_reqId);
			pReq->pUserInfo = pOwn;
			{
				mir_cslock lck(m_csOwnIds);
				m_arOwnIds.insert(pOwn);
			}

			pReq << AIMSID(this) << CHAR_PARAM("a", m_szAToken) << CHAR_PARAM("k", ICQ_APP_ID) << CHAR_PARAM("mentions", "") << WCHAR_PARAM("message", wszUrl)
				<< CHAR_PARAM("offlineIM", "true") << WCHAR_PARAM("parts", wszParts) << WCHAR_PARAM("t", GetUserId(pTransfer->pfts.hContact)) << INT_PARAM("ts", TS());
			Push(pReq);
		}
		else ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_FAILED, pTransfer);
		delete pTransfer;
		return;
	}

	// else send the next portion
	auto *pReq = new AsyncHttpRequest(CONN_NONE, REQUEST_POST, pTransfer->m_szHost, &CIcqProto::OnFileContinue);
	pReq << CHAR_PARAM("a", m_szAToken) << CHAR_PARAM("client", "icq") << CHAR_PARAM("k", ICQ_APP_ID) << INT_PARAM("ts", TS());
	CalcHash(pReq);
	pReq->m_szUrl.AppendChar('?');
	pReq->m_szUrl += pReq->m_szParam; pReq->m_szParam.Empty();
	pReq->pUserInfo = pTransfer;
	pTransfer->FillHeaders(pReq);
	Push(pReq);

	pTransfer->pfts.currentFileTime = time(0);
	ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_DATA, pTransfer, (LPARAM)&pTransfer->pfts);
}

void CIcqProto::OnFileInit(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pOld)
{
	IcqFileTransfer *pTransfer = (IcqFileTransfer*)pOld->pUserInfo;

	FileReply root(pReply);
	if (root.error() != 200) {
		ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_FAILED, pTransfer);
		delete pTransfer;
		return;
	}

	ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_INITIALISING, pTransfer);

	const JSONNode &data = root.data();
	CMStringW wszHost(data["host"].as_mstring());
	CMStringW wszUrl(data["url"].as_mstring());
	pTransfer->m_szHost = L"https://" + wszHost + wszUrl;

	auto *pReq = new AsyncHttpRequest(CONN_NONE, REQUEST_POST, pTransfer->m_szHost, &CIcqProto::OnFileContinue);
	pReq << CHAR_PARAM("a", m_szAToken) << CHAR_PARAM("client", "icq") << CHAR_PARAM("k", ICQ_APP_ID) << INT_PARAM("ts", TS());
	CalcHash(pReq);
	pReq->m_szUrl.AppendChar('?');
	pReq->m_szUrl += pReq->m_szParam; pReq->m_szParam.Empty();
	pReq->pUserInfo = pTransfer;
	pTransfer->FillHeaders(pReq);
	Push(pReq);

	pTransfer->pfts.currentFileTime = time(0);
	ProtoBroadcastAck(pTransfer->pfts.hContact, ACKTYPE_FILE, ACKRESULT_DATA, pTransfer, (LPARAM)&pTransfer->pfts);
}

void CIcqProto::OnGetUserHistory(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	RobustReply root(pReply);
	if (root.error() != 20000)
		return;

	__int64 lastMsgId = getId(pReq->hContact, DB_KEY_LASTMSGID);

	const JSONNode &results = root.results();
	for (auto &it : results["messages"])
		ParseMessage(pReq->hContact, lastMsgId, it, true);

	setId(pReq->hContact, DB_KEY_LASTMSGID, lastMsgId);
}

void CIcqProto::OnGetUserInfo(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	JsonReply root(pReply);
	if (root.error() != 200) {
		ProtoBroadcastAck(pReq->hContact, ACKTYPE_GETINFO, ACKRESULT_FAILED, nullptr);
		return;
	}

	const JSONNode &data = root.data();
	for (auto &it : data["users"])
		ParseBuddyInfo(it, pReq->hContact);

	ProtoBroadcastAck(pReq->hContact, ACKTYPE_GETINFO, ACKRESULT_SUCCESS, nullptr);
}

void CIcqProto::OnStartSession(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *)
{
	JsonReply root(pReply);
	switch (root.error()) {
	case 200:
		break;

	case 451:
		// session forcibly closed from site
		delSetting(DB_KEY_ATOKEN);
		delSetting(DB_KEY_SESSIONKEY);
		CheckPassword();
		return;

	case 401:
		if (root.detail() == 1002) { // session expired
			delSetting(DB_KEY_ATOKEN);
			delSetting(DB_KEY_SESSIONKEY);
			CheckPassword();
		}
		else ConnectionFailed(LOGINERR_WRONGPASSWORD, root.error());
		return;

	case 400:
		if (root.detail() == 1015 && m_iTimeShift == 0) { // wrong timestamp
			JSONNode &data = root.data();
			int srvTS = data["ts"].as_int();
			m_iTimeShift = (srvTS) ? time(0) - srvTS : 0;
			StartSession();
			return;
		}
		__fallthrough;

	default:
		ConnectionFailed(LOGINERR_WRONGPROTOCOL, root.error());
		return;
	}

	JSONNode &data = root.data();
	m_fetchBaseURL = data["fetchBaseURL"].as_mstring();
	m_aimsid = data["aimsid"].as_mstring();

	int srvTS = data["ts"].as_int();
	m_iTimeShift = (srvTS) ? time(0) - srvTS : 0;

	OnLoggedIn();

	for (auto &it : data["events"])
		ProcessEvent(it);

	if (m_hPollThread == nullptr)
		m_hPollThread = ForkThreadEx(&CIcqProto::PollThread, 0, 0);
}

void CIcqProto::OnReceiveAvatar(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	PROTO_AVATAR_INFORMATION ai = {};
	ai.hContact = pReq->hContact;

	if (pReply->resultCode != 200 || pReply->pData == nullptr) {
LBL_Error:
		ProtoBroadcastAck(pReq->hContact, ACKTYPE_AVATAR, ACKRESULT_FAILED, HANDLE(&ai), 0);
		return;
	}

	const wchar_t *pwszExtension;
	ai.format = ProtoGetBufferFormat(pReply->pData, &pwszExtension);
	setByte(pReq->hContact, "AvatarType", ai.format);
	GetAvatarFileName(pReq->hContact, ai.filename, _countof(ai.filename));

	FILE *out = _wfopen(ai.filename, L"wb");
	if (out == nullptr)
		goto LBL_Error;

	fwrite(pReply->pData, pReply->dataLength, 1, out);
	fclose(out);

	if (pReq->hContact != 0) {
		ProtoBroadcastAck(pReq->hContact, ACKTYPE_AVATAR, ACKRESULT_SUCCESS, HANDLE(&ai), 0);
		debugLogW(L"Broadcast new avatar: %s", ai.filename);
	}
	else ReportSelfAvatarChanged();
}

void CIcqProto::OnSearchResults(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	RobustReply root(pReply);
	if (root.error() != 20000) {
		ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_FAILED, (HANDLE)pReq, 0);
		return;
	}

	const JSONNode &results = root.results();

	PROTOSEARCHRESULT psr = {};
	psr.cbSize = sizeof(psr);
	psr.flags = PSR_UNICODE;
	for (auto &it : results["data"]) {
		const JSONNode &anketa = it["anketa"];

		CMStringW wszId = it["sn"].as_mstring();
		CMStringW wszNick = anketa["nickname"].as_mstring();
		CMStringW wszFirst = anketa["firstName"].as_mstring();
		CMStringW wszLast = anketa["lastName"].as_mstring();

		psr.id.w = wszId.GetBuffer();
		psr.nick.w = wszNick.GetBuffer();
		psr.firstName.w = wszFirst.GetBuffer();
		psr.lastName.w = wszLast.GetBuffer();
		ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_DATA, (HANDLE)pReq, LPARAM(&psr));
	}

	ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, (HANDLE)pReq);
}

void CIcqProto::OnSendMessage(NETLIBHTTPREQUEST *pReply, AsyncHttpRequest *pReq)
{
	IcqOwnMessage *ownMsg = (IcqOwnMessage*)pReq->pUserInfo;

	JsonReply root(pReply);
	if (root.error() != 200) {
		ProtoBroadcastAck(ownMsg->m_hContact, ACKTYPE_MESSAGE, ACKRESULT_FAILED, (HANDLE)ownMsg->m_msgid, 0);

		mir_cslock lck(m_csOwnIds);
		m_arOwnIds.remove(ownMsg);
	}

	const JSONNode &data = root.data();
	CMStringA reqId(root.requestId());
	CMStringA msgId(data["histMsgId"].as_mstring());
	CheckOwnMessage(reqId, msgId, false);
	CheckLastId(ownMsg->m_hContact, data);
}
