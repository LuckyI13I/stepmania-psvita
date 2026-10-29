#include "LauncherPacks.h"
#include "LauncherHttp.h"
#include "LauncherUtil.h"

#include "../miniz.h"
#include "json/json.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace
{
	/* Where the indexes live by default. Edit packs.json / packs-all.json in
	 * the repository to publish different lists. */
	const char *kDefaultIndexUrl =
		"https://raw.githubusercontent.com/LuckyI13I/stepmania-psvita/main/packs.json";
	const char *kDefaultSearchIndexUrl =
		"https://raw.githubusercontent.com/LuckyI13I/stepmania-psvita/main/packs-all.json";

	std::string BaseName( const std::string &sPath )
	{
		const size_t iSlash = sPath.find_last_of( '/' );
		if( iSlash == std::string::npos )
			return sPath;
		return sPath.substr( iSlash + 1 );
	}

	std::string StripExtension( const std::string &sPath )
	{
		const size_t iDot = sPath.find_last_of( '.' );
		const size_t iSlash = sPath.find_last_of( '/' );
		if( iDot == std::string::npos )
			return sPath;
		if( iSlash != std::string::npos && iDot < iSlash )
			return sPath;
		return sPath.substr( 0, iDot );
	}

	bool EndsWithNoCase( const std::string &sStr, const std::string &sSuffix )
	{
		if( sStr.size() < sSuffix.size() )
			return false;
		return LauncherUtil::Lower( sStr.substr( sStr.size() - sSuffix.size() ) ) ==
			LauncherUtil::Lower( sSuffix );
	}

	bool StartsWithNoCase( const std::string &sStr, const std::string &sPrefix )
	{
		if( sStr.size() < sPrefix.size() )
			return false;
		return LauncherUtil::Lower( sStr.substr( 0, sPrefix.size() ) ) ==
			LauncherUtil::Lower( sPrefix );
	}

	std::string DirName( const std::string &sPath )
	{
		const size_t iSlash = sPath.find_last_of( '/' );
		if( iSlash == std::string::npos )
			return std::string();
		return sPath.substr( 0, iSlash );
	}
}

LauncherPacks::LauncherPacks()
	: m_sIndexUrl( kDefaultIndexUrl ), m_sSearchIndexUrl( kDefaultSearchIndexUrl ),
		m_bSearchReady( false ), m_pfnPump( NULL ), m_pUser( NULL )
{
}

void LauncherPacks::SetPump( PumpFn pfnPump, void *pUser )
{
	m_pfnPump = pfnPump;
	m_pUser = pUser;
}

bool LauncherPacks::Pump( const char *szStage, float fProgress, long long iDone, long long iTotal )
{
	if( m_pfnPump == NULL )
		return true;
	return m_pfnPump( m_pUser, szStage, fProgress, iDone, iTotal );
}

bool LauncherPacks::ProgressThunk( void *pUser, long long iDone, long long iTotal )
{
	LauncherPacks *pThis = (LauncherPacks *)pUser;
	const float fProgress = ( iTotal > 0 ) ? (float)( (double)iDone / (double)iTotal ) : -1.0f;
	return pThis->Pump( "Downloading", fProgress, iDone, iTotal );
}

bool LauncherPacks::FetchIndex( std::string &sError )
{
	return Fetch( m_sIndexUrl, "Fetching pack list", m_vPacks, sError );
}

bool LauncherPacks::FetchSearchIndex( std::string &sError )
{
	if( !Fetch( m_sSearchIndexUrl, "Fetching all packs", m_vSearchPacks, sError ) )
		return false;
	m_bSearchReady = true;
	return true;
}

