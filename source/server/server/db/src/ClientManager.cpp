#include "stdafx.h"
#include "../../common/building.h"
#include "../../common/VnumHelper.h"
#include "../../libgame/libgame/grid.h"

#include "ClientManager.h"

#include "Main.h"
#include "Config.h"
#include "DBManager.h"
#include "QID.h"
#include "GuildManager.h"
#include "PrivManager.h"
#include "MoneyLog.h"
#include "ItemAwardManager.h"
#include "Marriage.h"
#include "ItemIDRangeManager.h"
#include "Cache.h"

extern int g_iPlayerCacheFlushSeconds;
extern int g_iItemCacheFlushSeconds;
extern int g_test_server;
extern int g_log;
extern std::string g_stLocale;
extern std::string g_stLocaleNameColumn;
bool CreateItemTableFromRes(MYSQL_RES* res, std::vector<TPlayerItem>* pVec, DWORD dwPID);

DWORD g_dwUsageMax = 0;
DWORD g_dwUsageAvg = 0;

CPacketInfo g_query_info;
CPacketInfo g_item_info;

int g_item_count = 0;
int g_query_count[2];

CClientManager::CClientManager() :
	m_pkAuthPeer(nullptr),
	m_iPlayerIDStart(0),
	m_iPlayerDeleteLevelLimit(0),
	m_iPlayerDeleteLevelLimitLower(0),
	m_iShopTableSize(0),
	m_pShopTable(nullptr),
	m_iRefineTableSize(0),
	m_pRefineTable(nullptr),
	m_bShutdowned(FALSE),
	m_iCacheFlushCount(0),
	m_iCacheFlushCountLimit(200)
{
	m_itemRange.dwMin = 0;
	m_itemRange.dwMax = 0;
	m_itemRange.dwUsableItemIDMin = 0;

	memset(g_query_count, 0, sizeof(g_query_count));
}

CClientManager::~CClientManager()
{
	Destroy();
}

void CClientManager::SetPlayerIDStart(int iIDStart)
{
	m_iPlayerIDStart = iIDStart;
}

void CClientManager::Destroy()
{
	m_mChannelStatus.clear();
	for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
		(*i)->Destroy();

	m_peerList.clear();

	if (m_fdAccept > 0)
	{
		socket_close(m_fdAccept);
		m_fdAccept = -1;
	}
}

#ifdef ENABLE_DEFAULT_PRIV_SYSTEM
static bool bCleanOldPriv = true;
static bool __InitializeDefaultPriv()
{
	if (bCleanOldPriv)
	{
		std::unique_ptr<SQLMsg> pCleanStuff(CDBManager::instance().DirectQuery("DELETE FROM priv_settings WHERE value <= 0 OR duration <= NOW();", SQL_COMMON));
		printf("DEFAULT_PRIV_EMPIRE: removed %u expired priv settings.\n", pCleanStuff->Get()->uiAffectedRows);
	}
	std::unique_ptr<SQLMsg> pMsg(CDBManager::instance().DirectQuery("SELECT priv_type, id, type, value, UNIX_TIMESTAMP(duration) FROM priv_settings", SQL_COMMON));
	if (pMsg->Get()->uiNumRows == 0)
		return false;
	MYSQL_ROW row = nullptr;
	while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
	{
		if (!strcmp(row[0], "EMPIRE"))
		{
			// init
			BYTE empire = 0;
			BYTE type = 1;
			int value = 0;
			time_t duration_sec = 0;
			// set
			str_to_number(empire, row[1]);
			str_to_number(type, row[2]);
			str_to_number(value, row[3]);
			str_to_number(duration_sec, row[4]);
			// recalibrate time
			time_t now_time_sec = CClientManager::instance().GetCurrentTime();
			if (now_time_sec > duration_sec)
				duration_sec = 0;
			else
				duration_sec -= now_time_sec;
			// send priv
			printf("DEFAULT_PRIV_EMPIRE: set empire(%u), type(%u), value(%d), duration(%u)\n", empire, type, value, duration_sec);
			CPrivManager::instance().AddEmpirePriv(empire, type, value, duration_sec);
		}
		else if (!strcmp(row[0], "GUILD"))
		{
			// init
			DWORD guild_id = 0;
			BYTE type = 1;
			int value = 0;
			time_t duration_sec = 0;
			// set
			str_to_number(guild_id, row[1]);
			str_to_number(type, row[2]);
			str_to_number(value, row[3]);
			str_to_number(duration_sec, row[4]);
			// recalibrate time
			time_t now_time_sec = CClientManager::instance().GetCurrentTime();
			if (now_time_sec > duration_sec)
				duration_sec = 0;
			else
				duration_sec -= now_time_sec;
			// send priv
			if (guild_id)
			{
				printf("DEFAULT_PRIV_GUILD: set guild_id(%u), type(%u), value(%d), duration(%u)\n", guild_id, type, value, duration_sec);
				CPrivManager::instance().AddGuildPriv(guild_id, type, value, duration_sec);
			}
		}
		else if (!strcmp(row[0], "PLAYER"))
		{
			// init
			DWORD pid = 0;
			BYTE type = 1;
			int value = 0;
			// set
			str_to_number(pid, row[1]);
			str_to_number(type, row[2]);
			str_to_number(value, row[3]);
			// send priv
			if (pid)
			{
				printf("DEFAULT_PRIV_PLAYER: set pid(%u), type(%u), value(%d)\n", pid, type, value);
				CPrivManager::instance().AddCharPriv(pid, type, value);
			}
		}
	}
	return true;
}

static bool __UpdateDefaultPriv(const char* priv_type, DWORD id, BYTE type, int value, time_t duration_sec)
{
	char szQuery[1024];
	snprintf(szQuery, 1024,
		"REPLACE INTO priv_settings SET priv_type='%s', id=%u, type=%u, value=%d, duration=DATE_ADD(NOW(), INTERVAL %u SECOND);",
		priv_type, id, type, value, duration_sec
	);
	std::unique_ptr<SQLMsg> pMsg(CDBManager::instance().DirectQuery(szQuery, SQL_COMMON));
	return pMsg->Get()->uiAffectedRows;
}
#endif

bool CClientManager::Initialize()
{
	int tmpValue;

	//BOOT_LOCALIZATION
	if (!InitializeLocalization())
	{
		fprintf(stderr, "Failed Localization Infomation so exit\n");
		return false;
	}

#ifdef ENABLE_DEFAULT_PRIV_SYSTEM
	if (!__InitializeDefaultPriv())
	{
		fprintf(stderr, "Failed Default Priv Setting so exit\n");
		// return false;
	}
#endif
	//END_BOOT_LOCALIZATION
	//ITEM_UNIQUE_ID

	if (!InitializeNowItemID())
	{
		fprintf(stderr, " Item range Initialize Failed. Exit DBCache Server\n");
		return false;
	}
	//END_ITEM_UNIQUE_ID

	if (!InitializeTables())
	{
		sys_err("Table Initialize FAILED");
		return false;
	}

	CGuildManager::instance().BootReserveWar();

	if (!CConfig::instance().GetValue("BIND_PORT", &tmpValue))
		tmpValue = 5300;

	char szBindIP[128];

	if (!CConfig::instance().GetValue("BIND_IP", szBindIP, 128))
		strlcpy(szBindIP, "0", sizeof(szBindIP));

	m_fdAccept = socket_tcp_bind(szBindIP, tmpValue);

	if (m_fdAccept < 0)
	{
		perror("socket");
		return false;
	}

	sys_log(0, "ACCEPT_HANDLE: %u", m_fdAccept);
	fdwatch_add_fd(m_fdWatcher, m_fdAccept, nullptr, FDW_READ, false);

	if (!CConfig::instance().GetValue("BACKUP_LIMIT_SEC", &tmpValue))
		tmpValue = 600;

	m_looping = true;

	if (!CConfig::instance().GetValue("PLAYER_DELETE_LEVEL_LIMIT", &m_iPlayerDeleteLevelLimit))
	{
		sys_err("conf.txt: Cannot find PLAYER_DELETE_LEVEL_LIMIT, use default level %d", PLAYER_MAX_LEVEL_CONST + 1);
		m_iPlayerDeleteLevelLimit = PLAYER_MAX_LEVEL_CONST + 1;
	}

	if (!CConfig::instance().GetValue("PLAYER_DELETE_LEVEL_LIMIT_LOWER", &m_iPlayerDeleteLevelLimitLower))
	{
		m_iPlayerDeleteLevelLimitLower = 0;
	}

	sys_log(0, "PLAYER_DELETE_LEVEL_LIMIT set to %d", m_iPlayerDeleteLevelLimit);
	sys_log(0, "PLAYER_DELETE_LEVEL_LIMIT_LOWER set to %d", m_iPlayerDeleteLevelLimitLower);

	LoadEventFlag();

	// database character-set을 강제로 맞춤
	if (g_stLocale == "big5" || g_stLocale == "sjis")
		CDBManager::instance().QueryLocaleSet();

	return true;
}

void CClientManager::MainLoop()
{
	SQLMsg* tmp;

	sys_log(0, "ClientManager pointer is %p", this);

	// 메인루프
	while (!m_bShutdowned)
	{
		while ((tmp = CDBManager::instance().PopResult()))
		{
			AnalyzeQueryResult(tmp);
			delete tmp;
		}

		if (!Process())
			break;

		log_rotate();
	}

	//
	// 메인루프 종료처리
	//
	sys_log(0, "MainLoop exited, Starting cache flushing");

	signal_timer_disable();

	auto it = m_map_playerCache.begin();

	//플레이어 테이블 캐쉬 플러쉬
	while (it != m_map_playerCache.end())
	{
		CPlayerTableCache* c = (it++)->second;

		c->Flush();
		delete c;
	}
	m_map_playerCache.clear();

	auto it2 = m_map_itemCache.begin();
	//아이템 플러쉬
	while (it2 != m_map_itemCache.end())
	{
		CItemCache* c = (it2++)->second;

		c->Flush();
		delete c;
	}
	m_map_itemCache.clear();

	// MYSHOP_PRICE_LIST
	//
	// 개인상점 아이템 가격 리스트 Flush
	//
	for (auto itPriceList = m_mapItemPriceListCache.begin(); itPriceList != m_mapItemPriceListCache.end(); ++itPriceList)
	{
		CItemPriceListTableCache* pCache = itPriceList->second;
		pCache->Flush();
	}

	m_mapItemPriceListCache.clear();
	// END_OF_MYSHOP_PRICE_LIST
}

void CClientManager::Quit()
{
	m_bShutdowned = TRUE;
}

void CClientManager::QUERY_BOOT(CPeer * peer, TPacketGDBoot * p)
{
	const BYTE bPacketVersion = 6; // BOOT 패킷이 바뀔때마다 번호를 올리도록 한다.

	std::vector<tAdminInfo> vAdmin;
	std::vector<std::string> vHost;

	__GetHostInfo(vHost);
	__GetAdminInfo(p->szIP, vAdmin);

	//sys_log(0, "QUERY_BOOT : AdminInfo (Request ServerIp %s) ", p->szIP);

	DWORD dwPacketSize =
		sizeof(DWORD) +
		sizeof(BYTE) +
		sizeof(WORD) + sizeof(WORD) + sizeof(TMobTable) * m_vec_mobTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(TItemTable) * m_vec_itemTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(TShopTable) * m_iShopTableSize +
		sizeof(WORD) + sizeof(WORD) + sizeof(TSkillTable) * m_vec_skillTable.size() +
#ifdef ENABLE_GEM_SYSTEM
		sizeof(WORD) + sizeof(WORD) + sizeof(TGemShopTable) * m_iGemShopTableSize +
#endif
#ifdef ENABLE_BOSS_GEM_SYSTEM
		sizeof(WORD) + sizeof(WORD) + sizeof(TGemShopTable) * m_iGemBossShopTableSize +
#endif
		sizeof(WORD) + sizeof(WORD) + sizeof(TRefineTable) * m_iRefineTableSize +
		sizeof(WORD) + sizeof(WORD) + sizeof(TItemAttrTable) * m_vec_itemAttrTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(TItemAttrTable) * m_vec_itemRareTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(TBanwordTable) * m_vec_banwordTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(building::TLand) * m_vec_kLandTable.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(building::TObjectProto) * m_vec_kObjectProto.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(building::TObject) * m_map_pkObjectTable.size() +
		sizeof(time_t) +
		sizeof(WORD) + sizeof(WORD) + sizeof(TItemIDRangeTable) * 2 +
		//ADMIN_MANAGER
		sizeof(WORD) + sizeof(WORD) + 16 * vHost.size() +
		sizeof(WORD) + sizeof(WORD) + sizeof(tAdminInfo) * vAdmin.size() +
		//END_ADMIN_MANAGER
		sizeof(WORD);

	peer->EncodeHeader(HEADER_DG_BOOT, 0, dwPacketSize);
	peer->Encode(&dwPacketSize, sizeof(DWORD));
	peer->Encode(&bPacketVersion, sizeof(BYTE));

	sys_log(0, "BOOT: PACKET: %d", dwPacketSize);
	sys_log(0, "BOOT: VERSION: %d", bPacketVersion);

	sys_log(0, "sizeof(TMobTable) = %d", sizeof(TMobTable));
	sys_log(0, "sizeof(TItemTable) = %d", sizeof(TItemTable));
	sys_log(0, "sizeof(TShopTable) = %d", sizeof(TShopTable));
	sys_log(0, "sizeof(TSkillTable) = %d", sizeof(TSkillTable));
#ifdef ENABLE_GEM_SYSTEM
	sys_log(0, "sizeof(TGemShopTable) = %d", sizeof(TGemShopTable));
#endif
#ifdef ENABLE_BOSS_GEM_SYSTEM
	sys_log(0, "sizeof(TGemShopTable) = %d", sizeof(TGemShopTable));
#endif
	sys_log(0, "sizeof(TRefineTable) = %d", sizeof(TRefineTable));
	sys_log(0, "sizeof(TItemAttrTable) = %d", sizeof(TItemAttrTable));
	sys_log(0, "sizeof(TItemRareTable) = %d", sizeof(TItemAttrTable));
	sys_log(0, "sizeof(TBanwordTable) = %d", sizeof(TBanwordTable));
	sys_log(0, "sizeof(TLand) = %d", sizeof(building::TLand));
	sys_log(0, "sizeof(TObjectProto) = %d", sizeof(building::TObjectProto));
	sys_log(0, "sizeof(TObject) = %d", sizeof(building::TObject));
	//ADMIN_MANAGER
	sys_log(0, "sizeof(tAdminInfo) = %d * %d ", sizeof(tAdminInfo) * vAdmin.size());
	//END_ADMIN_MANAGER

	peer->EncodeWORD(sizeof(TMobTable));
	peer->EncodeWORD(m_vec_mobTable.size());
	peer->Encode(&m_vec_mobTable[0], sizeof(TMobTable) * m_vec_mobTable.size());

	peer->EncodeWORD(sizeof(TItemTable));
	peer->EncodeWORD(m_vec_itemTable.size());
	peer->Encode(&m_vec_itemTable[0], sizeof(TItemTable) * m_vec_itemTable.size());

	peer->EncodeWORD(sizeof(TShopTable));
	peer->EncodeWORD(m_iShopTableSize);
	peer->Encode(m_pShopTable, sizeof(TShopTable) * m_iShopTableSize);

	peer->EncodeWORD(sizeof(TSkillTable));
	peer->EncodeWORD(m_vec_skillTable.size());
	peer->Encode(&m_vec_skillTable[0], sizeof(TSkillTable) * m_vec_skillTable.size());

#ifdef ENABLE_GEM_SYSTEM
	peer->EncodeWORD(sizeof(TGemShopTable));
	peer->EncodeWORD(m_iGemShopTableSize);
	peer->Encode(m_pGemShopTable, sizeof(TGemShopTable) * m_iGemShopTableSize);
#endif

#ifdef ENABLE_BOSS_GEM_SYSTEM
	peer->EncodeWORD(sizeof(TGemShopTable));
	peer->EncodeWORD(m_iGemBossShopTableSize);
	peer->Encode(m_pGemBossShopTable, sizeof(TGemShopTable) * m_iGemBossShopTableSize);
#endif

	peer->EncodeWORD(sizeof(TRefineTable));
	peer->EncodeWORD(m_iRefineTableSize);
	peer->Encode(m_pRefineTable, sizeof(TRefineTable) * m_iRefineTableSize);

	peer->EncodeWORD(sizeof(TItemAttrTable));
	peer->EncodeWORD(m_vec_itemAttrTable.size());
	peer->Encode(&m_vec_itemAttrTable[0], sizeof(TItemAttrTable) * m_vec_itemAttrTable.size());

	peer->EncodeWORD(sizeof(TItemAttrTable));
	peer->EncodeWORD(m_vec_itemRareTable.size());
	peer->Encode(&m_vec_itemRareTable[0], sizeof(TItemAttrTable) * m_vec_itemRareTable.size());

	peer->EncodeWORD(sizeof(TBanwordTable));
	peer->EncodeWORD(m_vec_banwordTable.size());
	peer->Encode(&m_vec_banwordTable[0], sizeof(TBanwordTable) * m_vec_banwordTable.size());

	peer->EncodeWORD(sizeof(building::TLand));
	peer->EncodeWORD(m_vec_kLandTable.size());
	peer->Encode(&m_vec_kLandTable[0], sizeof(building::TLand) * m_vec_kLandTable.size());

	peer->EncodeWORD(sizeof(building::TObjectProto));
	peer->EncodeWORD(m_vec_kObjectProto.size());
	peer->Encode(&m_vec_kObjectProto[0], sizeof(building::TObjectProto) * m_vec_kObjectProto.size());

	peer->EncodeWORD(sizeof(building::TObject));
	peer->EncodeWORD(m_map_pkObjectTable.size());

	auto it = m_map_pkObjectTable.begin();

	while (it != m_map_pkObjectTable.end())
		peer->Encode((it++)->second, sizeof(building::TObject));

	time_t now = time(0);
	peer->Encode(&now, sizeof(time_t));

	TItemIDRangeTable itemRange = CItemIDRangeManager::instance().GetRange();
	TItemIDRangeTable itemRangeSpare = CItemIDRangeManager::instance().GetRange();

	peer->EncodeWORD(sizeof(TItemIDRangeTable));
	peer->EncodeWORD(1);
	peer->Encode(&itemRange, sizeof(TItemIDRangeTable));
	peer->Encode(&itemRangeSpare, sizeof(TItemIDRangeTable));

	peer->SetItemIDRange(itemRange);
	peer->SetSpareItemIDRange(itemRangeSpare);

	//ADMIN_MANAGER
	peer->EncodeWORD(16);
	peer->EncodeWORD(vHost.size());

	for (size_t n = 0; n < vHost.size(); ++n)
	{
		peer->Encode(vHost[n].c_str(), 16);
		sys_log(0, "GMHosts %s", vHost[n].c_str());
	}

	peer->EncodeWORD(sizeof(tAdminInfo));
	peer->EncodeWORD(vAdmin.size());

	for (size_t n = 0; n < vAdmin.size(); ++n)
	{
		peer->Encode(&vAdmin[n], sizeof(tAdminInfo));
		sys_log(0, "Admin name %s ConntactIP %s", vAdmin[n].m_szName, vAdmin[n].m_szContactIP);
	}
	//END_ADMIN_MANAGER

	peer->EncodeWORD(0xffff);
}

