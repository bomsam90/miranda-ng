#pragma once

struct CDbxSQLite : public MDatabaseCommon, public MZeroedObject
{
private:
	sqlite3 *m_db;

	HANDLE hContactAddedEvent;
	HANDLE hContactDeletedEvent;
	HANDLE hEventAddedEvent;
	HANDLE hEventEditedEvent;
	HANDLE hEventDeletedEvent;
	HANDLE hEventFilterAddedEvent;
	HANDLE hEventMarkedRead;
	HANDLE hSettingChangeEvent;

	CDbxSQLite(sqlite3 *database);

	void InitContacts();
	void UninitContacts();

	void InitEvents();
	void UninitEvents();

	void InitSettings();
	void UninitSettings();

public:
	~CDbxSQLite();

	static int Create(const wchar_t *profile);
	static int Check(const wchar_t *profile);
	static MDatabaseCommon* Load(const wchar_t *profile, int readonly);

	STDMETHODIMP_(BOOL)     IsRelational(void) override;
	STDMETHODIMP_(void)     SetCacheSafetyMode(BOOL) override;

	STDMETHODIMP_(LONG)     GetContactCount(void) override;
	STDMETHODIMP_(LONG)     DeleteContact(MCONTACT contactID) override;
	STDMETHODIMP_(MCONTACT) AddContact(void) override;
	STDMETHODIMP_(BOOL)     IsDbContact(MCONTACT contactID) override;
	STDMETHODIMP_(LONG)     GetContactSize(void) override;

	STDMETHODIMP_(LONG)     GetEventCount(MCONTACT contactID) override;
	STDMETHODIMP_(MEVENT)   AddEvent(MCONTACT contactID, DBEVENTINFO *dbe) override;
	STDMETHODIMP_(BOOL)     DeleteEvent(MCONTACT contactID, MEVENT hDbEvent) override;
	STDMETHODIMP_(BOOL)     EditEvent(MCONTACT contactID, MEVENT hDbEvent, DBEVENTINFO *dbe) override;
	STDMETHODIMP_(LONG)     GetBlobSize(MEVENT hDbEvent) override;
	STDMETHODIMP_(BOOL)     GetEvent(MEVENT hDbEvent, DBEVENTINFO *dbe) override;
	STDMETHODIMP_(BOOL)     MarkEventRead(MCONTACT contactID, MEVENT hDbEvent) override;
	STDMETHODIMP_(MCONTACT) GetEventContact(MEVENT hDbEvent) override;
	STDMETHODIMP_(MEVENT)   FindFirstEvent(MCONTACT contactID) override;
	STDMETHODIMP_(MEVENT)   FindFirstUnreadEvent(MCONTACT contactID) override;
	STDMETHODIMP_(MEVENT)   FindLastEvent(MCONTACT contactID) override;
	STDMETHODIMP_(MEVENT)   FindNextEvent(MCONTACT contactID, MEVENT hDbEvent) override;
	STDMETHODIMP_(MEVENT)   FindPrevEvent(MCONTACT contactID, MEVENT hDbEvent) override;

	STDMETHODIMP_(MEVENT)   GetEventById(LPCSTR szModule, LPCSTR szId) override;
	STDMETHODIMP_(BOOL)     SetEventId(LPCSTR szModule, MEVENT, LPCSTR szId) override;

	STDMETHODIMP_(BOOL)     EnumModuleNames(DBMODULEENUMPROC pFunc, void *pParam) override;

	STDMETHODIMP_(BOOL)     GetContactSettingWorker(MCONTACT contactID, LPCSTR szModule, LPCSTR szSetting, DBVARIANT *dbv, int isStatic) override;
	STDMETHODIMP_(BOOL)     WriteContactSetting(MCONTACT contactID, DBCONTACTWRITESETTING *dbcws) override;
	STDMETHODIMP_(BOOL)     DeleteContactSetting(MCONTACT contactID, LPCSTR szModule, LPCSTR szSetting) override;
	STDMETHODIMP_(BOOL)     EnumContactSettings(MCONTACT hContact, DBSETTINGENUMPROC pfnEnumProc, const char *szModule, void *param) override;

	STDMETHODIMP_(BOOL)     MetaMergeHistory(DBCachedContact *ccMeta, DBCachedContact *ccSub) override;
	STDMETHODIMP_(BOOL)     MetaSplitHistory(DBCachedContact *ccMeta, DBCachedContact *ccSub) override;

	STDMETHODIMP_(BOOL)     Compact() override;
	STDMETHODIMP_(BOOL)     Backup(LPCWSTR) override;
};