#ifndef VITA_LAUNCHER_PACKS_H
#define VITA_LAUNCHER_PACKS_H

#include <string>
#include <vector>

struct PackEntry
{
	std::string sName;
	std::string sUrl;
	std::string sGroup;   /* destination folder under Songs/ */
	std::string sBanner;  /* optional cover art URL from the index */
	std::string sDate;    /* release date as published, may be empty */
	int iSongs;           /* 0 when the index does not say */
	double fSizeMB;
	bool bInstalled;
};

/* Pack index + downloader used by the launcher. Engine-free: it only needs
 * LauncherHttp, jsoncpp and miniz.
 *
 * Two lists are kept:
 *   - the curated index (packs.json), fetched when the browser opens;
 *   - the full catalogue (packs-all.json), fetched the first time the player
 *     searches and reused until the launcher exits. */
class LauncherPacks
{
public:
	/* Called while a transfer or an unpack is in progress. Returning false
	 * cancels the job. */
	typedef bool (*PumpFn)( void *pUser, const char *szStage, float fProgress,
		long long iDone, long long iTotal );

	LauncherPacks();

	void SetPump( PumpFn pfnPump, void *pUser );
	void SetIndexUrl( const std::string &sUrl ) { m_sIndexUrl = sUrl; }
	const std::string &GetIndexUrl() const { return m_sIndexUrl; }
	void SetSearchIndexUrl( const std::string &sUrl ) { m_sSearchIndexUrl = sUrl; }
	const std::string &GetSearchIndexUrl() const { return m_sSearchIndexUrl; }

	bool FetchIndex( std::string &sError );
	bool FetchSearchIndex( std::string &sError );
	bool HasSearchIndex() const { return m_bSearchReady; }

	bool Install( size_t iPack, std::string &sError );
	bool InstallSearch( size_t iPack, std::string &sError );

	const std::vector<PackEntry> &GetPacks() const { return m_vPacks; }
	const std::vector<PackEntry> &GetSearchPacks() const { return m_vSearchPacks; }

	void RefreshInstalled();
	/* Checks the green "installed" dot for the entries in a view. bAll selects
	 * the full catalogue; the count is capped because every check is a stat
	 * on the memory card. */
	void RefreshInstalledFor( const std::vector<int> &vView, bool bAll,
		size_t iLimit = 500 );

private:
	bool Pump( const char *szStage, float fProgress, long long iDone, long long iTotal );
	bool Fetch( const std::string &sUrl, const char *szStage, std::vector<PackEntry> &vOut,
		std::string &sError );
	bool InstallEntry( PackEntry &pack, std::string &sError );
	bool Extract( const std::string &sZip, const std::string &sGroup, std::string &sError );
	static bool ProgressThunk( void *pUser, long long iDone, long long iTotal );

	std::vector<PackEntry> m_vPacks;
	std::vector<PackEntry> m_vSearchPacks;
	std::string m_sIndexUrl;
	std::string m_sSearchIndexUrl;
	bool m_bSearchReady;
	PumpFn m_pfnPump;
	void *m_pUser;
};

#endif