void CClientManager::SendPartyOnSetup(CPeer * pkPeer)
{
	TPartyMap& pm = m_map_pkChannelParty[pkPeer->GetChannel()];

	for (auto it_party = pm.begin(); it_party != pm.end(); ++it_party)
	{
		sys_log(0, "PARTY SendPartyOnSetup Party [%u]", it_party->first);
		pkPeer->EncodeHeader(HEADER_DG_PARTY_CREATE, 0, sizeof(TPacketPartyCreate));
		pkPeer->Encode(&it_party->first, sizeof(DWORD));

		for (auto it_member = it_party->second.begin(); it_member != it_party->second.end(); ++it_member)
		{
			sys_log(0, "PARTY SendPartyOnSetup Party [%u] Member [%u]", it_party->first, it_member->first);
			pkPeer->EncodeHeader(HEADER_DG_PARTY_ADD, 0, sizeof(TPacketPartyAdd));
			pkPeer->Encode(&it_party->first, sizeof(DWORD));
			pkPeer->Encode(&it_member->first, sizeof(DWORD));
			pkPeer->Encode(&it_member->second.bRole, sizeof(BYTE));

			pkPeer->EncodeHeader(HEADER_DG_PARTY_SET_MEMBER_LEVEL, 0, sizeof(TPacketPartySetMemberLevel));
			pkPeer->Encode(&it_party->first, sizeof(DWORD));
			pkPeer->Encode(&it_member->first, sizeof(DWORD));
			pkPeer->Encode(&it_member->second.bLevel, sizeof(BYTE));
		}
	}
}

void CClientManager::QUERY_PLAYER_COUNT(CPeer * pkPeer, TPlayerCountPacket * pPacket)
{
	pkPeer->SetUserCount(pPacket->dwCount);
}

void CClientManager::QUERY_QUEST_SAVE(CPeer * pkPeer, TQuestTable * pTable, DWORD dwLen)
{
	if (0 != (dwLen % sizeof(TQuestTable)))
	{
		sys_err("invalid packet size %d, sizeof(TQuestTable) == %d", dwLen, sizeof(TQuestTable));
		return;
	}

	int iSize = dwLen / sizeof(TQuestTable);

	char szQuery[1024];

	for (int i = 0; i < iSize; ++i, ++pTable)
	{
		if (pTable->lValue == 0)
		{
			snprintf(szQuery, sizeof(szQuery),
				"DELETE FROM quest%s WHERE dwPID=%d AND szName='%s' AND szState='%s'",
				GetTablePostfix(), pTable->dwPID, pTable->szName, pTable->szState);
		}
		else
		{
			snprintf(szQuery, sizeof(szQuery),
				"REPLACE INTO quest%s (dwPID, szName, szState, lValue) VALUES(%d, '%s', '%s', %ld)",
				GetTablePostfix(), pTable->dwPID, pTable->szName, pTable->szState, pTable->lValue);
		}

		CDBManager::instance().ReturnQuery(szQuery, QID_QUEST_SAVE, pkPeer->GetHandle(), nullptr);
	}
}

void CClientManager::QUERY_SAFEBOX_LOAD(CPeer * pkPeer, DWORD dwHandle, TSafeboxLoadPacket * packet, bool bMall)
{
	ClientHandleInfo* pi = new ClientHandleInfo(dwHandle);
	strlcpy(pi->safebox_password, packet->szPassword, sizeof(pi->safebox_password));
	pi->account_id = packet->dwID;
	pi->account_index = 0;
	pi->ip[0] = bMall ? 1 : 0;
	strlcpy(pi->login, packet->szLogin, sizeof(pi->login));

	char szQuery[QUERY_MAX_LEN];
	snprintf(szQuery, sizeof(szQuery),
		"SELECT account_id, size, password, gold, cheque FROM safebox%s WHERE account_id=%u",
		GetTablePostfix(), packet->dwID);

	if (g_log)
		sys_log(0, "HEADER_GD_SAFEBOX_LOAD (handle: %d account.id %u is_mall %d)", dwHandle, packet->dwID, bMall ? 1 : 0);

	CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_LOAD, pkPeer->GetHandle(), pi);
}

void CClientManager::RESULT_SAFEBOX_LOAD(CPeer * pkPeer, SQLMsg * msg)
{
	CQueryInfo* qi = (CQueryInfo*)msg->pvUserData;
	ClientHandleInfo* pi = (ClientHandleInfo*)qi->pvData;
	DWORD dwHandle = pi->dwHandle;

	if (pi->account_index == 0)
	{
		char szSafeboxPassword[SAFEBOX_PASSWORD_MAX_LEN + 1];
		strlcpy(szSafeboxPassword, pi->safebox_password, sizeof(szSafeboxPassword));

		TSafeboxTable* pSafebox = new TSafeboxTable;
		memset(pSafebox, 0, sizeof(TSafeboxTable));

		SQLResult* res = msg->Get();
		if (res->uiNumRows == 0)
		{
			if (strcmp("000000", szSafeboxPassword))
			{
				pkPeer->EncodeHeader(HEADER_DG_SAFEBOX_WRONG_PASSWORD, dwHandle, 0);
				delete pSafebox;
				delete pi;
				return;
			}
		}
		else
		{
			MYSQL_ROW row = mysql_fetch_row(res->pSQLResult);
			if (((!row[2] || !*row[2]) && strcmp("000000", szSafeboxPassword)) || ((row[2] && *row[2]) && strcmp(row[2], szSafeboxPassword)))
			{
				pkPeer->EncodeHeader(HEADER_DG_SAFEBOX_WRONG_PASSWORD, dwHandle, 0);
				delete pSafebox;
				delete pi;
				return;
			}

			if (!row[0])
				pSafebox->dwID = 0;
			else
				str_to_number(pSafebox->dwID, row[0]);

			if (!row[1])
				pSafebox->bSize = 0;
			else
				str_to_number(pSafebox->bSize, row[1]);
			
			if (!row[3])
			   pSafebox->llGold = 0;
			else
				str_to_number(pSafebox->llGold, row[3]);
			
			if (!row[4])
			   pSafebox->wCheque = 0;
			else
				str_to_number(pSafebox->wCheque, row[4]);
			/*
			   if (!row[3])
			   pSafebox->dwGold = 0;
			   else
			   pSafebox->dwGold = atoi(row[3]);
			   */
			if (pi->ip[0] == 1)
			{
				pSafebox->bSize = 1;
				sys_log(0, "MALL id[%d] size[%d]", pSafebox->dwID, pSafebox->bSize);
			}
			else
				sys_log(0, "SAFEBOX id[%d] size[%d]", pSafebox->dwID, pSafebox->bSize);
		}

		if (0 == pSafebox->dwID)
			pSafebox->dwID = pi->account_id;

		pi->pSafebox = pSafebox;
		char szQuery[756];
#ifdef ENABLE_CHANGELOOK_SYSTEM
		snprintf(szQuery, sizeof(szQuery), "SELECT id, window+0, pos, count, vnum, socket0, socket1, socket2, socket3, attrtype0, attrvalue0, attrtype1, attrvalue1, attrtype2, attrvalue2, attrtype3, attrvalue3, attrtype4, attrvalue4, attrtype5, attrvalue5, attrtype6, attrvalue6, applytype0, applyvalue0, applytype1, applyvalue1, applytype2, applyvalue2, applytype3, applyvalue3, applytype4, applyvalue4, applytype5, applyvalue5, applytype6, applyvalue6, applytype7, applyvalue7, is_basic, transmutation, sealbind FROM item%s WHERE owner_id=%d AND window='%s'", GetTablePostfix(), pi->account_id, pi->ip[0] == 0 ? "SAFEBOX" : "MALL");
#else
		snprintf(szQuery, sizeof(szQuery), "SELECT id, window+0, pos, count, vnum, socket0, socket1, socket2, socket3, attrtype0, attrvalue0, attrtype1, attrvalue1, attrtype2, attrvalue2, attrtype3, attrvalue3, attrtype4, attrvalue4, attrtype5, attrvalue5, attrtype6, attrvalue6, applytype0, applyvalue0, applytype1, applyvalue1, applytype2, applyvalue2, applytype3, applyvalue3, applytype4, applyvalue4, applytype5, applyvalue5, applytype6, applyvalue6, applytype7, applyvalue7, sealbind FROM item%s WHERE owner_id=%d AND window='%s'", GetTablePostfix(), pi->account_id, pi->ip[0] == 0 ? "SAFEBOX" : "MALL");
#endif
		pi->account_index = 1;
		CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_LOAD, pkPeer->GetHandle(), pi);
	}
	else
	{
		if (!pi->pSafebox)
		{
			sys_err("nullptr safebox pointer!");
			delete pi;
			return;
		}

		if (!msg->Get()->pSQLResult)
		{
			sys_err("nullptr safebox result");
			delete pi;
			return;
		}

		static std::vector<TPlayerItem> s_items;
		CreateItemTableFromRes(msg->Get()->pSQLResult, &s_items, pi->account_id);

		std::set<TItemAward*>* pSet = ItemAwardManager::instance().GetByLogin(pi->login);
		if (pSet && !m_vec_itemTable.empty())
		{
			CGrid grid(5, MAX(1, pi->pSafebox->bSize) * 9);
			bool bEscape = false;
			for (DWORD i = 0; i < s_items.size(); ++i)
			{
				TPlayerItem& r = s_items[i];
				auto it = m_map_itemTableByVnum.find(r.vnum);
				if (it == m_map_itemTableByVnum.end())
				{
					bEscape = true;
					sys_err("invalid item vnum %u in safebox: login %s", r.vnum, pi->login);
					break;
				}

				grid.Put(r.pos, 1, it->second->bSize);
			}

			if (!bEscape)
			{
				std::vector<std::pair<DWORD, DWORD> > vec_dwFinishedAwardID;
				decltype(pSet->begin()) it = pSet->begin();
				char szQuery[512];
				while (it != pSet->end())
				{
					TItemAward* pItemAward = *(it++);
					const DWORD& dwItemVnum = pItemAward->dwVnum;
					if (pItemAward->bTaken)
						continue;

					if (pi->ip[0] == 0 && pItemAward->bMall)
						continue;

					if (pi->ip[0] == 1 && !pItemAward->bMall)
						continue;

					auto it = m_map_itemTableByVnum.find(pItemAward->dwVnum);
					if (it == m_map_itemTableByVnum.end())
					{
						sys_err("invalid item vnum %u in item_award: login %s", pItemAward->dwVnum, pi->login);
						continue;
					}

					TItemTable* pItemTable = it->second;

					int iPos;
					if ((iPos = grid.FindBlank(1, it->second->bSize)) == -1)
						break;

					TPlayerItem item;
					memset(&item, 0, sizeof(TPlayerItem));
					#ifdef ENABLE_EXTEND_ITEM_AWARD
					DWORD dwSocket2 = pItemAward->dwSocket2;
					#else
					DWORD dwSocket2 = 0;
					#endif
					if (pItemTable->bType == ITEM_UNIQUE)
					{
#ifdef ENABLE_EXTEND_ITEM_AWARD
						// 12.04.2019 - Correction for unique items based on the real time.
						const long lValue0 = pItemTable->alValues[ITEM_SOCKET_REMAIN_SEC];
						const long lValue2 = pItemTable->alValues[ITEM_SOCKET_UNIQUE_REMAIN_TIME];
						const time_t tNow = CClientManager::instance().GetCurrentTime();
						dwSocket2 = (lValue2 == 0) ? static_cast<DWORD>(lValue0) : static_cast<DWORD>(tNow + lValue0);
#else
						if (pItemAward->dwSocket2 != 0)
							dwSocket2 = pItemAward->dwSocket2;
						else
							dwSocket2 = pItemTable->alValues[0];
#endif
					}
					else if ((dwItemVnum == 50300 || dwItemVnum == 70037) && pItemAward->dwSocket0 == 0)
					{
						DWORD dwSkillIdx;
						DWORD dwSkillVnum;
						do
						{
							dwSkillIdx = number(0, m_vec_skillTable.size() - 1);
							dwSkillVnum = m_vec_skillTable[dwSkillIdx].dwVnum;
							if (!dwSkillVnum || dwSkillVnum > 120)
								continue;

							break;
						} while (1);

						#ifndef ENABLE_EXTEND_ITEM_AWARD
						pItemAward->dwSocket0 = dwSkillVnum;
						#endif
					}
					else
					{
						switch (dwItemVnum)
						{
						case 72723: case 72724: case 72725: case 72726:
						case 72727: case 72728: case 72729: case 72730:
						case 76004: case 76005: case 76021: case 76022:
						case 79012: case 79013:
							if (pItemAward->dwSocket2 == 0)
							{
								dwSocket2 = pItemTable->alValues[0];
							}
							else
							{
								dwSocket2 = pItemAward->dwSocket2;
							}
							break;
						}
					}

					if (GetItemID() > m_itemRange.dwMax)
					{
						sys_err("UNIQUE ID OVERFLOW!!");
						break;
					}

					{
						auto it = m_map_itemTableByVnum.find(dwItemVnum);
						if (it == m_map_itemTableByVnum.end())
						{
							sys_err("Invalid item(vnum : %d). It is not in m_map_itemTableByVnum.", dwItemVnum);
							continue;
						}

						TItemTable* item_table = it->second;
						if (item_table == nullptr)
						{
							sys_err("Invalid item_table (vnum : %d). It's value is nullptr in m_map_itemTableByVnum.", dwItemVnum);
							continue;
						}

						if (0 == pItemAward->dwSocket0)
						{
							for (int i = 0; i < ITEM_LIMIT_MAX_NUM; i++)
							{
								if (LIMIT_REAL_TIME == item_table->aLimits[i].bType)
								{
									if (0 == item_table->aLimits[i].lValue)
										pItemAward->dwSocket0 = time(0) + 60 * 60 * 24 * 7;
									else
										pItemAward->dwSocket0 = time(0) + item_table->aLimits[i].lValue;

									break;
								}
								else if (LIMIT_REAL_TIME_START_FIRST_USE == item_table->aLimits[i].bType || LIMIT_TIMER_BASED_ON_WEAR == item_table->aLimits[i].bType)
								{
									if (0 == item_table->aLimits[i].lValue)
										pItemAward->dwSocket0 = 60 * 60 * 24 * 7;
									else
										pItemAward->dwSocket0 = item_table->aLimits[i].lValue;

									break;
								}
							}
						}

#ifdef ENABLE_EXTEND_ITEM_AWARD
						ItemAwardManager::instance().CheckItemCount(*pItemAward, *pItemTable);
						ItemAwardManager::instance().CheckItemBlend(*pItemAward, *pItemTable);
						ItemAwardManager::instance().CheckItemAddonType(*pItemAward, *pItemTable);
						ItemAwardManager::instance().CheckItemSkillBook(*pItemAward, m_vec_skillTable);
						#ifdef USE_ITEM_AWARD_CHECK_ATTRIBUTES
						ItemAwardManager::instance().CheckItemAttributes(*pItemAward, *pItemTable, m_vec_itemAttrTable);
						#endif
						
						// START_OF_AUTO_QUERY
						char szColumns[QUERY_MAX_LEN], szValues[QUERY_MAX_LEN];
						
						int	iLen = snprintf(szColumns, sizeof(szColumns), "id, owner_id, window, pos, vnum, count");
						int	iValueLen = snprintf(szValues, sizeof(szValues), "%u, %u, '%s', %d, %u, %u", GainItemID(), pi->account_id, (pi->ip[0] == 0) ? "SAFEBOX" : "MALL", iPos, pItemAward->dwVnum, pItemAward->dwCount);

						iLen += snprintf(szColumns + iLen, sizeof(szColumns) - iLen, ", socket0, socket1, socket2, socket3");
						iValueLen += snprintf(szValues + iValueLen, sizeof(szValues) - iValueLen, ", %u, %u, %u, %u", pItemAward->dwSocket0, pItemAward->dwSocket1, dwSocket2, pItemAward->dwSocket3);

						for (size_t i = 0; i < ITEM_ATTRIBUTE_MAX_NUM; ++i)
						{
							iLen += snprintf(szColumns + iLen, sizeof(szColumns) - iLen, ", attrtype%d, attrvalue%d", i, i);
							iValueLen += snprintf(szValues + iValueLen, sizeof(szValues) - iValueLen, ", %d, %d", pItemAward->aAttr[i].bType, pItemAward->aAttr[i].sValue);
						}
						// END_OF_AUTO_QUERY
						
						snprintf(szQuery, sizeof(szQuery), "INSERT INTO item%s (%s) VALUES(%s)", GetTablePostfix(), szColumns, szValues);
#else
						snprintf(szQuery, sizeof(szQuery),
								"INSERT INTO item%s (id, owner_id, window, pos, vnum, count, socket0, socket1, socket2) "
								"VALUES(%u, %u, '%s', %d, %u, %u, %u, %u, %u)",
								GetTablePostfix(),
								GainItemID(),
								pi->account_id,
								pi->ip[0] == 0 ? "SAFEBOX" : "MALL",
								iPos,
								pItemAward->dwVnum, pItemAward->dwCount, pItemAward->dwSocket0, pItemAward->dwSocket1, dwSocket2);
#endif
					}

					std::unique_ptr<SQLMsg> pmsg(CDBManager::instance().DirectQuery(szQuery));
					SQLResult* pRes = pmsg->Get();
					sys_log(0, "SAFEBOX Query : [%s]", szQuery);
					if (pRes->uiAffectedRows == 0 || pRes->uiInsertID == 0 || pRes->uiAffectedRows == (uint32_t)-1)
						break;

					item.id = pmsg->Get()->uiInsertID;
					item.window = pi->ip[0] == 0 ? SAFEBOX : MALL,
					item.pos = iPos;
					item.count = pItemAward->dwCount;
					item.vnum = pItemAward->dwVnum;
					item.alSockets[0] = pItemAward->dwSocket0;
					item.alSockets[1] = pItemAward->dwSocket1;
					item.alSockets[2] = dwSocket2;
					item.alSockets[3] = pItemAward->dwSocket3;
#ifdef ENABLE_EXTEND_ITEM_AWARD
					thecore_memcpy(&item.aAttr, pItemAward->aAttr, sizeof(item.aAttr));
#endif
					s_items.push_back(item);

					vec_dwFinishedAwardID.push_back(std::make_pair(pItemAward->dwID, item.id));
					grid.Put(iPos, 1, it->second->bSize);
				}

				for (DWORD i = 0; i < vec_dwFinishedAwardID.size(); ++i)
					ItemAwardManager::instance().Taken(vec_dwFinishedAwardID[i].first, vec_dwFinishedAwardID[i].second);
			}
		}

		pi->pSafebox->wItemCount = s_items.size();

		pkPeer->EncodeHeader(pi->ip[0] == 0 ? HEADER_DG_SAFEBOX_LOAD : HEADER_DG_MALL_LOAD, dwHandle, sizeof(TSafeboxTable) + sizeof(TPlayerItem) * s_items.size());
		pkPeer->Encode(pi->pSafebox, sizeof(TSafeboxTable));
		if (!s_items.empty())
			pkPeer->Encode(&s_items[0], sizeof(TPlayerItem) * s_items.size());

		delete pi;
	}
}