bool LauncherPacks::Fetch( const std::string &sUrl, const char *szStage,
	std::vector<PackEntry> &vOut, std::string &sError )
{
	LauncherUtil::Log( "packs: fetching %s", sUrl.c_str() );
	Pump( szStage, -1.0f, 0, -1 );

	std::string sBody;
	if( !LauncherHttp::GetToString( sUrl, sBody, sError ) )
	{
		LauncherUtil::Log( "packs: index fetch failed: %s", sError.c_str() );
		return false;
	}

	/* Parsing a large catalogue takes a moment on the Vita, so keep the busy
	 * screen up while jsoncpp walks the document. */
	Pump( "Building the list", -1.0f, 0, -1 );

	Json::Value root;
	Json::Reader reader;
	if( !reader.parse( sBody, root ) )
	{
		sError = "index is not valid JSON";
		LauncherUtil::Log( "packs: %s", sError.c_str() );
		return false;
	}

	const Json::Value *pList = NULL;
	if( root.isArray() )
		pList = &root;
	else if( root.isObject() && root["packs"].isArray() )
		pList = &root["packs"];

	if( pList == NULL )
	{
		sError = "index has no \"packs\" array";
		LauncherUtil::Log( "packs: %s", sError.c_str() );
		return false;
	}

	/* The body is not needed once the document is parsed; releasing it early
	 * keeps the peak memory of the full catalogue lower. */
	std::string().swap( sBody );

	std::vector<PackEntry> vPacks;
	for( unsigned i = 0; i < (unsigned)pList->size(); ++i )
	{
		const Json::Value &e = (*pList)[i];
		if( !e.isObject() )
			continue;

		PackEntry p;
		p.sName = e.get( "name", e.get( "title", "" ) ).asString();
		p.sUrl = e.get( "url", "" ).asString();
		if( p.sUrl.empty() )
			continue;
		if( !StartsWithNoCase( p.sUrl, "http://" ) && !StartsWithNoCase( p.sUrl, "https://" ) )
		{
			LauncherUtil::Log( "packs: skipping non-http url '%s'", p.sUrl.c_str() );
			continue;
		}
		if( p.sName.empty() )
			p.sName = StripExtension( BaseName( p.sUrl ) );

		p.fSizeMB = 0.0;
		if( e["size"].isNumeric() )
			p.fSizeMB = e["size"].asDouble() / ( 1024.0 * 1024.0 );

		p.sGroup = e.get( "group", "" ).asString();
		if( p.sGroup.empty() )
			p.sGroup = StripExtension( BaseName( p.sUrl ) );
		p.sGroup = LauncherUtil::Sanitize( p.sGroup );
		p.sBanner = e.get( "banner", "" ).asString();
		p.sDate = e.get( "date", "" ).asString();
		p.iSongs = e["songs"].isNumeric() ? e["songs"].asInt() : 0;
		p.bInstalled = false;

		vPacks.push_back( p );
	}

	if( vPacks.empty() )
	{
		sError = "index lists no usable packs";
		LauncherUtil::Log( "packs: %s", sError.c_str() );
		return false;
	}

	vOut.swap( vPacks );
	RefreshInstalled();

	LauncherUtil::Log( "packs: %d packs in %s", (int)vOut.size(), sUrl.c_str() );
	return true;
}

void LauncherPacks::RefreshInstalled()
{
	for( size_t i = 0; i < m_vPacks.size(); ++i )
	{
		const std::string sDir = LauncherUtil::DataPath( "Songs/" + m_vPacks[i].sGroup );
		m_vPacks[i].bInstalled = LauncherUtil::DirExists( sDir );
	}
}

void LauncherPacks::RefreshInstalledFor( const std::vector<int> &vView, bool bAll,
	size_t iLimit )
{
	std::vector<PackEntry> &vPacks = bAll ? m_vSearchPacks : m_vPacks;
	const size_t iCount = vView.size() < iLimit ? vView.size() : iLimit;
	for( size_t k = 0; k < iCount; ++k )
	{
		const int i = vView[k];
		if( i < 0 || (size_t)i >= vPacks.size() )
			continue;
		PackEntry &p = vPacks[(size_t)i];
		p.bInstalled = LauncherUtil::DirExists( LauncherUtil::DataPath( "Songs/" + p.sGroup ) );
	}
}

bool LauncherPacks::Install( size_t iPack, std::string &sError )
{
	if( iPack >= m_vPacks.size() )
	{
		sError = "no such pack";
		return false;
	}
	return InstallEntry( m_vPacks[iPack], sError );
}

bool LauncherPacks::InstallSearch( size_t iPack, std::string &sError )
{
	if( iPack >= m_vSearchPacks.size() )
	{
		sError = "no such pack";
		return false;
	}
	return InstallEntry( m_vSearchPacks[iPack], sError );
}

