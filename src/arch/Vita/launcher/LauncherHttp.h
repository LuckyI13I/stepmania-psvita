#ifndef VITA_LAUNCHER_HTTP_H
#define VITA_LAUNCHER_HTTP_H

#include <string>

/* HTTPS client for the pack downloader, built on libcurl + mbedTLS.
 *
 * SceHttp/SceSsl cannot complete a handshake with modern hosts (GitHub answers
 * with SCE_HTTPS_ERROR_HANDSHAKE), so the launcher uses the VitaSDK curl package
 * instead. The CA bundle is taken from app0:/cacert.pem (shipped in the VPK) or
 * from ux0:data/stepmania-5.0/cacert.pem if the player drops a newer one there.
 * When certificate validation fails (usually a wrong clock) the transfer is
 * retried once without verification. */
class LauncherHttp
{
public:
	/* progress returns false to abort the transfer. */
	typedef bool (*ProgressFn)( void *pUser, long long iDone, long long iTotal );

	static bool EnsureInit( std::string &sError );
	static void Shutdown();

	static bool GetToString( const std::string &sUrl, std::string &sOut, std::string &sError );
	/* iTotalHint is the expected size from the index (-1 when unknown); it is
	 * only used for the progress bar. */
	static bool GetToFile( const std::string &sUrl, const std::string &sFile,
		ProgressFn pfnProgress, void *pUser, std::string &sError,
		long long iTotalHint = -1 );
};

#endif