void CClientManager::QUERY_SAFEBOX_CHANGE_SIZE(CPeer * pkPeer, DWORD dwHandle, TSafeboxChangeSizePacket * p)
{
	ClientHandleInfo* pi = new ClientHandleInfo(dwHandle);
	pi->account_index = p->bSize;	// account_index를 사이즈로 임시로 사용

	char szQuery[QUERY_MAX_LEN];

	if (p->bSize == 1)
		snprintf(szQuery, sizeof(szQuery), "INSERT INTO safebox%s (account_id, size) VALUES(%u, %u)", GetTablePostfix(), p->dwID, p->bSize);
	else
		snprintf(szQuery, sizeof(szQuery), "UPDATE safebox%s SET size=%u WHERE account_id=%u", GetTablePostfix(), p->bSize, p->dwID);

	CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_CHANGE_SIZE, pkPeer->GetHandle(), pi);
}

void CClientManager::RESULT_SAFEBOX_CHANGE_SIZE(CPeer * pkPeer, SQLMsg * msg)
{
	CQueryInfo* qi = (CQueryInfo*)msg->pvUserData;
	ClientHandleInfo* p = (ClientHandleInfo*)qi->pvData;
	DWORD dwHandle = p->dwHandle;
	BYTE bSize = p->account_index;

	delete p;

	if (msg->Get()->uiNumRows > 0)
	{
		pkPeer->EncodeHeader(HEADER_DG_SAFEBOX_CHANGE_SIZE, dwHandle, sizeof(BYTE));
		pkPeer->EncodeBYTE(bSize);
	}
}

void CClientManager::QUERY_SAFEBOX_CHANGE_PASSWORD(CPeer * pkPeer, DWORD dwHandle, TSafeboxChangePasswordPacket * p)
{
	ClientHandleInfo* pi = new ClientHandleInfo(dwHandle);
	strlcpy(pi->safebox_password, p->szNewPassword, sizeof(pi->safebox_password));
	strlcpy(pi->login, p->szOldPassword, sizeof(pi->login));
	pi->account_id = p->dwID;

	char szQuery[QUERY_MAX_LEN];
	snprintf(szQuery, sizeof(szQuery), "SELECT password FROM safebox%s WHERE account_id=%u", GetTablePostfix(), p->dwID);

	CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_CHANGE_PASSWORD, pkPeer->GetHandle(), pi);
}

void CClientManager::RESULT_SAFEBOX_CHANGE_PASSWORD(CPeer * pkPeer, SQLMsg * msg)
{
	CQueryInfo* qi = (CQueryInfo*)msg->pvUserData;
	ClientHandleInfo* p = (ClientHandleInfo*)qi->pvData;
	DWORD dwHandle = p->dwHandle;

	if (msg->Get()->uiNumRows > 0)
	{
		MYSQL_ROW row = mysql_fetch_row(msg->Get()->pSQLResult);

		if ((row[0] && *row[0] && !strcasecmp(row[0], p->login)) || ((!row[0] || !*row[0]) && !strcmp("000000", p->login)))
		{
			char szQuery[QUERY_MAX_LEN];
			char escape_pwd[64];
			CDBManager::instance().EscapeString(escape_pwd, p->safebox_password, strlen(p->safebox_password));

			snprintf(szQuery, sizeof(szQuery), "UPDATE safebox%s SET password='%s' WHERE account_id=%u", GetTablePostfix(), escape_pwd, p->account_id);

			CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_CHANGE_PASSWORD_SECOND, pkPeer->GetHandle(), p);
			return;
		}
	}

	delete p;

	// Wrong old password
	pkPeer->EncodeHeader(HEADER_DG_SAFEBOX_CHANGE_PASSWORD_ANSWER, dwHandle, sizeof(BYTE));
	pkPeer->EncodeBYTE(0);
}

void CClientManager::RESULT_SAFEBOX_CHANGE_PASSWORD_SECOND(CPeer * pkPeer, SQLMsg * msg)
{
	CQueryInfo* qi = (CQueryInfo*)msg->pvUserData;
	ClientHandleInfo* p = (ClientHandleInfo*)qi->pvData;
	DWORD dwHandle = p->dwHandle;
	delete p;

	pkPeer->EncodeHeader(HEADER_DG_SAFEBOX_CHANGE_PASSWORD_ANSWER, dwHandle, sizeof(BYTE));
	pkPeer->EncodeBYTE(1);
}

// MYSHOP_PRICE_LIST
void CClientManager::RESULT_PRICELIST_LOAD(CPeer * peer, SQLMsg * pMsg)
{
	TItemPricelistReqInfo* pReqInfo = (TItemPricelistReqInfo*)static_cast<CQueryInfo*>(pMsg->pvUserData)->pvData;

	//
	// DB 에서 로드한 정보를 Cache 에 저장
	//

	TItemPriceListTable table;
	table.dwOwnerID = pReqInfo->second;
	table.byCount = 0;

	MYSQL_ROW row;

	while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
	{
		str_to_number(table.aPriceInfo[table.byCount].dwVnum, row[0]);
#ifdef ENABLE_CHEQUE_SYSTEM
		str_to_number(table.aPriceInfo[table.byCount].price.dwPrice, row[1]);
		str_to_number(table.aPriceInfo[table.byCount].price.byChequePrice, row[2]);
#else
		str_to_number(table.aPriceInfo[table.byCount].dwPrice, row[1]);
#endif
		table.byCount++;
	}

	PutItemPriceListCache(&table);

	//
	// 로드한 데이터를 Game server 에 전송
	//

	TPacketMyshopPricelistHeader header;

	header.dwOwnerID = pReqInfo->second;
	header.byCount = table.byCount;

	size_t sizePriceListSize = sizeof(TItemPriceInfo) * header.byCount;

	peer->EncodeHeader(HEADER_DG_MYSHOP_PRICELIST_RES, pReqInfo->first, sizeof(header) + sizePriceListSize);
	peer->Encode(&header, sizeof(header));
	peer->Encode(table.aPriceInfo, sizePriceListSize);

	sys_log(0, "Load MyShopPricelist handle[%d] pid[%d] count[%d]", pReqInfo->first, pReqInfo->second, header.byCount);

	delete pReqInfo;
}

void CClientManager::RESULT_PRICELIST_LOAD_FOR_UPDATE(SQLMsg * pMsg)
{
	TItemPriceListTable* pUpdateTable = (TItemPriceListTable*)static_cast<CQueryInfo*>(pMsg->pvUserData)->pvData;

	//
	// DB 에서 로드한 정보를 Cache 에 저장
	//

	TItemPriceListTable table;
	table.dwOwnerID = pUpdateTable->dwOwnerID;
	table.byCount = 0;

	MYSQL_ROW row;

	while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
	{
		str_to_number(table.aPriceInfo[table.byCount].dwVnum, row[0]);
#ifdef ENABLE_CHEQUE_SYSTEM
		str_to_number(table.aPriceInfo[table.byCount].price.dwPrice, row[1]);
		str_to_number(table.aPriceInfo[table.byCount].price.byChequePrice, row[2]);
#else
		str_to_number(table.aPriceInfo[table.byCount].dwPrice, row[1]);
#endif
		table.byCount++;
	}

	PutItemPriceListCache(&table);

	// Update cache
	GetItemPriceListCache(pUpdateTable->dwOwnerID)->UpdateList(pUpdateTable);

	delete pUpdateTable;
}
// END_OF_MYSHOP_PRICE_LIST

void CClientManager::QUERY_SAFEBOX_SAVE(CPeer * pkPeer, TSafeboxTable * pTable)
{
	char szQuery[QUERY_MAX_LEN];
	snprintf(szQuery, sizeof(szQuery), "UPDATE safebox%s SET gold=gold,cheque=cheque WHERE account_id=%u", GetTablePostfix(), pTable->dwID);
	CDBManager::instance().ReturnQuery(szQuery, QID_SAFEBOX_SAVE, pkPeer->GetHandle(), nullptr);
}

void CClientManager::QUERY_EMPIRE_SELECT(CPeer * pkPeer, DWORD dwHandle, TEmpireSelectPacket * p)
{
	char szQuery[QUERY_MAX_LEN];
	snprintf(szQuery, sizeof(szQuery), "UPDATE player_index%s SET empire=%u WHERE id=%u", GetTablePostfix(), p->bEmpire, p->dwAccountID);
	delete CDBManager::instance().DirectQuery(szQuery);
	sys_log(0, "EmpireSelect: %s", szQuery);
	{
		snprintf(szQuery, sizeof(szQuery), "SELECT pid1, pid2, pid3, pid4, pid5 FROM player_index%s WHERE id=%u", GetTablePostfix(), p->dwAccountID);
		std::unique_ptr<SQLMsg> pmsg(CDBManager::instance().DirectQuery(szQuery));
		SQLResult* pRes = pmsg->Get();
		if (pRes->uiNumRows)
		{
			sys_log(0, "EMPIRE %lu", pRes->uiNumRows);
			MYSQL_ROW row = mysql_fetch_row(pRes->pSQLResult);
			DWORD pids[3];

			UINT g_start_map[4] =
			{
				0,
				1,
				21,
				41
			};

			DWORD g_start_position[4][2] =
			{
				{0, 0},
				{469300, 964200},
				{55700, 157900},
				{969600, 278400}
			};

			for (int i = 0; i < 3; ++i)
			{
				str_to_number(pids[i], row[i]);
				sys_log(0, "EMPIRE PIDS[%d]", pids[i]);
				if (pids[i])
				{
					sys_log(0, "EMPIRE move to pid[%d] to villiage of %u, map_index %d", pids[i], p->bEmpire, g_start_map[p->bEmpire]);
					snprintf(szQuery, sizeof(szQuery), "UPDATE player%s SET map_index=%u,x=%u,y=%u WHERE id=%u", GetTablePostfix(), g_start_map[p->bEmpire], g_start_position[p->bEmpire][0], g_start_position[p->bEmpire][1], pids[i]);
					std::unique_ptr<SQLMsg> pmsg2(CDBManager::instance().DirectQuery(szQuery));
				}
			}
		}
	}

	pkPeer->EncodeHeader(HEADER_DG_EMPIRE_SELECT, dwHandle, sizeof(BYTE));
	pkPeer->EncodeBYTE(p->bEmpire);
}

void CClientManager::QUERY_SETUP(CPeer * peer, DWORD dwHandle, const char* c_pData)
{
	TPacketGDSetup* p = (TPacketGDSetup*)c_pData;
	c_pData += sizeof(TPacketGDSetup);

	if (p->bAuthServer)
	{
		sys_log(0, "AUTH_PEER ptr %p", peer);

		m_pkAuthPeer = peer;
		return;
	}

	peer->SetPublicIP(p->szPublicIP);
	peer->SetChannel(p->bChannel);
	peer->SetListenPort(p->wListenPort);
	peer->SetP2PPort(p->wP2PPort);
	peer->SetMaps(p->alMaps);

	//
	// 어떤 맵이 어떤 서버에 있는지 보내기
	//
	TMapLocation kMapLocations;

	strlcpy(kMapLocations.szHost, peer->GetPublicIP(), sizeof(kMapLocations.szHost));
	kMapLocations.wPort = peer->GetListenPort();
	thecore_memcpy(kMapLocations.alMaps, peer->GetMaps(), sizeof(kMapLocations.alMaps));

	BYTE bMapCount;

	std::vector<TMapLocation> vec_kMapLocations;

	if (peer->GetChannel() == 1)
	{
		for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
		{
			CPeer* tmp = *i;

			if (tmp == peer)
				continue;

			if (!tmp->GetChannel())
				continue;

			if (tmp->GetChannel() == 99 || tmp->GetChannel() == peer->GetChannel())
			{
				TMapLocation kMapLocation2;
				strlcpy(kMapLocation2.szHost, tmp->GetPublicIP(), sizeof(kMapLocation2.szHost));
				kMapLocation2.wPort = tmp->GetListenPort();
				thecore_memcpy(kMapLocation2.alMaps, tmp->GetMaps(), sizeof(kMapLocation2.alMaps));
				vec_kMapLocations.push_back(kMapLocation2);

				tmp->EncodeHeader(HEADER_DG_MAP_LOCATIONS, 0, sizeof(BYTE) + sizeof(TMapLocation));
				bMapCount = 1;
				tmp->EncodeBYTE(bMapCount);
				tmp->Encode(&kMapLocations, sizeof(TMapLocation));
			}
		}
	}
	else if (peer->GetChannel() == 99)
	{
		for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
		{
			CPeer* tmp = *i;

			if (tmp == peer)
				continue;

			if (!tmp->GetChannel())
				continue;

			if (tmp->GetChannel() == 1 || tmp->GetChannel() == peer->GetChannel())
			{
				TMapLocation kMapLocation2;
				strlcpy(kMapLocation2.szHost, tmp->GetPublicIP(), sizeof(kMapLocation2.szHost));
				kMapLocation2.wPort = tmp->GetListenPort();
				thecore_memcpy(kMapLocation2.alMaps, tmp->GetMaps(), sizeof(kMapLocation2.alMaps));
				vec_kMapLocations.push_back(kMapLocation2);
			}

			tmp->EncodeHeader(HEADER_DG_MAP_LOCATIONS, 0, sizeof(BYTE) + sizeof(TMapLocation));
			bMapCount = 1;
			tmp->EncodeBYTE(bMapCount);
			tmp->Encode(&kMapLocations, sizeof(TMapLocation));
		}
	}
	else
	{
		for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
		{
			CPeer* tmp = *i;

			if (tmp == peer)
				continue;

			if (!tmp->GetChannel())
				continue;

			if (tmp->GetChannel() == 99 || tmp->GetChannel() == peer->GetChannel())
			{
				TMapLocation kMapLocation2;

				strlcpy(kMapLocation2.szHost, tmp->GetPublicIP(), sizeof(kMapLocation2.szHost));
				kMapLocation2.wPort = tmp->GetListenPort();
				thecore_memcpy(kMapLocation2.alMaps, tmp->GetMaps(), sizeof(kMapLocation2.alMaps));

				vec_kMapLocations.push_back(kMapLocation2);
			}

			if (tmp->GetChannel() == peer->GetChannel())
			{
				tmp->EncodeHeader(HEADER_DG_MAP_LOCATIONS, 0, sizeof(BYTE) + sizeof(TMapLocation));
				bMapCount = 1;
				tmp->EncodeBYTE(bMapCount);
				tmp->Encode(&kMapLocations, sizeof(TMapLocation));
			}
		}
	}

	vec_kMapLocations.push_back(kMapLocations);

	peer->EncodeHeader(HEADER_DG_MAP_LOCATIONS, 0, sizeof(BYTE) + sizeof(TMapLocation) * vec_kMapLocations.size());
	bMapCount = vec_kMapLocations.size();
	peer->EncodeBYTE(bMapCount);
	peer->Encode(&vec_kMapLocations[0], sizeof(TMapLocation) * vec_kMapLocations.size());

	//
	// 셋업 : 접속한 피어에 다른 피어들이 접속하게 만든다. (P2P 컨넥션 생성)
	//
	sys_log(0, "SETUP: channel %u listen %u p2p %u count %u", peer->GetChannel(), p->wListenPort, p->wP2PPort, bMapCount);

	TPacketDGP2P p2pSetupPacket;
	p2pSetupPacket.wPort = peer->GetP2PPort();
	p2pSetupPacket.bChannel = peer->GetChannel();
	strlcpy(p2pSetupPacket.szHost, peer->GetPublicIP(), sizeof(p2pSetupPacket.szHost));

	for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (tmp == peer)
			continue;

		// 채널이 0이라면 아직 SETUP 패킷이 오지 않은 피어 또는 auth라고 간주할 수 있음
		if (0 == tmp->GetChannel())
			continue;

		tmp->EncodeHeader(HEADER_DG_P2P, 0, sizeof(TPacketDGP2P));
		tmp->Encode(&p2pSetupPacket, sizeof(TPacketDGP2P));
	}

	//
	// 로그인 및 빌링정보 보내기
	//
	TPacketLoginOnSetup * pck = (TPacketLoginOnSetup*)c_pData;;

	for (DWORD c = 0; c < p->dwLoginCount; ++c, ++pck)
	{
		CLoginData* pkLD = new CLoginData;

		pkLD->SetKey(pck->dwLoginKey);
		pkLD->SetClientKey(pck->adwClientKey);
		pkLD->SetIP(pck->szHost);

		TAccountTable& r = pkLD->GetAccountRef();

		r.id = pck->dwID;
		trim_and_lower(pck->szLogin, r.login, sizeof(r.login));
		strlcpy(r.social_id, pck->szSocialID, sizeof(r.social_id));
		strlcpy(r.passwd, "TEMP", sizeof(r.passwd));

		InsertLoginData(pkLD);

		if (InsertLogonAccount(pck->szLogin, peer->GetHandle(), pck->szHost))
		{
			sys_log(0, "SETUP: login %u %s login_key %u host %s", pck->dwID, pck->szLogin, pck->dwLoginKey, pck->szHost);
			pkLD->SetPlay(true);
		}
		else
			sys_log(0, "SETUP: login_fail %u %s login_key %u", pck->dwID, pck->szLogin, pck->dwLoginKey);
	}

	SendPartyOnSetup(peer);
	CGuildManager::instance().OnSetup(peer);
	CPrivManager::instance().SendPrivOnSetup(peer);
	SendEventFlagsOnSetup(peer);
	marriage::CManager::instance().OnSetup(peer);
}

