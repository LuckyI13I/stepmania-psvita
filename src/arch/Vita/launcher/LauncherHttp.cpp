#include "LauncherHttp.h"
#include "LauncherUtil.h"

#include <curl/curl.h>

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/io/fcntl.h>

#include <cstdio>
#include <cstring>

namespace
{
	/* 0 = not initialised, 1 = ready, -1 = failed. */
	int g_iState = 0;
	std::string g_sError;

	/* sceNetInit needs its memory to outlive the session. */
	const SceSize kNetPoolSize = 1024 * 1024;
	char *g_pNetMemory = NULL;

	bool g_bCurlInit = false;

	std::string HexError( int iRet )
	{
		char szBuf[32];
		snprintf( szBuf, sizeof(szBuf), "0x%08X", (unsigned)iRet );
		return szBuf;
	}

	bool FileExists( const std::string &sPath )
	{
		const SceUID fd = sceIoOpen( sPath.c_str(), SCE_O_RDONLY, 0 );
		if( fd < 0 )
			return false;
		sceIoClose( fd );
		return true;
	}

	std::string PickCaBundle()
	{
		const std::string sUser = LauncherUtil::DataPath( "cacert.pem" );
		if( FileExists( sUser ) )
			return sUser;
		if( FileExists( "app0:/cacert.pem" ) )
			return "app0:/cacert.pem";
		return std::string();
	}

	struct Sink
	{
		std::string *pStr;      /* set for GetToString */
		SceUID iFd;             /* set for GetToFile */
		LauncherHttp::ProgressFn pfn;
		void *pUser;
		long long iDone;
		long long iTotal;
		bool bCancel;
		bool bWriteError;
	};

	/* The full catalogue (packs-all.json) is a few megabytes; 8 MB leaves room
	 * for the list to grow without failing the transfer. */
	const size_t kMaxStringBody = 8 * 1024 * 1024;

	size_t WriteToString( char *pData, size_t iSize, size_t iCount, void *pUser )
	{
		Sink *pSink = (Sink *)pUser;
		const size_t iLen = iSize * iCount;
		if( pSink->pStr->size() + iLen > kMaxStringBody )
			return 0;
		pSink->pStr->append( pData, iLen );
		return iLen;
	}

	size_t WriteToFile( char *pData, size_t iSize, size_t iCount, void *pUser )
	{
		Sink *pSink = (Sink *)pUser;
		const size_t iLen = iSize * iCount;
		if( iLen == 0 )
			return 0;
		if( sceIoWrite( pSink->iFd, pData, (SceSize)iLen ) != (int)iLen )
		{
			pSink->bWriteError = true;
			return 0;
		}
		pSink->iDone += (long long)iLen;
		if( pSink->pfn != NULL && !pSink->pfn( pSink->pUser, pSink->iDone, pSink->iTotal ) )
		{
			pSink->bCancel = true;
			return 0;
		}
		return iLen;
	}

	int OnProgress( void *pUser, curl_off_t iDlTotal, curl_off_t iDlNow,
		curl_off_t, curl_off_t )
	{
		Sink *pSink = (Sink *)pUser;
		if( pSink->pfn == NULL )
			return 0;
		if( iDlTotal > 0 )
			pSink->iTotal = (long long)iDlTotal;
		if( !pSink->pfn( pSink->pUser, (long long)iDlNow, pSink->iTotal ) )
			return 1;
		return 0;
	}