bool LauncherPacks::InstallEntry( PackEntry &pack, std::string &sError )
{
	const std::string sFolder = LauncherUtil::Sanitize( pack.sGroup );
	const std::string sZip = LauncherUtil::DataPath( "Downloads/" + sFolder + ".zip" );

	LauncherUtil::Mkdirs( LauncherUtil::DataPath( "Downloads" ) );

	LauncherUtil::Log( "packs: installing '%s' from %s", pack.sName.c_str(), pack.sUrl.c_str() );
	const long long iHint = (long long)( pack.fSizeMB * 1024.0 * 1024.0 );
	if( !LauncherHttp::GetToFile( pack.sUrl, sZip, &LauncherPacks::ProgressThunk, this,
			sError, iHint ) )
	{
		LauncherUtil::Log( "packs: download failed: %s", sError.c_str() );
		return false;
	}

	Pump( "Unpacking", -1.0f, 0, -1 );
	if( !Extract( sZip, sFolder, sError ) )
	{
		LauncherUtil::Log( "packs: unpack failed: %s", sError.c_str() );
		LauncherUtil::RemoveFile( sZip );
		return false;
	}

	LauncherUtil::RemoveFile( sZip );

	/* The same archive can appear in both lists, so mark every entry with this
	 * URL as installed instead of only the one that was tapped. */
	pack.bInstalled = true;
	for( size_t i = 0; i < m_vPacks.size(); ++i )
		if( m_vPacks[i].sUrl == pack.sUrl )
			m_vPacks[i].bInstalled = true;
	for( size_t i = 0; i < m_vSearchPacks.size(); ++i )
		if( m_vSearchPacks[i].sUrl == pack.sUrl )
			m_vSearchPacks[i].bInstalled = true;

	LauncherUtil::Log( "packs: '%s' installed into Songs/%s", pack.sName.c_str(), sFolder.c_str() );
	return true;
}

bool LauncherPacks::Extract( const std::string &sZip, const std::string &sGroup, std::string &sError )
{
	mz_zip_archive zip;
	memset( &zip, 0, sizeof(zip) );
	if( !mz_zip_reader_init_file( &zip, sZip.c_str(), 0 ) )
	{
		sError = "not a readable zip archive";
		return false;
	}

	std::vector<int> viIndex;
	std::vector<std::string> vsName;
	const unsigned iCount = mz_zip_reader_get_num_files( &zip );
	for( unsigned i = 0; i < iCount; ++i )
	{
		if( mz_zip_reader_is_file_a_directory( &zip, i ) )
			continue;

		mz_zip_archive_file_stat st;
		if( !mz_zip_reader_file_stat( &zip, i, &st ) )
			continue;

		std::string sName = st.m_filename;
		if( sName.empty() || sName[0] == '/' )
			continue;
		for( size_t c = 0; c < sName.size(); ++c )
			if( sName[c] == '\\' )
				sName[c] = '/';
		if( StartsWithNoCase( sName, "__MACOSX/" ) )
			continue;
		if( sName.find( ".." ) != std::string::npos )
			continue; /* zip slip */
		if( EndsWithNoCase( sName, ".ctl" ) )
			continue;

		viIndex.push_back( (int)i );
		vsName.push_back( sName );
	}

	if( viIndex.empty() )
	{
		mz_zip_reader_end( &zip );
		sError = "archive contains no files";
		return false;
	}

	/* A pack is usually wrapped in a single top-level folder; drop it so songs
	 * do not end up one level too deep. */
	std::string sTop;
	bool bStrip = true;
	for( size_t i = 0; i < vsName.size(); ++i )
	{
		const size_t iSlash = vsName[i].find( '/' );
		if( iSlash == std::string::npos )
		{
			bStrip = false;
			break;
		}
		const std::string sThis = vsName[i].substr( 0, iSlash );
		if( i == 0 )
			sTop = sThis;
		else if( sThis != sTop )
		{
			bStrip = false;
			break;
		}
	}

	const std::string sRoot = LauncherUtil::DataPath( "Songs/" + sGroup );
	if( !LauncherUtil::Mkdirs( sRoot ) )
	{
		mz_zip_reader_end( &zip );
		sError = "cannot create Songs/" + sGroup;
		return false;
	}

	int iDone = 0;
	bool bOk = true;
	for( size_t i = 0; i < viIndex.size(); ++i )
	{
		std::string sRel = vsName[i];
		if( bStrip )
			sRel = sRel.substr( sTop.size() + 1 );
		if( sRel.empty() )
			continue;

		const std::string sOut = sRoot + "/" + sRel;
		LauncherUtil::Mkdirs( DirName( sOut ) );

		if( !mz_zip_reader_extract_to_file( &zip, (mz_uint)viIndex[i], sOut.c_str(), 0 ) )
		{
			LauncherUtil::Log( "packs: failed to extract %s", sRel.c_str() );
			continue;
		}

		++iDone;
		if( ( iDone & 15 ) == 0 )
		{
			const float f = (float)iDone / (float)viIndex.size();
			if( !Pump( "Unpacking", f, iDone, (long long)viIndex.size() ) )
			{
				bOk = false;
				sError = "cancelled";
				break;
			}
		}
	}

	mz_zip_reader_end( &zip );

	if( bOk )
	{
		Pump( "Unpacking", 1.0f, iDone, (long long)viIndex.size() );
		LauncherUtil::Log( "packs: extracted %d files", iDone );
	}
	return bOk;
}