void CClientManager::QUERY_ITEM_FLUSH(CPeer * pkPeer, const char* c_pData)
{
	DWORD dwID = *(DWORD*)c_pData;

	if (g_log)
		sys_log(0, "HEADER_GD_ITEM_FLUSH: %u", dwID);

	CItemCache* c = GetItemCache(dwID);

	if (c)
		c->Flush();
}

void CClientManager::QUERY_ITEM_SAVE(CPeer * pkPeer, const char* c_pData)
{
	TPlayerItem* p = (TPlayerItem*)c_pData;
	if (p->window == SAFEBOX || p->window == MALL)
	{
		CItemCache* c = GetItemCache(p->id);
		if (c)
		{
			TItemCacheSetPtrMap::iterator it = m_map_pkItemCacheSetPtr.find(c->Get()->owner);
			if (it != m_map_pkItemCacheSetPtr.end())
			{
				if (g_test_server)
					sys_log(0, "ITEM_CACHE: safebox owner %u id %u", c->Get()->owner, c->Get()->id);

				it->second->erase(c);
			}

			m_map_itemCache.erase(p->id);
			delete c;
		}

		char szQuery[QUERY_MAX_LEN];
		snprintf(szQuery, sizeof(szQuery),
			"REPLACE INTO item%s (id, owner_id, window, pos, count, vnum, is_basic, "
#ifdef ENABLE_CHANGELOOK_SYSTEM
			"transmutation, "
#endif
			"socket0, socket1, socket2, socket3, "
			"attrtype0, attrvalue0,"
			"attrtype1, attrvalue1,"
			"attrtype2, attrvalue2,"
			"attrtype3, attrvalue3,"
			"attrtype4, attrvalue4,"
			"attrtype5, attrvalue5,"
			"attrtype6, attrvalue6,"
			"applytype0, applyvalue0,"
			"applytype1, applyvalue1,"
			"applytype2, applyvalue2,"
			"applytype3, applyvalue3,"
			"applytype4, applyvalue4,"
			"applytype5, applyvalue5,"
			"applytype6, applyvalue6,"
			"applytype7, applyvalue7,"
			"sealbind)"
			"VALUES(%u, %u, %d, %d, %u, %u, %d,	"
#ifdef ENABLE_CHANGELOOK_SYSTEM
			"%u, "
#endif
			"%ld, %ld, %ld, %ld, "
			"%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %ld)",

			GetTablePostfix(),
			p->id,
			p->owner,
			p->window,
			p->pos,
			p->count,
			p->vnum,
			p->is_basic,
#ifdef ENABLE_CHANGELOOK_SYSTEM
			p->transmutation,
#endif
			p->alSockets[0],
			p->alSockets[1],
			p->alSockets[2],
			p->alSockets[3],
			p->aAttr[0].bType, p->aAttr[0].sValue,
			p->aAttr[1].bType, p->aAttr[1].sValue,
			p->aAttr[2].bType, p->aAttr[2].sValue,
			p->aAttr[3].bType, p->aAttr[3].sValue,
			p->aAttr[4].bType, p->aAttr[4].sValue,
			p->aAttr[5].bType, p->aAttr[5].sValue,
			p->aAttr[6].bType, p->aAttr[6].sValue,
			p->aAttr[7].bType, p->aAttr[7].sValue,
			p->aAttr[8].bType, p->aAttr[8].sValue,
			p->aAttr[9].bType, p->aAttr[9].sValue,
			p->aAttr[10].bType, p->aAttr[10].sValue,
			p->aAttr[11].bType, p->aAttr[11].sValue,
			p->aAttr[12].bType, p->aAttr[12].sValue,
			p->aAttr[13].bType, p->aAttr[13].sValue,
			p->aAttr[14].bType, p->aAttr[14].sValue,
			p->sealbind);

		CDBManager::instance().ReturnQuery(szQuery, QID_ITEM_SAVE, pkPeer->GetHandle(), nullptr);
	}
	else
	{
		if (g_test_server)
			sys_log(0, "QUERY_ITEM_SAVE => PutItemCache() owner %d id %d vnum %d ", p->owner, p->id, p->vnum);

		PutItemCache(p);
	}
}

CClientManager::TItemCacheSet* CClientManager::GetItemCacheSet(DWORD pid)
{
	TItemCacheSetPtrMap::iterator it = m_map_pkItemCacheSetPtr.find(pid);

	if (it == m_map_pkItemCacheSetPtr.end())
		return nullptr;

	return it->second;
}

void CClientManager::CreateItemCacheSet(DWORD pid)
{
	if (m_map_pkItemCacheSetPtr.find(pid) != m_map_pkItemCacheSetPtr.end())
		return;

	TItemCacheSet * pSet = new TItemCacheSet;
	m_map_pkItemCacheSetPtr.insert(TItemCacheSetPtrMap::value_type(pid, pSet));

	if (g_log)
		sys_log(0, "ITEM_CACHE: new cache %u", pid);
}

void CClientManager::FlushItemCacheSet(DWORD pid)
{
	TItemCacheSetPtrMap::iterator it = m_map_pkItemCacheSetPtr.find(pid);

	if (it == m_map_pkItemCacheSetPtr.end())
	{
		sys_log(0, "FLUSH_ITEMCACHESET : No ItemCacheSet pid(%d)", pid);
		return;
	}

	TItemCacheSet* pSet = it->second;
	TItemCacheSet::iterator it_set = pSet->begin();

	while (it_set != pSet->end())
	{
		CItemCache* c = *it_set++;
		c->Flush();

		m_map_itemCache.erase(c->Get()->id);
		delete c;
	}

	pSet->clear();
	delete pSet;

	m_map_pkItemCacheSetPtr.erase(it);

	if (g_log)
		sys_log(0, "FLUSH_ITEMCACHESET : Deleted pid(%d)", pid);
}

CItemCache* CClientManager::GetItemCache(DWORD id)
{
	TItemCacheMap::iterator it = m_map_itemCache.find(id);

	if (it == m_map_itemCache.end())
		return nullptr;

	return it->second;
}

void CClientManager::PutItemCache(TPlayerItem * pNew, bool bSkipQuery)
{
	CItemCache* c;

	c = GetItemCache(pNew->id);

	// 아이템 새로 생성
	if (!c)
	{
		if (g_log)
			sys_log(0, "ITEM_CACHE: PutItemCache ==> New CItemCache id%d vnum%d new owner%d", pNew->id, pNew->vnum, pNew->owner);

		c = new CItemCache;
		m_map_itemCache.insert(TItemCacheMap::value_type(pNew->id, c));
	}
	// 있을시
	else
	{
		if (g_log)
			sys_log(0, "ITEM_CACHE: PutItemCache ==> Have Cache");
		// 소유자가 틀리면
		if (pNew->owner != c->Get()->owner)
		{
			// 이미 이 아이템을 가지고 있었던 유저로 부터 아이템을 삭제한다.
			TItemCacheSetPtrMap::iterator it = m_map_pkItemCacheSetPtr.find(c->Get()->owner);

			if (it != m_map_pkItemCacheSetPtr.end())
			{
				if (g_log)
					sys_log(0, "ITEM_CACHE: delete owner %u id %u new owner %u", c->Get()->owner, c->Get()->id, pNew->owner);
				it->second->erase(c);
			}
		}
	}

	// 새로운 정보 업데이트
	c->Put(pNew, bSkipQuery);

	TItemCacheSetPtrMap::iterator it = m_map_pkItemCacheSetPtr.find(c->Get()->owner);

	if (it != m_map_pkItemCacheSetPtr.end())
	{
		if (g_log)
			sys_log(0, "ITEM_CACHE: save %u id %u", c->Get()->owner, c->Get()->id);
		else
			sys_log(1, "ITEM_CACHE: save %u id %u", c->Get()->owner, c->Get()->id);
		it->second->insert(c);
	}
	else
	{
		// 현재 소유자가 없으므로 바로 저장해야 다음 접속이 올 때 SQL에 쿼리하여
		// 받을 수 있으므로 바로 저장한다.
		if (g_log)
			sys_log(0, "ITEM_CACHE: direct save %u id %u", c->Get()->owner, c->Get()->id);
		else
			sys_log(1, "ITEM_CACHE: direct save %u id %u", c->Get()->owner, c->Get()->id);

		c->OnFlush();
	}
}

bool CClientManager::DeleteItemCache(DWORD dwID)
{
	CItemCache* c = GetItemCache(dwID);

	if (!c)
		return false;

	c->Delete();
	return true;
}

// MYSHOP_PRICE_LIST
CItemPriceListTableCache* CClientManager::GetItemPriceListCache(DWORD dwID)
{
	TItemPriceListCacheMap::iterator it = m_mapItemPriceListCache.find(dwID);

	if (it == m_mapItemPriceListCache.end())
		return nullptr;

	return it->second;
}

void CClientManager::PutItemPriceListCache(const TItemPriceListTable * pItemPriceList)
{
	CItemPriceListTableCache* pCache = GetItemPriceListCache(pItemPriceList->dwOwnerID);

	if (!pCache)
	{
		pCache = new CItemPriceListTableCache;
		m_mapItemPriceListCache.insert(TItemPriceListCacheMap::value_type(pItemPriceList->dwOwnerID, pCache));
	}

	pCache->Put(const_cast<TItemPriceListTable*>(pItemPriceList), true);
}

void CClientManager::UpdatePlayerCache()
{
	TPlayerTableCacheMap::iterator it = m_map_playerCache.begin();

	while (it != m_map_playerCache.end())
	{
		CPlayerTableCache* c = (it++)->second;

		if (c->CheckTimeout())
		{
			if (g_log)
				sys_log(0, "UPDATE : UpdatePlayerCache() ==> FlushPlayerCache %d %s ", c->Get(false)->id, c->Get(false)->name);

			c->Flush();

			// Item Cache도 업데이트
			UpdateItemCacheSet(c->Get()->id);
		}
		else if (c->CheckFlushTimeout())
			c->Flush();
	}
}
// END_OF_MYSHOP_PRICE_LIST

void CClientManager::SetCacheFlushCountLimit(int iLimit)
{
	m_iCacheFlushCountLimit = MAX(10, iLimit);
	sys_log(0, "CACHE_FLUSH_LIMIT_PER_SECOND: %d", m_iCacheFlushCountLimit);
}

void CClientManager::UpdateItemCache()
{
	if (m_iCacheFlushCount >= m_iCacheFlushCountLimit)
		return;

	TItemCacheMap::iterator it = m_map_itemCache.begin();

	while (it != m_map_itemCache.end())
	{
		CItemCache* c = (it++)->second;

		// 아이템은 Flush만 한다.
		if (c->CheckFlushTimeout())
		{
			if (g_test_server)
				sys_log(0, "UpdateItemCache ==> Flush() vnum %d id owner %d", c->Get()->vnum, c->Get()->id, c->Get()->owner);

			c->Flush();

			if (++m_iCacheFlushCount >= m_iCacheFlushCountLimit)
				break;
		}
	}
}

void CClientManager::UpdateItemPriceListCache()
{
	TItemPriceListCacheMap::iterator it = m_mapItemPriceListCache.begin();

	while (it != m_mapItemPriceListCache.end())
	{
		CItemPriceListTableCache* pCache = it->second;

		if (pCache->CheckFlushTimeout())
		{
			pCache->Flush();
			m_mapItemPriceListCache.erase(it++);
		}
		else
			++it;
	}
}

void CClientManager::QUERY_ITEM_DESTROY(CPeer * pkPeer, const char* c_pData)
{
	DWORD dwID = *(DWORD*)c_pData;
	c_pData += sizeof(DWORD);

	DWORD dwPID = *(DWORD*)c_pData;

	if (!DeleteItemCache(dwID))
	{
		char szQuery[64];
		snprintf(szQuery, sizeof(szQuery), "DELETE FROM item%s WHERE id=%u", GetTablePostfix(), dwID);

		if (g_log)
			sys_log(0, "HEADER_GD_ITEM_DESTROY: PID %u ID %u", dwPID, dwID);

		if (dwPID == 0) // 아무도 가진 사람이 없었다면, 비동기 쿼리
			CDBManager::instance().AsyncQuery(szQuery);
		else
			CDBManager::instance().ReturnQuery(szQuery, QID_ITEM_DESTROY, pkPeer->GetHandle(), nullptr);
	}
}

void CClientManager::QUERY_FLUSH_CACHE(CPeer * pkPeer, const char* c_pData)
{
	DWORD dwPID = *(DWORD*)c_pData;

	CPlayerTableCache* pkCache = GetPlayerCache(dwPID);

	if (!pkCache)
		return;

	sys_log(0, "FLUSH_CACHE: %u", dwPID);

	pkCache->Flush();
	FlushItemCacheSet(dwPID);

	m_map_playerCache.erase(dwPID);
	delete pkCache;
}

void CClientManager::QUERY_RELOAD_PROTO()
{
	if (!InitializeTables())
	{
		sys_err("QUERY_RELOAD_PROTO: cannot load tables");
		return;
	}

	for (TPeerList::iterator i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (!tmp->GetChannel())
			continue;

		tmp->EncodeHeader(HEADER_DG_RELOAD_PROTO, 0,
			sizeof(WORD) + sizeof(TSkillTable) * m_vec_skillTable.size() +
			sizeof(WORD) + sizeof(TBanwordTable) * m_vec_banwordTable.size() +
			sizeof(WORD) + sizeof(TItemTable) * m_vec_itemTable.size() +
#ifdef ENABLE_ADVANCED_RELOAD_SYSTEM
			sizeof(WORD) + sizeof(TMobTable) * m_vec_mobTable.size() +
			sizeof(WORD) + sizeof(TShopTable) * m_iShopTableSize +
			sizeof(WORD) + sizeof(TRefineTable) * m_iRefineTableSize +
			sizeof(WORD) + sizeof(TItemAttrTable) * m_vec_itemAttrTable.size() +
			sizeof(WORD) + sizeof(TItemAttrTable) * m_vec_itemRareTable.size());
#else
			sizeof(WORD) + sizeof(TMobTable) * m_vec_mobTable.size());
#endif
		tmp->EncodeWORD(m_vec_skillTable.size());
		tmp->Encode(&m_vec_skillTable[0], sizeof(TSkillTable) * m_vec_skillTable.size());

		tmp->EncodeWORD(m_vec_banwordTable.size());
		tmp->Encode(&m_vec_banwordTable[0], sizeof(TBanwordTable) * m_vec_banwordTable.size());

		tmp->EncodeWORD(m_vec_itemTable.size());
		tmp->Encode(&m_vec_itemTable[0], sizeof(TItemTable) * m_vec_itemTable.size());

		tmp->EncodeWORD(m_vec_mobTable.size());
		tmp->Encode(&m_vec_mobTable[0], sizeof(TMobTable) * m_vec_mobTable.size());
#ifdef ENABLE_ADVANCED_RELOAD_SYSTEM
		tmp->EncodeWORD(m_iShopTableSize);
		tmp->Encode(m_pShopTable, sizeof(TShopTable) * m_iShopTableSize);

		tmp->EncodeWORD(m_iRefineTableSize);
		tmp->Encode(m_pRefineTable, sizeof(TRefineTable) * m_iRefineTableSize);

		tmp->EncodeWORD(m_vec_itemAttrTable.size());
		tmp->Encode(&m_vec_itemAttrTable[0], sizeof(TItemAttrTable) * m_vec_itemAttrTable.size());

		tmp->EncodeWORD(m_vec_itemRareTable.size());
		tmp->Encode(&m_vec_itemRareTable[0], sizeof(TItemAttrTable) * m_vec_itemRareTable.size());
#endif
	}
}

// ADD_GUILD_PRIV_TIME
/**
 * @version	05/06/08 Bang2ni - 지속시간 추가
 */
void CClientManager::AddGuildPriv(TPacketGiveGuildPriv * p)
{
	CPrivManager::instance().AddGuildPriv(p->guild_id, p->type, p->value, p->duration_sec);
#ifdef ENABLE_DEFAULT_PRIV_SYSTEM
	__UpdateDefaultPriv("GUILD", p->guild_id, p->type, p->value, p->duration_sec);
#endif
}

