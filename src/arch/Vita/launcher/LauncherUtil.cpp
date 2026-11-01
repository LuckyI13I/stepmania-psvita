#include "LauncherUtil.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace LauncherUtil
{
	const char *kDataRoot = "ux0:data/stepmania-5.0";

	/* The log file is opened on demand: the launcher may run before the data
	 * tree exists, and it must never fail because of logging. */
	static void AppendLog( const char *szText )
	{
		const std::string sPath = DataPath( "launcher.log" );
		SceUID fd = sceIoOpen( sPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666 );
		if( fd < 0 )
			return;
		sceIoWrite( fd, szText, (SceSize)strlen( szText ) );
		sceIoClose( fd );
	}

	std::string DataPath( const std::string &sRel )
	{
		if( sRel.empty() )
			return std::string( kDataRoot );
		return std::string( kDataRoot ) + "/" + sRel;
	}

	void Log( const char *szFmt, ... )
	{
		char szBuf[512];
		va_list args;
		va_start( args, szFmt );
		vsnprintf( szBuf, sizeof(szBuf) - 2, szFmt, args );
		va_end( args );
		const size_t iLen = strlen( szBuf );
		szBuf[iLen] = '\n';
		szBuf[iLen + 1] = 0;
		AppendLog( szBuf );
	}

	bool DirExists( const std::string &sPath )
	{
		SceIoStat st;
		memset( &st, 0, sizeof(st) );
		return sceIoGetstat( sPath.c_str(), &st ) >= 0 &&
			SCE_S_ISDIR( st.st_mode );
	}

	bool Mkdirs( const std::string &sPath )
	{
		if( DirExists( sPath ) )
			return true;

		std::string sBuild;
		size_t iStart = 0;
		while( iStart <= sPath.size() )
		{
			size_t iSlash = sPath.find( '/', iStart );
			if( iSlash == std::string::npos )
				iSlash = sPath.size();
			sBuild = sPath.substr( 0, iSlash );
			if( !sBuild.empty() && sBuild[sBuild.size() - 1] != ':' )
				sceIoMkdir( sBuild.c_str(), 0777 );
			if( iSlash == sPath.size() )
				break;
			iStart = iSlash + 1;
		}
		return DirExists( sPath );
	}

	bool RemoveFile( const std::string &sPath )
	{
		return sceIoRemove( sPath.c_str() ) >= 0;
	}

	std::string Sanitize( const std::string &sIn )
	{
		std::string out;
		for( size_t i = 0; i < sIn.size(); ++i )
		{
			const unsigned char c = (unsigned char)sIn[i];
			if( isalnum( c ) || c == ' ' || c == '-' || c == '_' || c == '.' )
				out += (char)c;
			else
				out += '_';
		}
		const size_t iFirst = out.find_first_not_of( " ." );
		if( iFirst == std::string::npos )
			return "Pack";
		const size_t iLast = out.find_last_not_of( " ." );
		out = out.substr( iFirst, iLast - iFirst + 1 );
		if( out.size() > 96 )
			out = out.substr( 0, 96 );
		return out.empty() ? std::string( "Pack" ) : out;
	}

	std::string FormatBytes( long long iBytes )
	{
		char szBuf[64];
		if( iBytes >= 1024LL * 1024 * 1024 )
			snprintf( szBuf, sizeof(szBuf), "%.2f GB", (double)iBytes / ( 1024.0 * 1024 * 1024 ) );
		else if( iBytes >= 1024 * 1024 )
			snprintf( szBuf, sizeof(szBuf), "%.1f MB", (double)iBytes / ( 1024.0 * 1024 ) );
		else if( iBytes >= 1024 )
			snprintf( szBuf, sizeof(szBuf), "%.0f KB", (double)iBytes / 1024.0 );
		else
			snprintf( szBuf, sizeof(szBuf), "%lld B", iBytes );
		return szBuf;
	}

	std::string Lower( const std::string &sIn )
	{
		std::string out( sIn );
		for( size_t i = 0; i < out.size(); ++i )
			out[i] = (char)tolower( (unsigned char)out[i] );
		return out;
	}
}