	/* Returns 0 on success, 1 when the request failed for another reason and 2
	 * when TLS verification is the suspect. */
	int RunTransfer( const std::string &sUrl, Sink &sink, bool bVerify, std::string &sError )
	{
		CURL *pHandle = curl_easy_init();
		if( pHandle == NULL )
		{
			sError = "curl_easy_init failed";
			return 1;
		}

		char szCurlError[CURL_ERROR_SIZE];
		memset( szCurlError, 0, sizeof(szCurlError) );

		const std::string sCa = bVerify ? PickCaBundle() : std::string();

		curl_easy_setopt( pHandle, CURLOPT_URL, sUrl.c_str() );
		curl_easy_setopt( pHandle, CURLOPT_USERAGENT, "StepMania5-Vita/1.0" );
		curl_easy_setopt( pHandle, CURLOPT_FOLLOWLOCATION, 1L );
		curl_easy_setopt( pHandle, CURLOPT_MAXREDIRS, 8L );
		curl_easy_setopt( pHandle, CURLOPT_CONNECTTIMEOUT, 20L );
		curl_easy_setopt( pHandle, CURLOPT_LOW_SPEED_LIMIT, 1024L );
		curl_easy_setopt( pHandle, CURLOPT_LOW_SPEED_TIME, 30L );
		curl_easy_setopt( pHandle, CURLOPT_NOSIGNAL, 1L );
		curl_easy_setopt( pHandle, CURLOPT_ERRORBUFFER, szCurlError );
		curl_easy_setopt( pHandle, CURLOPT_WRITEFUNCTION,
			sink.pStr != NULL ? WriteToString : WriteToFile );
		curl_easy_setopt( pHandle, CURLOPT_WRITEDATA, &sink );
		curl_easy_setopt( pHandle, CURLOPT_XFERINFOFUNCTION, OnProgress );
		curl_easy_setopt( pHandle, CURLOPT_XFERINFODATA, &sink );
		curl_easy_setopt( pHandle, CURLOPT_NOPROGRESS, 0L );
		curl_easy_setopt( pHandle, CURLOPT_SSL_VERIFYPEER, bVerify ? 1L : 0L );
		curl_easy_setopt( pHandle, CURLOPT_SSL_VERIFYHOST, bVerify ? 2L : 0L );
		if( !sCa.empty() )
			curl_easy_setopt( pHandle, CURLOPT_CAINFO, sCa.c_str() );

		LauncherUtil::Log( "http: GET %s (verify %d%s%s)", sUrl.c_str(), bVerify ? 1 : 0,
			sCa.empty() ? "" : ", ca ", sCa.c_str() );

		const CURLcode rc = curl_easy_perform( pHandle );
		long iCode = 0;
		curl_easy_getinfo( pHandle, CURLINFO_RESPONSE_CODE, &iCode );
		curl_easy_cleanup( pHandle );

		if( sink.bCancel )
		{
			sError = "cancelled";
			return 1;
		}
		if( rc == CURLE_OK && iCode == 200 )
			return 0;

		if( rc != CURLE_OK )
		{
			char szBuf[160];
			snprintf( szBuf, sizeof(szBuf), "HTTP error %d (%s)", (int)rc,
				curl_easy_strerror( rc ) );
			sError = szBuf;
			if( szCurlError[0] != '\0' )
			{
				sError += ": ";
				sError += szCurlError;
			}
			LauncherUtil::Log( "http: transfer failed, %s", sError.c_str() );

			switch( rc )
			{
			case CURLE_PEER_FAILED_VERIFICATION:
			case CURLE_SSL_CONNECT_ERROR:
			case CURLE_SSL_CERTPROBLEM:
			case CURLE_SSL_CIPHER:
			case CURLE_USE_SSL_FAILED:
				return 2;
			default:
				return 1;
			}
		}

		char szBuf[64];
		snprintf( szBuf, sizeof(szBuf), "HTTP %ld", iCode );
		sError = szBuf;
		LauncherUtil::Log( "http: status %ld", iCode );
		return 1;
	}

	void ResetSink( Sink &sink )
	{
		sink.iDone = 0;
		sink.bCancel = false;
		sink.bWriteError = false;
	}
}