void CClientManager::AddEmpirePriv(TPacketGiveEmpirePriv * p)
{
	CPrivManager::instance().AddEmpirePriv(p->empire, p->type, p->value, p->duration_sec);
#ifdef ENABLE_DEFAULT_PRIV_SYSTEM
	__UpdateDefaultPriv("EMPIRE", p->empire, p->type, p->value, p->duration_sec);
#endif
}
// END_OF_ADD_GUILD_PRIV_TIME

void CClientManager::AddCharacterPriv(TPacketGiveCharacterPriv * p)
{
	CPrivManager::instance().AddCharPriv(p->pid, p->type, p->value);
#ifdef ENABLE_DEFAULT_PRIV_SYSTEM
	__UpdateDefaultPriv("PLAYER", p->pid, p->type, p->value, 0);
#endif
}

void CClientManager::MoneyLog(TPacketMoneyLog * p)
{
	CMoneyLog::instance().AddLog(p->type, p->vnum, p->gold);
}

CLoginData* CClientManager::GetLoginData(DWORD dwKey)
{
	TLoginDataByLoginKey::iterator it = m_map_pkLoginData.find(dwKey);

	if (it == m_map_pkLoginData.end())
		return nullptr;

	return it->second;
}

CLoginData* CClientManager::GetLoginDataByLogin(const char* c_pszLogin)
{
	char szLogin[LOGIN_MAX_LEN + 1];
	trim_and_lower(c_pszLogin, szLogin, sizeof(szLogin));

	TLoginDataByLogin::iterator it = m_map_pkLoginDataByLogin.find(szLogin);

	if (it == m_map_pkLoginDataByLogin.end())
		return nullptr;

	return it->second;
}

CLoginData* CClientManager::GetLoginDataByAID(DWORD dwAID)
{
	TLoginDataByAID::iterator it = m_map_pkLoginDataByAID.find(dwAID);

	if (it == m_map_pkLoginDataByAID.end())
		return nullptr;

	return it->second;
}

void CClientManager::InsertLoginData(CLoginData * pkLD)
{
	char szLogin[LOGIN_MAX_LEN + 1];
	trim_and_lower(pkLD->GetAccountRef().login, szLogin, sizeof(szLogin));

	m_map_pkLoginData.insert(std::make_pair(pkLD->GetKey(), pkLD));
	m_map_pkLoginDataByLogin.insert(std::make_pair(szLogin, pkLD));
	m_map_pkLoginDataByAID.insert(std::make_pair(pkLD->GetAccountRef().id, pkLD));
}

void CClientManager::DeleteLoginData(CLoginData * pkLD)
{
	m_map_pkLoginData.erase(pkLD->GetKey());
	m_map_pkLoginDataByLogin.erase(pkLD->GetAccountRef().login);
	m_map_pkLoginDataByAID.erase(pkLD->GetAccountRef().id);

	if (m_map_kLogonAccount.find(pkLD->GetAccountRef().login) == m_map_kLogonAccount.end())
		delete pkLD;
	else
		pkLD->SetDeleted(true);
}

void CClientManager::QUERY_AUTH_LOGIN(CPeer * pkPeer, DWORD dwHandle, TPacketGDAuthLogin * p)
{
	if (g_test_server)
		sys_log(0, "QUERY_AUTH_LOGIN %d %d %s", p->dwID, p->dwLoginKey, p->szLogin);
	CLoginData* pkLD = GetLoginDataByLogin(p->szLogin);

	if (pkLD)
	{
		DeleteLoginData(pkLD);
	}

	BYTE bResult;

	if (GetLoginData(p->dwLoginKey))
	{
		sys_err("LoginData already exist key %u login %s", p->dwLoginKey, p->szLogin);
		bResult = 0;

		pkPeer->EncodeHeader(HEADER_DG_AUTH_LOGIN, dwHandle, sizeof(BYTE));
		pkPeer->EncodeBYTE(bResult);
	}
	else
	{
		CLoginData* pkLD = new CLoginData;

		pkLD->SetKey(p->dwLoginKey);
		pkLD->SetClientKey(p->adwClientKey);
		pkLD->SetPremium(p->iPremiumTimes);

		TAccountTable& r = pkLD->GetAccountRef();

		r.id = p->dwID;
		trim_and_lower(p->szLogin, r.login, sizeof(r.login));
		strlcpy(r.social_id, p->szSocialID, sizeof(r.social_id));
		strlcpy(r.passwd, "TEMP", sizeof(r.passwd));

		sys_log(0, "AUTH_LOGIN id(%u) login(%s) social_id(%s) login_key(%u), client_key(%u %u %u %u)",
			p->dwID, p->szLogin, p->szSocialID, p->dwLoginKey,
			p->adwClientKey[0], p->adwClientKey[1], p->adwClientKey[2], p->adwClientKey[3]);

		bResult = 1;

		InsertLoginData(pkLD);

		pkPeer->EncodeHeader(HEADER_DG_AUTH_LOGIN, dwHandle, sizeof(BYTE));
		pkPeer->EncodeBYTE(bResult);
	}
}

void CClientManager::GuildDepositMoney(TPacketGDGuildMoney * p)
{
	CGuildManager::instance().DepositMoney(p->dwGuild, p->iGold);
}

void CClientManager::GuildWithdrawMoney(CPeer * peer, TPacketGDGuildMoney * p)
{
	CGuildManager::instance().WithdrawMoney(peer, p->dwGuild, p->iGold);
}

void CClientManager::GuildWithdrawMoneyGiveReply(TPacketGDGuildMoneyWithdrawGiveReply * p)
{
	CGuildManager::instance().WithdrawMoneyReply(p->dwGuild, p->bGiveSuccess, p->iChangeGold);
}

void CClientManager::GuildWarBet(TPacketGDGuildWarBet * p)
{
	CGuildManager::instance().Bet(p->dwWarID, p->szLogin, p->dwGold, p->dwGuild);
}

void CClientManager::CreateObject(TPacketGDCreateObject * p)
{
	using namespace building;

	char szQuery[512];

	snprintf(szQuery, sizeof(szQuery),
		"INSERT INTO object%s (land_id, vnum, map_index, x, y, x_rot, y_rot, z_rot) VALUES(%u, %u, %d, %d, %d, %f, %f, %f)",
		GetTablePostfix(), p->dwLandID, p->dwVnum, p->lMapIndex, p->x, p->y, p->xRot, p->yRot, p->zRot);

	std::unique_ptr<SQLMsg> pmsg(CDBManager::instance().DirectQuery(szQuery));

	if (pmsg->Get()->uiInsertID == 0)
	{
		sys_err("cannot insert object");
		return;
	}

	TObject* pkObj = new TObject;

	memset(pkObj, 0, sizeof(TObject));

	pkObj->dwID = pmsg->Get()->uiInsertID;
	pkObj->dwVnum = p->dwVnum;
	pkObj->dwLandID = p->dwLandID;
	pkObj->lMapIndex = p->lMapIndex;
	pkObj->x = p->x;
	pkObj->y = p->y;
	pkObj->xRot = p->xRot;
	pkObj->yRot = p->yRot;
	pkObj->zRot = p->zRot;
	pkObj->lLife = 0;

	ForwardPacket(HEADER_DG_CREATE_OBJECT, pkObj, sizeof(TObject));

	m_map_pkObjectTable.insert(std::make_pair(pkObj->dwID, pkObj));
}

void CClientManager::DeleteObject(DWORD dwID)
{
	char szQuery[128];

	snprintf(szQuery, sizeof(szQuery), "DELETE FROM object%s WHERE id=%u", GetTablePostfix(), dwID);

	std::unique_ptr<SQLMsg> pmsg(CDBManager::instance().DirectQuery(szQuery));

	if (pmsg->Get()->uiAffectedRows == 0 || pmsg->Get()->uiAffectedRows == (uint32_t)-1)
	{
		sys_err("no object by id %u", dwID);
		return;
	}

	auto it = m_map_pkObjectTable.find(dwID);

	if (it != m_map_pkObjectTable.end())
	{
		delete it->second;
		m_map_pkObjectTable.erase(it);
	}

	ForwardPacket(HEADER_DG_DELETE_OBJECT, &dwID, sizeof(DWORD));
}

void CClientManager::UpdateLand(DWORD * pdw)
{
	DWORD dwID = pdw[0];
	DWORD dwGuild = pdw[1];

	building::TLand* p = &m_vec_kLandTable[0];

	DWORD i;

	for (i = 0; i < m_vec_kLandTable.size(); ++i, ++p)
	{
		if (p->dwID == dwID)
		{
			char buf[256];
			snprintf(buf, sizeof(buf), "UPDATE land%s SET guild_id=%u WHERE id=%u", GetTablePostfix(), dwGuild, dwID);
			CDBManager::instance().AsyncQuery(buf);

			p->dwGuildID = dwGuild;
			break;
		}
	}

	if (i < m_vec_kLandTable.size())
	{
		ForwardPacket(HEADER_DG_UPDATE_LAND, p, sizeof(building::TLand));
	}
}

// BLOCK_CHAT
void CClientManager::BlockChat(TPacketBlockChat * p)
{
	char szQuery[256];

	if (g_stLocale == "sjis")
		snprintf(szQuery, sizeof(szQuery), "SELECT id FROM player%s WHERE name = '%s' collate sjis_japanese_ci", GetTablePostfix(), p->szName);
	else
		snprintf(szQuery, sizeof(szQuery), "SELECT id FROM player%s WHERE name = '%s'", GetTablePostfix(), p->szName);
	std::unique_ptr<SQLMsg> pmsg(CDBManager::instance().DirectQuery(szQuery));
	SQLResult * pRes = pmsg->Get();

	if (pRes->uiNumRows)
	{
		MYSQL_ROW row = mysql_fetch_row(pRes->pSQLResult);
		DWORD pid = strtoul(row[0], nullptr, 10);

		TPacketGDAddAffect pa;
		pa.dwPID = pid;
		pa.elem.dwType = 223;
		pa.elem.bApplyOn = 0;
		pa.elem.lApplyValue = 0;
		pa.elem.dwFlag = 0;
		pa.elem.lDuration = p->lDuration;
		pa.elem.lSPCost = 0;
		QUERY_ADD_AFFECT(nullptr, &pa);
	}
	else
	{
		// cannot find user with that name
	}
}
// END_OF_BLOCK_CHAT

void CClientManager::MarriageAdd(TPacketMarriageAdd * p)
{
	sys_log(0, "MarriageAdd %u %u %s %s", p->dwPID1, p->dwPID2, p->szName1, p->szName2);
	marriage::CManager::instance().Add(p->dwPID1, p->dwPID2, p->szName1, p->szName2);
}

void CClientManager::MarriageUpdate(TPacketMarriageUpdate * p)
{
	sys_log(0, "MarriageUpdate PID:%u %u LP:%d ST:%d", p->dwPID1, p->dwPID2, p->iLovePoint, p->byMarried);
	marriage::CManager::instance().Update(p->dwPID1, p->dwPID2, p->iLovePoint, p->byMarried);
}

void CClientManager::MarriageRemove(TPacketMarriageRemove * p)
{
	sys_log(0, "MarriageRemove %u %u", p->dwPID1, p->dwPID2);
	marriage::CManager::instance().Remove(p->dwPID1, p->dwPID2);
}

void CClientManager::WeddingRequest(TPacketWeddingRequest * p)
{
	sys_log(0, "WeddingRequest %u %u", p->dwPID1, p->dwPID2);
	ForwardPacket(HEADER_DG_WEDDING_REQUEST, p, sizeof(TPacketWeddingRequest));
	//marriage::CManager::instance().RegisterWedding(p->dwPID1, p->szName1, p->dwPID2, p->szName2);
}

void CClientManager::WeddingReady(TPacketWeddingReady * p)
{
	sys_log(0, "WeddingReady %u %u", p->dwPID1, p->dwPID2);
	ForwardPacket(HEADER_DG_WEDDING_READY, p, sizeof(TPacketWeddingReady));
	marriage::CManager::instance().ReadyWedding(p->dwMapIndex, p->dwPID1, p->dwPID2);
}

void CClientManager::WeddingEnd(TPacketWeddingEnd * p)
{
	sys_log(0, "WeddingEnd %u %u", p->dwPID1, p->dwPID2);
	marriage::CManager::instance().EndWedding(p->dwPID1, p->dwPID2);
}

//
// 캐시에 가격정보가 있으면 캐시를 업데이트 하고 캐시에 가격정보가 없다면
// 우선 기존의 데이터를 로드한 뒤에 기존의 정보로 캐시를 만들고 새로 받은 가격정보를 업데이트 한다.
//
void CClientManager::MyshopPricelistUpdate(const TItemPriceListTable * pPacket) // @fixme403 (TPacketMyshopPricelistHeader to TItemPriceListTable)
{
	if (pPacket->byCount > SHOP_PRICELIST_MAX_NUM)
	{
		sys_err("count overflow!");
		return;
	}

	CItemPriceListTableCache* pCache = GetItemPriceListCache(pPacket->dwOwnerID);

	if (pCache)
	{
		TItemPriceListTable table;

		table.dwOwnerID = pPacket->dwOwnerID;
		table.byCount = pPacket->byCount;

		thecore_memcpy(table.aPriceInfo, pPacket->aPriceInfo, sizeof(TItemPriceInfo) * pPacket->byCount);

		pCache->UpdateList(&table);
	}
	else
	{
		TItemPriceListTable* pUpdateTable = new TItemPriceListTable;

		pUpdateTable->dwOwnerID = pPacket->dwOwnerID;
		pUpdateTable->byCount = pPacket->byCount;

		thecore_memcpy(pUpdateTable->aPriceInfo, pPacket->aPriceInfo, sizeof(TItemPriceInfo) * pPacket->byCount);

		char szQuery[QUERY_MAX_LEN];
#ifdef ENABLE_CHEQUE_SYSTEM
		snprintf(szQuery, sizeof(szQuery), "SELECT item_vnum, price, cheque FROM myshop_pricelist%s WHERE owner_id=%u", GetTablePostfix(), pPacket->dwOwnerID);
#else
		snprintf(szQuery, sizeof(szQuery), "SELECT item_vnum, price FROM myshop_pricelist%s WHERE owner_id=%u", GetTablePostfix(), pPacket->dwOwnerID);
#endif
		CDBManager::instance().ReturnQuery(szQuery, QID_ITEMPRICE_LOAD_FOR_UPDATE, 0, pUpdateTable);
	}
}

// MYSHOP_PRICE_LIST
// 캐시된 가격정보가 있으면 캐시를 읽어 바로 전송하고 캐시에 정보가 없으면 DB 에 쿼리를 한다.
//
void CClientManager::MyshopPricelistRequest(CPeer * peer, DWORD dwHandle, DWORD dwPlayerID)
{
	if (CItemPriceListTableCache * pCache = GetItemPriceListCache(dwPlayerID))
	{
		sys_log(0, "Cache MyShopPricelist handle[%d] pid[%d]", dwHandle, dwPlayerID);

		TItemPriceListTable* pTable = pCache->Get(false);

		TPacketMyshopPricelistHeader header =
		{
			pTable->dwOwnerID,
			pTable->byCount
		};

		size_t sizePriceListSize = sizeof(TItemPriceInfo) * pTable->byCount;

		peer->EncodeHeader(HEADER_DG_MYSHOP_PRICELIST_RES, dwHandle, sizeof(header) + sizePriceListSize);
		peer->Encode(&header, sizeof(header));
		peer->Encode(pTable->aPriceInfo, sizePriceListSize);
	}
	else
	{
		sys_log(0, "Query MyShopPricelist handle[%d] pid[%d]", dwHandle, dwPlayerID);

		char szQuery[QUERY_MAX_LEN];
#ifdef ENABLE_CHEQUE_SYSTEM
		snprintf(szQuery, sizeof(szQuery), "SELECT item_vnum, price, cheque FROM myshop_pricelist%s WHERE owner_id=%u", GetTablePostfix(), dwPlayerID);
#else
		snprintf(szQuery, sizeof(szQuery), "SELECT item_vnum, price FROM myshop_pricelist%s WHERE owner_id=%u", GetTablePostfix(), dwPlayerID);
#endif
		CDBManager::instance().ReturnQuery(szQuery, QID_ITEMPRICE_LOAD, peer->GetHandle(), new TItemPricelistReqInfo(dwHandle, dwPlayerID));
	}
}
// END_OF_MYSHOP_PRICE_LIST

void CPacketInfo::Add(int header)
{
	auto it = m_map_info.find(header);

	if (it == m_map_info.end())
		m_map_info.insert(std::map<int, int>::value_type(header, 1));
	else
		++it->second;
}

void CPacketInfo::Reset()
{
	m_map_info.clear();
}

void CClientManager::ProcessPackets(CPeer * peer)
{
	BYTE		header;
	DWORD		dwHandle;
	DWORD		dwLength;
	const char* data = nullptr;
	int			i = 0;
	int			iCount = 0;

	while (peer->PeekPacket(i, header, dwHandle, dwLength, &data))
	{
		// DISABLE_DB_HEADER_LOG
		// sys_log(0, "header %d %p size %d", header, this, dwLength);
		// END_OF_DISABLE_DB_HEADER_LOG
		m_bLastHeader = header;
		++iCount;

#ifdef _TEST
		if (header != 10)
			sys_log(0, " ProcessPacket Header [%d] Handle[%d] Length[%d] iCount[%d]", header, dwHandle, dwLength, iCount);
#endif
		if (g_test_server)
		{
			if (header != 10)
				sys_log(0, " ProcessPacket Header [%d] Handle[%d] Length[%d] iCount[%d]", header, dwHandle, dwLength, iCount);
		}

		switch (header)
		{
		case HEADER_GD_BOOT:
			QUERY_BOOT(peer, (TPacketGDBoot*)data);
			break;

		case HEADER_GD_LOGIN_BY_KEY:
			QUERY_LOGIN_BY_KEY(peer, dwHandle, (TPacketGDLoginByKey*)data);
			break;

		case HEADER_GD_LOGOUT:
			//sys_log(0, "HEADER_GD_LOGOUT (handle: %d length: %d)", dwHandle, dwLength);
			QUERY_LOGOUT(peer, dwHandle, data);
			break;

		case HEADER_GD_PLAYER_LOAD:
			sys_log(1, "HEADER_GD_PLAYER_LOAD (handle: %d length: %d)", dwHandle, dwLength);
			QUERY_PLAYER_LOAD(peer, dwHandle, (TPlayerLoadPacket*)data);
			break;

		case HEADER_GD_PLAYER_SAVE:
			sys_log(1, "HEADER_GD_PLAYER_SAVE (handle: %d length: %d)", dwHandle, dwLength);
			QUERY_PLAYER_SAVE(peer, dwHandle, (TPlayerTable*)data);
			break;

		case HEADER_GD_PLAYER_CREATE:
			sys_log(0, "HEADER_GD_PLAYER_CREATE (handle: %d length: %d)", dwHandle, dwLength);
			__QUERY_PLAYER_CREATE(peer, dwHandle, (TPlayerCreatePacket*)data);
			sys_log(0, "END");
			break;

		case HEADER_GD_PLAYER_DELETE:
			sys_log(1, "HEADER_GD_PLAYER_DELETE (handle: %d length: %d)", dwHandle, dwLength);
			__QUERY_PLAYER_DELETE(peer, dwHandle, (TPlayerDeletePacket*)data);
			break;

		case HEADER_GD_PLAYER_COUNT:
			QUERY_PLAYER_COUNT(peer, (TPlayerCountPacket*)data);
			break;

		case HEADER_GD_QUEST_SAVE:
			sys_log(1, "HEADER_GD_QUEST_SAVE (handle: %d length: %d)", dwHandle, dwLength);
			QUERY_QUEST_SAVE(peer, (TQuestTable*)data, dwLength);
			break;

		case HEADER_GD_SAFEBOX_LOAD:
			QUERY_SAFEBOX_LOAD(peer, dwHandle, (TSafeboxLoadPacket*)data, 0);
			break;

		case HEADER_GD_SAFEBOX_SAVE:
			sys_log(1, "HEADER_GD_SAFEBOX_SAVE (handle: %d length: %d)", dwHandle, dwLength);
			QUERY_SAFEBOX_SAVE(peer, (TSafeboxTable*)data);
			break;

		case HEADER_GD_SAFEBOX_CHANGE_SIZE:
			QUERY_SAFEBOX_CHANGE_SIZE(peer, dwHandle, (TSafeboxChangeSizePacket*)data);
			break;

		case HEADER_GD_SAFEBOX_CHANGE_PASSWORD:
			QUERY_SAFEBOX_CHANGE_PASSWORD(peer, dwHandle, (TSafeboxChangePasswordPacket*)data);
			break;

		case HEADER_GD_MALL_LOAD:
			QUERY_SAFEBOX_LOAD(peer, dwHandle, (TSafeboxLoadPacket*)data, 1);
			break;

		case HEADER_GD_EMPIRE_SELECT:
			QUERY_EMPIRE_SELECT(peer, dwHandle, (TEmpireSelectPacket*)data);
			break;

		case HEADER_GD_SETUP:
			QUERY_SETUP(peer, dwHandle, data);
			break;

		case HEADER_GD_GUILD_CREATE:
			GuildCreate(peer, *(DWORD*)data);
			break;

		case HEADER_GD_GUILD_SKILL_UPDATE:
			GuildSkillUpdate(peer, (TPacketGuildSkillUpdate*)data);
			break;

		case HEADER_GD_GUILD_EXP_UPDATE:
			GuildExpUpdate(peer, (TPacketGuildExpUpdate*)data);
			break;

		case HEADER_GD_GUILD_ADD_MEMBER:
			GuildAddMember(peer, (TPacketGDGuildAddMember*)data);
			break;

		case HEADER_GD_GUILD_REMOVE_MEMBER:
			GuildRemoveMember(peer, (TPacketGuild*)data);
			break;

		case HEADER_GD_GUILD_CHANGE_GRADE:
			GuildChangeGrade(peer, (TPacketGuild*)data);
			break;

		case HEADER_GD_GUILD_CHANGE_MEMBER_DATA:
			GuildChangeMemberData(peer, (TPacketGuildChangeMemberData*)data);
			break;

		case HEADER_GD_GUILD_DISBAND:
			GuildDisband(peer, (TPacketGuild*)data);
			break;

		case HEADER_GD_GUILD_WAR:
			GuildWar(peer, (TPacketGuildWar*)data);
			break;

		case HEADER_GD_GUILD_WAR_SCORE:
			GuildWarScore(peer, (TPacketGuildWarScore*)data);
			break;

		case HEADER_GD_GUILD_CHANGE_LADDER_POINT:
			GuildChangeLadderPoint((TPacketGuildLadderPoint*)data);
			break;

		case HEADER_GD_GUILD_USE_SKILL:
			GuildUseSkill((TPacketGuildUseSkill*)data);
			break;

		case HEADER_GD_FLUSH_CACHE:
			QUERY_FLUSH_CACHE(peer, data);
			break;

		case HEADER_GD_ITEM_SAVE:
			QUERY_ITEM_SAVE(peer, data);
			break;

		case HEADER_GD_ITEM_DESTROY:
			QUERY_ITEM_DESTROY(peer, data);
			break;

		case HEADER_GD_ITEM_FLUSH:
			QUERY_ITEM_FLUSH(peer, data);
			break;

		case HEADER_GD_ADD_AFFECT:
			sys_log(1, "HEADER_GD_ADD_AFFECT");
			QUERY_ADD_AFFECT(peer, (TPacketGDAddAffect*)data);
			break;

		case HEADER_GD_REMOVE_AFFECT:
			sys_log(1, "HEADER_GD_REMOVE_AFFECT");
			QUERY_REMOVE_AFFECT(peer, (TPacketGDRemoveAffect*)data);
			break;

		case HEADER_GD_HIGHSCORE_REGISTER:
			QUERY_HIGHSCORE_REGISTER(peer, (TPacketGDHighscore*)data);
			break;

		case HEADER_GD_PARTY_CREATE:
			QUERY_PARTY_CREATE(peer, (TPacketPartyCreate*)data);
			break;

		case HEADER_GD_PARTY_DELETE:
			QUERY_PARTY_DELETE(peer, (TPacketPartyDelete*)data);
			break;

		case HEADER_GD_PARTY_ADD:
			QUERY_PARTY_ADD(peer, (TPacketPartyAdd*)data);
			break;

		case HEADER_GD_PARTY_REMOVE:
			QUERY_PARTY_REMOVE(peer, (TPacketPartyRemove*)data);
			break;

		case HEADER_GD_PARTY_STATE_CHANGE:
			QUERY_PARTY_STATE_CHANGE(peer, (TPacketPartyStateChange*)data);
			break;

		case HEADER_GD_PARTY_SET_MEMBER_LEVEL:
			QUERY_PARTY_SET_MEMBER_LEVEL(peer, (TPacketPartySetMemberLevel*)data);
			break;

		case HEADER_GD_RELOAD_PROTO:
			QUERY_RELOAD_PROTO();
			break;

		case HEADER_GD_CHANGE_NAME:
			QUERY_CHANGE_NAME(peer, dwHandle, (TPacketGDChangeName*)data);
			break;

		case HEADER_GD_AUTH_LOGIN:
			QUERY_AUTH_LOGIN(peer, dwHandle, (TPacketGDAuthLogin*)data);
			break;

		case HEADER_GD_REQUEST_GUILD_PRIV:
			AddGuildPriv((TPacketGiveGuildPriv*)data);
			break;

		case HEADER_GD_REQUEST_EMPIRE_PRIV:
			AddEmpirePriv((TPacketGiveEmpirePriv*)data);
			break;

		case HEADER_GD_REQUEST_CHARACTER_PRIV:
			AddCharacterPriv((TPacketGiveCharacterPriv*)data);
			break;

		case HEADER_GD_MONEY_LOG:
			MoneyLog((TPacketMoneyLog*)data);
			break;

		case HEADER_GD_GUILD_DEPOSIT_MONEY:
			GuildDepositMoney((TPacketGDGuildMoney*)data);
			break;

		case HEADER_GD_GUILD_WITHDRAW_MONEY:
			GuildWithdrawMoney(peer, (TPacketGDGuildMoney*)data);
			break;

		case HEADER_GD_GUILD_WITHDRAW_MONEY_GIVE_REPLY:
			GuildWithdrawMoneyGiveReply((TPacketGDGuildMoneyWithdrawGiveReply*)data);
			break;

		case HEADER_GD_GUILD_WAR_BET:
			GuildWarBet((TPacketGDGuildWarBet*)data);
			break;

		case HEADER_GD_SET_EVENT_FLAG:
			SetEventFlag((TPacketSetEventFlag*)data);
			break;

		case HEADER_GD_CREATE_OBJECT:
			CreateObject((TPacketGDCreateObject*)data);
			break;

		case HEADER_GD_DELETE_OBJECT:
			DeleteObject(*(DWORD*)data);
			break;

		case HEADER_GD_UPDATE_LAND:
			UpdateLand((DWORD*)data);
			break;

		case HEADER_GD_MARRIAGE_ADD:
			MarriageAdd((TPacketMarriageAdd*)data);
			break;

		case HEADER_GD_MARRIAGE_UPDATE:
			MarriageUpdate((TPacketMarriageUpdate*)data);
			break;

		case HEADER_GD_MARRIAGE_REMOVE:
			MarriageRemove((TPacketMarriageRemove*)data);
			break;

		case HEADER_GD_WEDDING_REQUEST:
			WeddingRequest((TPacketWeddingRequest*)data);
			break;

		case HEADER_GD_WEDDING_READY:
			WeddingReady((TPacketWeddingReady*)data);
			break;

		case HEADER_GD_WEDDING_END:
			WeddingEnd((TPacketWeddingEnd*)data);
			break;

			// BLOCK_CHAT
		case HEADER_GD_BLOCK_CHAT:
			BlockChat((TPacketBlockChat*)data);
			break;
			// END_OF_BLOCK_CHAT

			// MYSHOP_PRICE_LIST
		case HEADER_GD_MYSHOP_PRICELIST_UPDATE:
			MyshopPricelistUpdate((TItemPriceListTable*)data); // @fixme403 (TPacketMyshopPricelistHeader to TItemPriceListTable)
			break;

		case HEADER_GD_MYSHOP_PRICELIST_REQ:
			MyshopPricelistRequest(peer, dwHandle, *(DWORD*)data);
			break;
			// END_OF_MYSHOP_PRICE_LIST

			//RELOAD_ADMIN
		case HEADER_GD_RELOAD_ADMIN:
			ReloadAdmin(peer, (TPacketReloadAdmin*)data);
			break;
			//END_RELOAD_ADMIN

		case HEADER_GD_BREAK_MARRIAGE:
			BreakMarriage(peer, data);
			break;

		case HEADER_GD_REQ_SPARE_ITEM_ID_RANGE:
			SendSpareItemIDRange(peer);
			break;

		case HEADER_GD_REQ_CHANGE_GUILD_MASTER:
			GuildChangeMaster((TPacketChangeGuildMaster*)data);
			break;

		case HEADER_GD_UPDATE_HORSE_NAME:
			UpdateHorseName((TPacketUpdateHorseName*)data, peer);
			break;

		case HEADER_GD_REQ_HORSE_NAME:
			AckHorseName(*(DWORD*)data, peer);
			break;

		case HEADER_GD_DC:
			DeleteLoginKey((TPacketDC*)data);
			break;

		case HEADER_GD_VALID_LOGOUT:
			ResetLastPlayerID((TPacketNeedLoginLogInfo*)data);
			break;

		case HEADER_GD_REQUEST_CHARGE_CASH:
			ChargeCash((TRequestChargeCash*)data);
			break;

			//delete gift notify icon

		case HEADER_GD_DELETE_AWARDID:
			DeleteAwardId((TPacketDeleteAwardID*)data);
			break;

		case HEADER_GD_UPDATE_CHANNELSTATUS:
			UpdateChannelStatus((SChannelStatus*)data);
			break;
		case HEADER_GD_REQUEST_CHANNELSTATUS:
			RequestChannelStatus(peer, dwHandle);
			break;

#ifdef ENABLE_OFFLINE_SHOP_SYSTEM
		case HEADER_GD_SHOP_NAME:
			ShopName(peer, (TPacketShopName*)data);
			break;
		case HEADER_GD_SHOP_CLOSE:
			ShopClose(peer, (TPacketShopClose*)data);
			break;
		case HEADER_GD_SHOP_UPDATE_ITEM:
			ShopUpdateItem(peer, (TPacketShopUpdateItem*)data);
			break;
#endif

#ifdef ENABLE_DUNGEON_FOR_GUILD
		case HEADER_GD_GUILD_DUNGEON:
			GuildDungeon((TPacketGDGuildDungeon*)data);
			break;
		case HEADER_GD_GUILD_DUNGEON_CD:
			GuildDungeonGD((TPacketGDGuildDungeonCD*)data);
			break;
#endif

#if defined(ENABLE_BATTLE_ZONE_SYSTEM)
		case HEADER_GD_COMBAT_ZONE_RESET_RANKING:
			CombatZoneResetRanking((TPacketGDCombatZoneResetRanking*)data);
			break;

		case HEADER_GD_COMBAT_ZONE_SKILLS_CACHE:
			UpdateSkillsCache((TPacketGDCombatZoneSkillsCache*)data);
			break;
#endif

#ifdef ENABLE_CHANNEL_SWITCH_SYSTEM
		case HEADER_GD_FIND_CHANNEL:
			FindChannel(peer, dwHandle, (TPacketChangeChannel*)data);
			break;
#endif

#ifdef ENABLE_PARTY_UPDATE
		case HEADER_GD_PARTY_SET_MEMBER_MAP_INDEX:
			QUERY_PARTY_SET_MEMBER_MAP_INDEX(peer, (TPacketPartySetMemberMapIndex*)data);
			break;

		case HEADER_GD_PARTY_SET_MEMBER_CHANNEL:
			QUERY_PARTY_SET_MEMBER_CHANNEL(peer, (TPacketPartySetMemberChannel*)data);
			break;
#endif

#ifdef ENABLE_SAFEBOX_MONEY_SYSTEM
		case HEADER_GD_SAFEBOX_MONEY:
			SafeboxMoney((TSafeboxMoney*)data);
			break;
			
		case HEADER_GD_SAFEBOX_CHEQUE:
			SafeboxCheque((TSafeboxCheque*)data);
			break;
#endif

		default:
			sys_err("Unknown header (header: %d handle: %d length: %d)", header, dwHandle, dwLength);
			break;
		}
	}

	peer->RecvEnd(i);
}

void CClientManager::AddPeer(socket_t fd)
{
	CPeer* pPeer = new CPeer;

	if (pPeer->Accept(fd))
		m_peerList.push_front(pPeer);
	else
		delete pPeer;
}

void CClientManager::RemovePeer(CPeer * pPeer)
{
	if (m_pkAuthPeer == pPeer)
	{
		m_pkAuthPeer = nullptr;
	}
	else
	{
		TLogonAccountMap::iterator it = m_map_kLogonAccount.begin();

		while (it != m_map_kLogonAccount.end())
		{
			CLoginData* pkLD = it->second;

			if (pkLD->GetConnectedPeerHandle() == pPeer->GetHandle())
			{
				if (pkLD->IsPlay())
				{
					pkLD->SetPlay(false);
				}

				if (pkLD->IsDeleted())
				{
					sys_log(0, "DELETING LoginData");
					delete pkLD;
				}

				m_map_kLogonAccount.erase(it++);
			}
			else
				++it;
		}
	}

	m_peerList.remove(pPeer);
	delete pPeer;
}

CPeer* CClientManager::GetPeer(IDENT ident)
{
	for (auto i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (tmp->GetHandle() == ident)
			return tmp;
	}

	return nullptr;
}

CPeer* CClientManager::GetAnyPeer()
{
	if (m_peerList.empty())
		return nullptr;

	return m_peerList.front();
}