bool LauncherHttp::EnsureInit( std::string &sError )
{
	if( g_iState == 1 )
		return true;
	if( g_iState == -1 )
	{
		sError = g_sError;
		return false;
	}

	int iRet = sceSysmoduleLoadModule( SCE_SYSMODULE_NET );
	if( iRet < 0 && iRet != (int)0x8002D080 )
	{
		g_sError = "sceSysmoduleLoadModule(NET) failed (" + HexError( iRet ) + ")";
		g_iState = -1;
		sError = g_sError;
		return false;
	}

	if( g_pNetMemory == NULL )
		g_pNetMemory = new char[kNetPoolSize];
	if( g_pNetMemory == NULL )
	{
		g_sError = "out of memory for the network pool";
		g_iState = -1;
		sError = g_sError;
		return false;
	}

	SceNetInitParam param;
	memset( &param, 0, sizeof(param) );
	param.memory = g_pNetMemory;
	param.size = (int)kNetPoolSize;
	iRet = sceNetInit( &param );
	if( iRet < 0 && iRet != (int)0x80410201 )
	{
		g_sError = "sceNetInit failed (" + HexError( iRet ) + ")";
		g_iState = -1;
		sError = g_sError;
		return false;
	}

	iRet = sceNetCtlInit();
	LauncherUtil::Log( "http: net ready (netctl %s)", HexError( iRet ).c_str() );

	const CURLcode rc = curl_global_init( CURL_GLOBAL_DEFAULT );
	if( rc != CURLE_OK )
	{
		g_sError = std::string( "curl_global_init failed (" ) + curl_easy_strerror( rc ) + ")";
		g_iState = -1;
		sError = g_sError;
		return false;
	}
	g_bCurlInit = true;

	LauncherUtil::Log( "http: curl %s, ca bundle '%s'", curl_version_info( CURLVERSION_NOW )->version,
		PickCaBundle().c_str() );

	g_iState = 1;
	return true;
}

void LauncherHttp::Shutdown()
{
	if( g_iState != 1 )
		return;

	if( g_bCurlInit )
	{
		curl_global_cleanup();
		g_bCurlInit = false;
	}

	sceNetCtlTerm();
	sceNetTerm();

	/* Give the RAM back before the game starts. */
	delete[] g_pNetMemory;
	g_pNetMemory = NULL;

	g_iState = 0;
	LauncherUtil::Log( "http: stack released" );
}

bool LauncherHttp::GetToString( const std::string &sUrl, std::string &sOut, std::string &sError )
{
	if( !EnsureInit( sError ) )
		return false;

	Sink sink;
	memset( &sink, 0, sizeof(sink) );
	sink.pStr = &sOut;
	sink.iFd = -1;
	sink.iTotal = -1;

	sOut.clear();
	int iRes = RunTransfer( sUrl, sink, true, sError );
	if( iRes == 2 )
	{
		LauncherUtil::Log( "http: retrying without certificate verification" );
		sOut.clear();
		ResetSink( sink );
		iRes = RunTransfer( sUrl, sink, false, sError );
	}

	if( iRes != 0 )
		return false;
	if( sOut.empty() )
	{
		sError = "empty response";
		return false;
	}
	LauncherUtil::Log( "http: %s -> %d bytes", sUrl.c_str(), (int)sOut.size() );
	return true;
}

bool LauncherHttp::GetToFile( const std::string &sUrl, const std::string &sFile,
	ProgressFn pfnProgress, void *pUser, std::string &sError, long long iTotalHint )
{
	if( !EnsureInit( sError ) )
		return false;

	LauncherUtil::Mkdirs( sFile.substr( 0, sFile.find_last_of( '/' ) ) );

	auto openTarget = [&]() -> SceUID {
		return sceIoOpen( sFile.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666 );
	};

	Sink sink;
	memset( &sink, 0, sizeof(sink) );
	sink.pStr = NULL;
	sink.pfn = pfnProgress;
	sink.pUser = pUser;
	sink.iTotal = iTotalHint;

	SceUID fd = openTarget();
	if( fd < 0 )
	{
		sError = "cannot create " + sFile;
		return false;
	}
	sink.iFd = fd;

	int iRes = RunTransfer( sUrl, sink, true, sError );

	if( iRes == 2 )
	{
		LauncherUtil::Log( "http: retrying without certificate verification" );
		sceIoClose( fd );
		fd = openTarget();
		if( fd < 0 )
		{
			sError = "cannot create " + sFile;
			return false;
		}
		sink.iFd = fd;
		sink.iTotal = iTotalHint;
		ResetSink( sink );
		iRes = RunTransfer( sUrl, sink, false, sError );
	}

	sceIoClose( fd );

	if( iRes != 0 || sink.bWriteError )
	{
		if( sink.bWriteError )
			sError = "write failed (disk full?)";
		sceIoRemove( sFile.c_str() );
		if( iRes == 0 )
			return false;
		return false;
	}
	if( sink.iDone == 0 )
	{
		sceIoRemove( sFile.c_str() );
		sError = "empty response body";
		return false;
	}

	LauncherUtil::Log( "http: %s -> %s (%lld bytes)", sUrl.c_str(), sFile.c_str(), sink.iDone );
	return true;
}