// DB 매니저로 부터 받은 결과를 처리한다.
//
// @version	05/06/10 Bang2ni - 가격정보 관련 쿼리(QID_ITEMPRICE_XXX) 추가
int CClientManager::AnalyzeQueryResult(SQLMsg * msg)
{
	CQueryInfo* qi = (CQueryInfo*)msg->pvUserData;
	CPeer* peer = GetPeer(qi->dwIdent);

#ifdef _TEST
	if (qi->iType != QID_ITEM_AWARD_LOAD)
		sys_log(0, "AnalyzeQueryResult %d", qi->iType);
#endif
	switch (qi->iType)
	{
	case QID_ITEM_AWARD_LOAD:
		ItemAwardManager::instance().Load(msg);
		delete qi;
		return true;

	case QID_GUILD_RANKING:
		CGuildManager::instance().ResultRanking(msg->Get()->pSQLResult);
		break;

		// MYSHOP_PRICE_LIST
	case QID_ITEMPRICE_LOAD_FOR_UPDATE:
		RESULT_PRICELIST_LOAD_FOR_UPDATE(msg);
		break;
		// END_OF_MYSHOP_PRICE_LIST
	}

	if (!peer)
	{
		//sys_err("CClientManager::AnalyzeQueryResult: peer not exist anymore. (ident: %d)", qi->dwIdent);
		delete qi;
		return true;
	}

	switch (qi->iType)
	{
	case QID_PLAYER:
	case QID_ITEM:
	case QID_QUEST:
	case QID_AFFECT:
		RESULT_COMPOSITE_PLAYER(peer, msg, qi->iType);
		break;

	case QID_LOGIN:
		RESULT_LOGIN(peer, msg);
		break;

	case QID_SAFEBOX_LOAD:
		sys_log(0, "QUERY_RESULT: HEADER_GD_SAFEBOX_LOAD");
		RESULT_SAFEBOX_LOAD(peer, msg);
		break;

	case QID_SAFEBOX_CHANGE_SIZE:
		sys_log(0, "QUERY_RESULT: HEADER_GD_SAFEBOX_CHANGE_SIZE");
		RESULT_SAFEBOX_CHANGE_SIZE(peer, msg);
		break;

	case QID_SAFEBOX_CHANGE_PASSWORD:
		sys_log(0, "QUERY_RESULT: HEADER_GD_SAFEBOX_CHANGE_PASSWORD %p", msg);
		RESULT_SAFEBOX_CHANGE_PASSWORD(peer, msg);
		break;

	case QID_SAFEBOX_CHANGE_PASSWORD_SECOND:
		sys_log(0, "QUERY_RESULT: HEADER_GD_SAFEBOX_CHANGE_PASSWORD %p", msg);
		RESULT_SAFEBOX_CHANGE_PASSWORD_SECOND(peer, msg);
		break;

	case QID_HIGHSCORE_REGISTER:
		sys_log(0, "QUERY_RESULT: HEADER_GD_HIGHSCORE_REGISTER %p", msg);
		RESULT_HIGHSCORE_REGISTER(peer, msg);
		break;

	case QID_SAFEBOX_SAVE:
	case QID_ITEM_SAVE:
	case QID_ITEM_DESTROY:
	case QID_QUEST_SAVE:
	case QID_PLAYER_SAVE:
	case QID_ITEM_AWARD_TAKEN:
		break;

		// PLAYER_INDEX_CREATE_BUG_FIX
	case QID_PLAYER_INDEX_CREATE:
		RESULT_PLAYER_INDEX_CREATE(peer, msg);
		break;
		// END_PLAYER_INDEX_CREATE_BUG_FIX

	case QID_PLAYER_DELETE:
		__RESULT_PLAYER_DELETE(peer, msg);
		break;

	case QID_LOGIN_BY_KEY:
		RESULT_LOGIN_BY_KEY(peer, msg);
		break;

		// MYSHOP_PRICE_LIST
	case QID_ITEMPRICE_LOAD:
		RESULT_PRICELIST_LOAD(peer, msg);
		break;
		// END_OF_MYSHOP_PRICE_LIST

	default:
		sys_log(0, "CClientManager::AnalyzeQueryResult unknown query result type: %d, str: %s", qi->iType, msg->stQuery.c_str());
		break;
	}

	delete qi;
	return true;
}

void UsageLog()
{
	FILE* fp = nullptr;

	time_t      ct;
	char* time_s;
	struct tm   lt;

	int         avg = g_dwUsageAvg / 3600; // 60 초 * 60 분

	fp = fopen("usage.txt", "a+");

	if (!fp)
		return;

	ct = time(0);
	lt = *localtime(&ct);
	time_s = asctime(&lt);

	time_s[strlen(time_s) - 1] = '\0';

	fprintf(fp, "| %4d %-15.15s | %5d | %5u |", lt.tm_year + 1900, time_s + 4, avg, g_dwUsageMax);

	fprintf(fp, "\n");
	fclose(fp);

	g_dwUsageMax = g_dwUsageAvg = 0;
}

int CClientManager::Process()
{
	int pulses;

	if (!(pulses = thecore_idle()))
		return 0;

	while (pulses--)
	{
		++thecore_heart->pulse;

		/*
		//30분마다 변경
		if (((thecore_pulse() % (60 * 30 * 10)) == 0))
		{
			g_iPlayerCacheFlushSeconds = MAX(60, rand() % 180);
			g_iItemCacheFlushSeconds = MAX(60, rand() % 180);
			sys_log(0, "[SAVE_TIME]Change saving time item %d player %d", g_iPlayerCacheFlushSeconds, g_iItemCacheFlushSeconds);
		}
		*/

		if (!(thecore_heart->pulse % thecore_heart->passes_per_sec))
		{
			if (g_test_server)
			{
				if (!(thecore_heart->pulse % thecore_heart->passes_per_sec * 10))

				{
					/*pt_log("[%9d] return %d/%d/%d/%d async %d/%d/%d/%d",
							thecore_heart->pulse,
							CDBManager::instance().CountReturnQuery(SQL_PLAYER),
							CDBManager::instance().CountReturnResult(SQL_PLAYER),
							CDBManager::instance().CountReturnQueryFinished(SQL_PLAYER),
							CDBManager::instance().CountReturnCopiedQuery(SQL_PLAYER),
							CDBManager::instance().CountAsyncQuery(SQL_PLAYER),
							CDBManager::instance().CountAsyncResult(SQL_PLAYER),
							CDBManager::instance().CountAsyncQueryFinished(SQL_PLAYER),
							CDBManager::instance().CountAsyncCopiedQuery(SQL_PLAYER));

					if ((thecore_heart->pulse % 50) == 0)
						sys_log(0, "[%9d] return %d/%d/%d async %d/%d/%d",
								thecore_heart->pulse,
								CDBManager::instance().CountReturnQuery(SQL_PLAYER),
								CDBManager::instance().CountReturnResult(SQL_PLAYER),
								CDBManager::instance().CountReturnQueryFinished(SQL_PLAYER),
								CDBManager::instance().CountAsyncQuery(SQL_PLAYER),
								CDBManager::instance().CountAsyncResult(SQL_PLAYER),
								CDBManager::instance().CountAsyncQueryFinished(SQL_PLAYER));*/
				}
			}
			else
			{
				/*pt_log("[%9d] return %d/%d/%d/%d async %d/%d/%d%/%d",
						thecore_heart->pulse,
						CDBManager::instance().CountReturnQuery(SQL_PLAYER),
						CDBManager::instance().CountReturnResult(SQL_PLAYER),
						CDBManager::instance().CountReturnQueryFinished(SQL_PLAYER),
						CDBManager::instance().CountReturnCopiedQuery(SQL_PLAYER),
						CDBManager::instance().CountAsyncQuery(SQL_PLAYER),
						CDBManager::instance().CountAsyncResult(SQL_PLAYER),
						CDBManager::instance().CountAsyncQueryFinished(SQL_PLAYER),
						CDBManager::instance().CountAsyncCopiedQuery(SQL_PLAYER));

						if ((thecore_heart->pulse % 50) == 0)
						sys_log(0, "[%9d] return %d/%d/%d async %d/%d/%d",
							thecore_heart->pulse,
							CDBManager::instance().CountReturnQuery(SQL_PLAYER),
							CDBManager::instance().CountReturnResult(SQL_PLAYER),
							CDBManager::instance().CountReturnQueryFinished(SQL_PLAYER),
							CDBManager::instance().CountAsyncQuery(SQL_PLAYER),
							CDBManager::instance().CountAsyncResult(SQL_PLAYER),
							CDBManager::instance().CountAsyncQueryFinished(SQL_PLAYER));*/
			}

			CDBManager::instance().ResetCounter();

			DWORD dwCount = CClientManager::instance().GetUserCount();

			g_dwUsageAvg += dwCount;
			g_dwUsageMax = MAX(g_dwUsageMax, dwCount);

			memset(&thecore_profiler[0], 0, sizeof(thecore_profiler));

			if (!(thecore_heart->pulse % (thecore_heart->passes_per_sec * 3600)))
				UsageLog();

			m_iCacheFlushCount = 0;

			//플레이어 플러쉬
			UpdatePlayerCache();
			//아이템 플러쉬
			UpdateItemCache();
			//로그아웃시 처리- 캐쉬셋 플러쉬
			UpdateLogoutPlayer();

			// MYSHOP_PRICE_LIST
			UpdateItemPriceListCache();
			// END_OF_MYSHOP_PRICE_LIST

			CGuildManager::instance().Update();
			CPrivManager::instance().Update();
			marriage::CManager::instance().Update();
		}
#ifdef ENABLE_ITEMAWARD_REFRESH
		if (!(thecore_heart->pulse % (thecore_heart->passes_per_sec * 5)))
		{
			ItemAwardManager::instance().RequestLoad();
		}
#endif
		if (!(thecore_heart->pulse % (thecore_heart->passes_per_sec * 10)))
		{
			/*
			char buf[4096 + 1];
			int len
			itertype(g_query_info.m_map_info) it;

			/////////////////////////////////////////////////////////////////
			buf[0] = '\0';
			len = 0;

			it = g_query_info.m_map_info.begin();

			int count = 0;

			while (it != g_query_info.m_map_info.end())
			{
				len += snprintf(buf + len, sizeof(buf) - len, "%2d %3d\n", it->first, it->second);
				count += it->second;
				it++;
			}

			pt_log("QUERY:\n%s-------------------- MAX : %d\n", buf, count);
			g_query_info.Reset();
			*/
			pt_log("QUERY: MAIN[%d] ASYNC[%d]", g_query_count[0], g_query_count[1]);
			g_query_count[0] = 0;
			g_query_count[1] = 0;
			/////////////////////////////////////////////////////////////////

			/////////////////////////////////////////////////////////////////
			/*
			buf[0] = '\0';
			len = 0;

			it = g_item_info.m_map_info.begin();

			count = 0;
			while (it != g_item_info.m_map_info.end())
			{
				len += snprintf(buf + len, sizeof(buf) - len, "%5d %3d\n", it->first, it->second);
				count += it->second;
				it++;
			}

			pt_log("ITEM:\n%s-------------------- MAX : %d\n", buf, count);
			g_item_info.Reset();
			*/
			pt_log("ITEM:%d\n", g_item_count);
			g_item_count = 0;
			/////////////////////////////////////////////////////////////////
		}

		if (!(thecore_heart->pulse % (thecore_heart->passes_per_sec * 60)))	// 60초에 한번
		{
			// 유니크 아이템을 위한 시간을 보낸다.
			CClientManager::instance().SendTime();
		}

		if (!(thecore_heart->pulse % (thecore_heart->passes_per_sec * 3600)))	// 한시간에 한번
		{
			CMoneyLog::instance().Save();
		}
	}

	int num_events = fdwatch(m_fdWatcher, 0);
	int idx;
	CPeer* peer;

	for (idx = 0; idx < num_events; ++idx) // 인풋
	{
		peer = (CPeer*)fdwatch_get_client_data(m_fdWatcher, idx);

		if (!peer)
		{
			if (fdwatch_check_event(m_fdWatcher, m_fdAccept, idx) == FDW_READ)
			{
				AddPeer(m_fdAccept);
				fdwatch_clear_event(m_fdWatcher, m_fdAccept, idx);
			}
			else
			{
				sys_log(0, "FDWATCH: peer nullptr in event: ident %d", fdwatch_get_ident(m_fdWatcher, idx)); // @warme012
			}

			continue;
		}

		switch (fdwatch_check_event(m_fdWatcher, peer->GetFd(), idx))
		{
		case FDW_READ:
			if (peer->Recv() < 0)
			{
				sys_err("Recv failed");
				RemovePeer(peer);
			}
			else
			{
				if (peer == m_pkAuthPeer)
					if (g_log)
						sys_log(0, "AUTH_PEER_READ: size %d", peer->GetRecvLength());

				ProcessPackets(peer);
			}
			break;

		case FDW_WRITE:
			if (peer == m_pkAuthPeer)
				if (g_log)
					sys_log(0, "AUTH_PEER_WRITE: size %d", peer->GetSendLength());

			if (peer->Send() < 0)
			{
				sys_err("Send failed");
				RemovePeer(peer);
			}

			break;

		case FDW_EOF:
			RemovePeer(peer);
			break;

		default:
			sys_err("fdwatch_check_fd returned unknown result");
			RemovePeer(peer);
			break;
		}
	}
	return 1;
}

DWORD CClientManager::GetUserCount()
{
	// 단순히 로그인 카운트를 센다.. --;
	return m_map_kLogonAccount.size();
}

void CClientManager::SendAllGuildSkillRechargePacket()
{
	ForwardPacket(HEADER_DG_GUILD_SKILL_RECHARGE, nullptr, 0);
}

void CClientManager::SendTime()
{
	time_t now = GetCurrentTime();
	ForwardPacket(HEADER_DG_TIME, &now, sizeof(time_t));
}

void CClientManager::ForwardPacket(BYTE header, const void* data, int size, BYTE bChannel, CPeer * except)
{
	for (auto it = m_peerList.begin(); it != m_peerList.end(); ++it)
	{
		CPeer* peer = *it;

		if (peer == except)
			continue;

		if (!peer->GetChannel())
			continue;

		if (bChannel && peer->GetChannel() != bChannel)
			continue;

		peer->EncodeHeader(header, 0, size);

		if (size > 0 && data)
			peer->Encode(data, size);
	}
}

void CClientManager::SendNotice(const char* c_pszFormat, ...)
{
	char szBuf[255 + 1];
	va_list args;

	va_start(args, c_pszFormat);
	int len = vsnprintf(szBuf, sizeof(szBuf), c_pszFormat, args);
	va_end(args);
	szBuf[len] = '\0';

	ForwardPacket(HEADER_DG_NOTICE, szBuf, len + 1);
}

time_t CClientManager::GetCurrentTime()
{
	return time(0);
}

// ITEM_UNIQUE_ID
bool CClientManager::InitializeNowItemID()
{
	DWORD dwMin, dwMax;

	//아이템 ID를 초기화 한다.
	if (!CConfig::instance().GetTwoValue("ITEM_ID_RANGE", &dwMin, &dwMax))
	{
		sys_err("conf.txt: Cannot find ITEM_ID_RANGE [start_item_id] [end_item_id]");
		return false;
	}

	sys_log(0, "ItemRange From File %u ~ %u ", dwMin, dwMax);

	if (CItemIDRangeManager::instance().BuildRange(dwMin, dwMax, m_itemRange) == false)
	{
		sys_err("Can not build ITEM_ID_RANGE");
		return false;
	}

	sys_log(0, " Init Success Start %u End %u Now %u\n", m_itemRange.dwMin, m_itemRange.dwMax, m_itemRange.dwUsableItemIDMin);

	return true;
}

DWORD CClientManager::GainItemID()
{
	return m_itemRange.dwUsableItemIDMin++;
}

DWORD CClientManager::GetItemID()
{
	return m_itemRange.dwUsableItemIDMin;
}
// ITEM_UNIQUE_ID_END
//BOOT_LOCALIZATION

bool CClientManager::InitializeLocalization()
{
	char szQuery[512];
	snprintf(szQuery, sizeof(szQuery), "SELECT mValue, mKey FROM locale");
	SQLMsg* pMsg = CDBManager::instance().DirectQuery(szQuery, SQL_COMMON);

	if (pMsg->Get()->uiNumRows == 0)
	{
		sys_err("InitializeLocalization() ==> DirectQuery failed(%s)", szQuery);
		delete pMsg;
		return false;
	}

	sys_log(0, "InitializeLocalization() - LoadLocaleTable(count:%d)", pMsg->Get()->uiNumRows);

	m_vec_Locale.clear();

	MYSQL_ROW row = nullptr;

	for (int n = 0; (row = mysql_fetch_row(pMsg->Get()->pSQLResult)) != nullptr; ++n)
	{
		int col = 0;
		tLocale locale;

		strlcpy(locale.szValue, row[col++], sizeof(locale.szValue));
		strlcpy(locale.szKey, row[col++], sizeof(locale.szKey));

		//DB_NAME_COLUMN Setting
		if (strcmp(locale.szKey, "LOCALE") == 0)
		{
			if (strcmp(locale.szValue, "cibn") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "gb2312");

				g_stLocale = "gb2312";
				g_stLocaleNameColumn = "gb2312name";
			}
			else if (strcmp(locale.szValue, "ymir") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "euckr";
				g_stLocaleNameColumn = "name";
			}
			else if (strcmp(locale.szValue, "japan") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "sjis");

				g_stLocale = "sjis";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "english") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "germany") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "france") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "italy") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "spain") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "uk") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "turkey") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin5");

				g_stLocale = "latin5";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "poland") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin2";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "portugal") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "hongkong") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "big5");

				g_stLocale = "big5";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "newcibn") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "gb2312");

				g_stLocale = "gb2312";
				g_stLocaleNameColumn = "gb2312name";
			}
			else if (strcmp(locale.szValue, "korea") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "euckr";
				g_stLocaleNameColumn = "name";
			}
			else if (strcmp(locale.szValue, "canada") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "gb2312name";
			}
			else if (strcmp(locale.szValue, "brazil") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "greek") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "greek";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "russia") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "cp1251";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "denmark") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "bulgaria") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "cp1251";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "croatia") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "cp1251";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "mexico") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "arabia") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "cp1256";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "czech") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin2";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "hungary") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin2";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "romania") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin2";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "netherlands") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "singapore") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "vietnam") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "thailand") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "usa") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "latin1");

				g_stLocale = "latin1";
				g_stLocaleNameColumn = "locale_name";
			}
			else if (strcmp(locale.szValue, "we_korea") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "euckr");

				g_stLocale = "euckr";
				g_stLocaleNameColumn = "name";
			}
			else if (strcmp(locale.szValue, "taiwan") == 0)
			{
				sys_log(0, "locale[LOCALE] = %s", locale.szValue);

				if (g_stLocale != locale.szValue)
					sys_log(0, "Changed g_stLocale %s to %s", g_stLocale.c_str(), "big5");
				g_stLocale = "big5";
				g_stLocaleNameColumn = "locale_name";
			}
			else
			{
				sys_err("locale[LOCALE] = UNKNOWN(%s)", locale.szValue);
				exit(0);
			}

			//CDBManager::instance().SetLocale(g_stLocale.c_str());
		}
		else if (strcmp(locale.szKey, "DB_NAME_COLUMN") == 0)
		{
			sys_log(0, "locale[DB_NAME_COLUMN] = %s", locale.szValue);
			g_stLocaleNameColumn = locale.szValue;
		}
		else
		{
			sys_log(0, "locale[UNKNOWN_KEY(%s)] = %s", locale.szKey, locale.szValue);
		}
		m_vec_Locale.push_back(locale);
	}

	delete pMsg;

	return true;
}
//END_BOOT_LOCALIZATION
//ADMIN_MANAGER

bool CClientManager::__GetAdminInfo(const char* szIP, std::vector<tAdminInfo> & rAdminVec)
{
	//szIP == nullptr 일경우  모든서버에 운영자 권한을 갖는다.
	char szQuery[512];
	snprintf(szQuery, sizeof(szQuery),
		"SELECT mID,mAccount,mName,mContactIP,mServerIP,mAuthority FROM gmlist WHERE mServerIP='ALL' or mServerIP='%s'",
		szIP ? szIP : "ALL");

	SQLMsg * pMsg = CDBManager::instance().DirectQuery(szQuery, SQL_COMMON);

	if (pMsg->Get()->uiNumRows == 0)
	{
		sys_err("__GetAdminInfo() ==> DirectQuery failed(%s)", szQuery);
		delete pMsg;
		return false;
	}

	MYSQL_ROW row;
	rAdminVec.reserve(pMsg->Get()->uiNumRows);

	while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
	{
		int idx = 0;
		tAdminInfo Info;

		str_to_number(Info.m_ID, row[idx++]);
		trim_and_lower(row[idx++], Info.m_szAccount, sizeof(Info.m_szAccount));
		strlcpy(Info.m_szName, row[idx++], sizeof(Info.m_szName));
		strlcpy(Info.m_szContactIP, row[idx++], sizeof(Info.m_szContactIP));
		strlcpy(Info.m_szServerIP, row[idx++], sizeof(Info.m_szServerIP));
		std::string stAuth = row[idx++];

		if (!stAuth.compare("IMPLEMENTOR"))
			Info.m_Authority = GM_IMPLEMENTOR;
		else if (!stAuth.compare("GOD"))
			Info.m_Authority = GM_GOD;
		else if (!stAuth.compare("HIGH_WIZARD"))
			Info.m_Authority = GM_HIGH_WIZARD;
		else if (!stAuth.compare("LOW_WIZARD"))
			Info.m_Authority = GM_LOW_WIZARD;
		else if (!stAuth.compare("WIZARD"))
			Info.m_Authority = GM_WIZARD;
		else
			continue;

		rAdminVec.push_back(Info);

		sys_log(0, "GM: PID %u Login %s Character %s ContactIP %s ServerIP %s Authority %d[%s]",
			Info.m_ID, Info.m_szAccount, Info.m_szName, Info.m_szContactIP, Info.m_szServerIP, Info.m_Authority, stAuth.c_str());
	}

	delete pMsg;

	return true;
}

bool CClientManager::__GetHostInfo(std::vector<std::string> & rIPVec)
{
	char szQuery[512];
	snprintf(szQuery, sizeof(szQuery), "SELECT mIP FROM gmhost");
	SQLMsg* pMsg = CDBManager::instance().DirectQuery(szQuery, SQL_COMMON);

	if (pMsg->Get()->uiNumRows == 0)
	{
		//sys_err("__GetHostInfo() ==> DirectQuery failed(%s)", szQuery);
		delete pMsg;
		return false;
	}

	rIPVec.reserve(pMsg->Get()->uiNumRows);

	MYSQL_ROW row;

	while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
	{
		if (row[0] && *row[0])
		{
			rIPVec.push_back(row[0]);
			sys_log(0, "GMHOST: %s", row[0]);
		}
	}

	delete pMsg;
	return true;
}
//END_ADMIN_MANAGER

void CClientManager::ReloadAdmin(CPeer*, TPacketReloadAdmin * p)
{
	std::vector<tAdminInfo> vAdmin;
	std::vector<std::string> vHost;

	__GetHostInfo(vHost);
	__GetAdminInfo(p->szIP, vAdmin);

	DWORD dwPacketSize = sizeof(WORD) + sizeof(WORD) + sizeof(tAdminInfo) * vAdmin.size() +
		sizeof(WORD) + sizeof(WORD) + 16 * vHost.size();

	for (auto it = m_peerList.begin(); it != m_peerList.end(); ++it)
	{
		CPeer* peer = *it;

		if (!peer->GetChannel())
			continue;

		peer->EncodeHeader(HEADER_DG_RELOAD_ADMIN, 0, dwPacketSize);

		peer->EncodeWORD(16);
		peer->EncodeWORD(vHost.size());

		for (size_t n = 0; n < vHost.size(); ++n)
			peer->Encode(vHost[n].c_str(), 16);

		peer->EncodeWORD(sizeof(tAdminInfo));
		peer->EncodeWORD(vAdmin.size());

		for (size_t n = 0; n < vAdmin.size(); ++n)
			peer->Encode(&vAdmin[n], sizeof(tAdminInfo));
	}

	sys_log(0, "ReloadAdmin End %s", p->szIP);
}

//BREAK_MARRIAGE
void CClientManager::BreakMarriage(CPeer * peer, const char* data)
{
	DWORD pid1, pid2;

	pid1 = *(int*)data;
	data += sizeof(int);

	pid2 = *(int*)data;
	data += sizeof(int);

	sys_log(0, "Breaking off a marriage engagement! pid %d and pid %d", pid1, pid2);
	marriage::CManager::instance().Remove(pid1, pid2);
}
//END_BREAK_MARIIAGE

void CClientManager::UpdateItemCacheSet(DWORD pid)
{
	auto it = m_map_pkItemCacheSetPtr.find(pid);

	if (it == m_map_pkItemCacheSetPtr.end())
	{
		if (g_test_server)
			sys_log(0, "UPDATE_ITEMCACHESET : UpdateItemCacheSet ==> No ItemCacheSet pid(%d)", pid);
		return;
	}

	TItemCacheSet* pSet = it->second;
	TItemCacheSet::iterator it_set = pSet->begin();

	while (it_set != pSet->end())
	{
		CItemCache* c = *it_set++;
		c->Flush();
	}

	if (g_log)
		sys_log(0, "UPDATE_ITEMCACHESET : UpdateItemCachsSet pid(%d)", pid);
}

void CClientManager::SendSpareItemIDRange(CPeer * peer)
{
	peer->SendSpareItemIDRange();
}

//
// Login Key만 맵에서 지운다.
//
void CClientManager::DeleteLoginKey(TPacketDC * data)
{
	char login[LOGIN_MAX_LEN + 1] = { 0 };
	trim_and_lower(data->login, login, sizeof(login));

	CLoginData* pkLD = GetLoginDataByLogin(login);

	if (pkLD)
	{
		TLoginDataByLoginKey::iterator it = m_map_pkLoginData.find(pkLD->GetKey());

		if (it != m_map_pkLoginData.end())
			m_map_pkLoginData.erase(it);
	}
}

// delete gift notify icon
void CClientManager::DeleteAwardId(TPacketDeleteAwardID * data)
{
	//sys_log(0,"data from game server arrived %d",data->dwID);
	std::map<DWORD, TItemAward*>::iterator it;
	it = ItemAwardManager::Instance().GetMapAward().find(data->dwID);
	if (it != ItemAwardManager::Instance().GetMapAward().end())
	{
		std::set<TItemAward*>& kSet = ItemAwardManager::Instance().GetMapkSetAwardByLogin()[it->second->szLogin];
		if (kSet.erase(it->second))
			sys_log(0, "erase ItemAward id: %d from cache", data->dwID);
		ItemAwardManager::Instance().GetMapAward().erase(data->dwID);
	}
	else
	{
		sys_log(0, "DELETE_AWARDID : could not find the id: %d", data->dwID);
	}
}

void CClientManager::UpdateChannelStatus(TChannelStatus * pData)
{
	TChannelStatusMap::iterator it = m_mChannelStatus.find(pData->nPort);
	if (it != m_mChannelStatus.end()) {
		it->second = pData->bStatus;
	}
	else {
		m_mChannelStatus.insert(TChannelStatusMap::value_type(pData->nPort, pData->bStatus));
	}
}

void CClientManager::RequestChannelStatus(CPeer * peer, DWORD dwHandle)
{
	const int nSize = m_mChannelStatus.size();
	peer->EncodeHeader(HEADER_DG_RESPOND_CHANNELSTATUS, dwHandle, sizeof(TChannelStatus) * nSize + sizeof(int));
	peer->Encode(&nSize, sizeof(int));
	for (TChannelStatusMap::iterator it = m_mChannelStatus.begin(); it != m_mChannelStatus.end(); it++) {
		peer->Encode(&it->first, sizeof(short));
		peer->Encode(&it->second, sizeof(BYTE));
	}
}

void CClientManager::ResetLastPlayerID(const TPacketNeedLoginLogInfo * data)
{
	CLoginData* pkLD = GetLoginDataByAID(data->dwPlayerID);

	if (nullptr != pkLD)
	{
		pkLD->SetLastPlayerID(0);
	}
}

#if defined(ENABLE_BATTLE_ZONE_SYSTEM)
void CClientManager::CombatZoneResetRanking(const TPacketGDCombatZoneResetRanking * p)
{
	CDBManager::instance().DirectQuery("UPDATE player.player SET combat_zone_rank = 0 WHERE combat_zone_rank > 0");

	std::unique_ptr<SQLMsg> pMsg(CDBManager::instance().DirectQuery("SELECT * FROM player.combat_zone_ranking_weekly ORDER BY memberPoints DESC LIMIT 3"));
	if (pMsg->Get()->uiNumRows == 3)
	{
		MYSQL_ROW row;
		int memberRank = 1;
		char szQuery[512 + 1];
		while ((row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
		{
			sprintf(szQuery, "UPDATE player.player SET combat_zone_rank = '%d' WHERE name = '%s'", memberRank, row[0]);
			CDBManager::instance().DirectQuery(szQuery);
			memberRank++;
		}
	}
	else {
		sys_err("The giving ranking medals not was possible because not was exist 3 players on ranking weekly.");
	}

	CDBManager::instance().DirectQuery("TRUNCATE TABLE player.combat_zone_ranking_weekly");
}

void CClientManager::UpdateSkillsCache(const TPacketGDCombatZoneSkillsCache * p)
{
	char szQuery[2048 + 1];
	sprintf(szQuery,
		"INSERT INTO player.combat_zone_skills_cache (pid, skillLevel1, skillLevel2, skillLevel3, skillLevel4, skillLevel5, skillLevel6) "
		"VALUES('%d', '%d', '%d', '%d', '%d', '%d', '%d') "
		"ON DUPLICATE KEY UPDATE skillLevel1 = '%d', skillLevel2 = '%d', skillLevel3 = '%d', skillLevel4 = '%d', skillLevel5 = '%d', skillLevel6 = '%d'",
		p->dwPID, p->dwSkillLevel1, p->dwSkillLevel2, p->dwSkillLevel3, p->dwSkillLevel4, p->dwSkillLevel5, p->dwSkillLevel6, p->dwSkillLevel1, p->dwSkillLevel2, p->dwSkillLevel3, p->dwSkillLevel4, p->dwSkillLevel5, p->dwSkillLevel6);
	CDBManager::instance().DirectQuery(szQuery);
}
#endif

void CClientManager::ChargeCash(const TRequestChargeCash * packet)
{
	char szQuery[512];

	if (ERequestCharge_Cash == packet->eChargeType)
		sprintf(szQuery, "update account set `cash` = `cash` + %d where id = %d limit 1", packet->dwAmount, packet->dwAID);
	else if (ERequestCharge_Mileage == packet->eChargeType)
		sprintf(szQuery, "update account set `mileage` = `mileage` + %d where id = %d limit 1", packet->dwAmount, packet->dwAID);
#ifdef ENABLE_DC_COUPON_SYSTEM
	else if (ERequestCharge_Coins == packet->eChargeType)
		sprintf(szQuery, "update account set `coins` = `coins` + %d where id = %d limit 1", packet->dwAmount, packet->dwAID);
#endif
	else
	{
		sys_err("Invalid request charge type (type : %d, amount : %d, aid : %d)", packet->eChargeType, packet->dwAmount, packet->dwAID);
		return;
	}

	sys_err("Request Charge (type : %d, amount : %d, aid : %d)", packet->eChargeType, packet->dwAmount, packet->dwAID);

	CDBManager::Instance().AsyncQuery(szQuery, SQL_ACCOUNT);
}

#ifdef ENABLE_OFFLINE_SHOP_SYSTEM
void CClientManager::ShopName(CPeer * peer, TPacketShopName * p)
{
	for (TPeerList::iterator i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (tmp == peer)
			continue;
		if (0 == tmp->GetChannel())
			continue;
		TPacketShopName packet;
		packet.shop_id = p->shop_id;
		strlcpy(packet.szSign, p->szSign, sizeof(packet.szSign) - 1);
		tmp->EncodeHeader(HEADER_DG_SHOP_NAME, 0, sizeof(packet));
		tmp->Encode(&packet, sizeof(packet));
	}
}

void CClientManager::ShopClose(CPeer * peer, TPacketShopClose * p)
{
	for (TPeerList::iterator i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (tmp == peer)
			continue;

		if (0 == tmp->GetChannel())
			continue;

		TPacketShopClose packet;
		packet.shop_id = p->shop_id;
		packet.pid = p->pid;
		tmp->EncodeHeader(HEADER_DG_SHOP_CLOSE, 0, sizeof(packet));
		tmp->Encode(&packet, sizeof(packet));
	}
}
void CClientManager::ShopUpdateItem(CPeer * peer, TPacketShopUpdateItem * p)
{
	for (TPeerList::iterator i = m_peerList.begin(); i != m_peerList.end(); ++i)
	{
		CPeer* tmp = *i;

		if (tmp == peer)
			continue;

		if (0 == tmp->GetChannel())
			continue;
		TPacketShopUpdateItem packet;
		packet.shop_id = p->shop_id;
		packet.shop_locked = p->shop_locked;
		tmp->EncodeHeader(HEADER_DG_SHOP_UPDATE_ITEM, 0, sizeof(packet));
		tmp->Encode(&packet, sizeof(packet));
	}
}
#endif

#ifdef ENABLE_DUNGEON_FOR_GUILD
void CClientManager::GuildDungeon(TPacketGDGuildDungeon * sPacket)
{
	CGuildManager::instance().Dungeon(sPacket->dwGuildID, sPacket->bChannel, sPacket->lMapIndex);
}

void CClientManager::GuildDungeonGD(TPacketGDGuildDungeonCD * sPacket)
{
	CGuildManager::instance().DungeonCooldown(sPacket->dwGuildID, sPacket->dwTime);
}
#endif

#ifdef ENABLE_CHANNEL_SWITCH_SYSTEM
void CClientManager::FindChannel(CPeer * requestPeer, DWORD dwHandle, TPacketChangeChannel * p)
{
	if (!p->lMapIndex || !p->channel)
		return;

	long lAddr = 0;
	WORD port = 0;

	for (const auto peer : m_peerList)
	{
		if (peer->GetChannel() != p->channel) //not the channel we are looking for!
			continue;

		TMapLocation kMapLocation;
		thecore_memcpy(kMapLocation.alMaps, peer->GetMaps(), sizeof(kMapLocation.alMaps));

		for (const auto midx : kMapLocation.alMaps)
		{
			if (midx == p->lMapIndex)
			{
				//Get host, and convert to int
				char host[16];
				strlcpy(host, peer->GetPublicIP(), sizeof(kMapLocation.szHost));
				lAddr = inet_addr(host);

				//Target port
				port = peer->GetListenPort();

				break;
			}
		}

		if (lAddr && port) //We already obtained them
			break;
	}

	TPacketReturnChannel r;
	r.lAddr = lAddr;
	r.port = port;

	requestPeer->EncodeHeader(HEADER_DG_CHANNEL_RESULT, dwHandle, sizeof(r));
	requestPeer->Encode(&r, sizeof(r));
}
#endif

#ifdef ENABLE_SAFEBOX_MONEY_SYSTEM
void CClientManager::SafeboxMoney(TSafeboxMoney* pData)
{
	char szQuery[512];
	if (pData->bType)
		sprintf(szQuery, "UPDATE safebox%s SET `gold`=`gold`+%lld WHERE account_id=%u LIMIT 1", GetTablePostfix(), pData->llAmount, pData->dwAID);
	else
		sprintf(szQuery, "UPDATE safebox%s SET `gold`=`gold`-%lld WHERE account_id=%u LIMIT 1", GetTablePostfix(), pData->llAmount, pData->dwAID);

	std::unique_ptr<SQLMsg> pMsg(CDBManager::instance().DirectQuery(szQuery, SQL_PLAYER));

	if (pData->bType && pMsg->Get()->uiAffectedRows == 0)
	{
		sprintf(szQuery, "INSERT INTO safebox%s (account_id, gold) VALUES(%u, %lld)", GetTablePostfix(), pData->dwAID, (pData->llAmount>0 ? pData->llAmount : 0LL));
		CDBManager::Instance().AsyncQuery(szQuery, SQL_PLAYER);
	}

	CClientManager::instance().ForwardPacket(HEADER_DG_SAFEBOX_MONEY, pData, sizeof(TSafeboxMoney));
}

void CClientManager::SafeboxCheque(TSafeboxCheque* pData)
{
	char szQuery[512];
	if (pData->bType)
		sprintf(szQuery, "UPDATE safebox%s SET `cheque`=`cheque`+%d WHERE account_id=%u LIMIT 1", GetTablePostfix(), pData->wAmount, pData->dwAID);
	else
		sprintf(szQuery, "UPDATE safebox%s SET `cheque`=`cheque`-%d WHERE account_id=%u LIMIT 1", GetTablePostfix(), pData->wAmount, pData->dwAID);

	std::unique_ptr<SQLMsg> pMsg(CDBManager::instance().DirectQuery(szQuery, SQL_PLAYER));

	if (pData->bType && pMsg->Get()->uiAffectedRows == 0)
	{
		sprintf(szQuery, "INSERT INTO safebox%s (account_id, cheque) VALUES(%u, %d)", GetTablePostfix(), pData->dwAID, (pData->wAmount>0 ? pData->wAmount : 0));
		CDBManager::Instance().AsyncQuery(szQuery, SQL_PLAYER);
	}

	CClientManager::instance().ForwardPacket(HEADER_DG_SAFEBOX_CHEQUE, pData, sizeof(TSafeboxCheque));
}
#endif