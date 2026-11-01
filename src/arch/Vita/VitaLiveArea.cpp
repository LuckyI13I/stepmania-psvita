#include "global.h"
#include "VitaLiveArea.h"

#ifdef VITA

#include "StageStats.h"
#include "PlayerStageStats.h"
#include "Song.h"
#include "Grade.h"
#include "PlayerNumber.h"
#include "RageLog.h"
#include "RageUtil.h"

#include <psp2/io/fcntl.h>
#include <psp2/sysmodule.h>

/* sceLiveArea* lives in libSceLiveArea_stub.a. This VitaSDK does not ship
 * psp2/livearea.h, so declare the one entry point we use (SceInt32 -> int,
 * enum SceLiveAreaTargetType -> int; both are 32-bit on ARM). */
extern "C" {
int sceLiveAreaUpdateFrameSync( const char *format_version, const char *frame_xml,
	int frame_xml_length, const char *contents_path, int target_type );
}

#define VITA_LIVEAREA_CONTENTS   "app0:sce_sys/livearea/contents"
#define VITA_LIVEAREA_FORMAT     "01.00"
#define VITA_LIVEAREA_MAX        8
#define VITA_LIVEAREA_PATH       "ux0:data/stepmania-5.0/Save/LiveAreaRecent.txt"
#define VITA_LIVEAREA_ROW        "row.png"
#define VITA_LIVEAREA_TITLE_W    18
#define VITA_LIVEAREA_ARTIST_W   12
#define VITA_LIVEAREA_ROW_W      40

static RString XmlEscape( const RString &sIn )
{
	RString sOut;
	for( size_t i = 0; i < sIn.size(); ++i )
	{
		switch( sIn[i] )
		{
			case '&':  sOut += "&amp;";  break;
			case '<':  sOut += "&lt;";   break;
			case '>':  sOut += "&gt;";   break;
			case '"':  sOut += "&quot;"; break;
			case '\'': sOut += "&apos;"; break;
			default:   sOut += sIn[i];   break;
		}
	}
	return sOut;
}

/* Pad sIn with trailing spaces (>= iW) or clip it to iW chars. Used to line
 * the LiveArea text up into columns (the frame font is proportional, so this
 * is an approximation). */
static RString FitLeft( const RString &sIn, size_t iW )
{
	RString sOut = sIn;
	if( sOut.size() > iW )
		sOut = sOut.substr( 0, iW );
	while( sOut.size() < iW )
		sOut += ' ';
	return sOut;
}

static RString FitRight( const RString &sIn, size_t iW )
{
	RString sOut = sIn;
	if( sOut.size() > iW )
		sOut = sOut.substr( 0, iW );
	while( sOut.size() < iW )
		sOut = " " + sOut;
	return sOut;
}

/* Splits "Title\tArtist\tGrade\tScore" into its fields (missing fields are
 * left empty). */
static void SplitEntry( const RString &sLine, RString vOut[4] )
{
	for( int i = 0; i < 4; ++i )
		vOut[i] = "";
	int iField = 0;
	for( size_t i = 0; i < sLine.size(); ++i )
	{
		const char c = sLine[i];
		if( c == '\t' )
		{
			if( iField < 3 )
				++iField;
		}
		else
			vOut[iField] += c;
	}
}

static vector<RString> ReadHistory()
{
	vector<RString> vOut;

	SceUID fd = sceIoOpen( VITA_LIVEAREA_PATH, SCE_O_RDONLY, 0 );
	if( fd < 0 )
		return vOut;

	char buf[4096];
	int iRead = sceIoRead( fd, buf, (SceSize)sizeof(buf) - 1 );
	sceIoClose( fd );
	if( iRead <= 0 )
		return vOut;
	buf[iRead] = '\0';

	RString sCur;
	for( int i = 0; i <= iRead; ++i )
	{
		const char c = buf[i];
		if( c == '\n' || c == '\0' )
		{
			if( !sCur.empty() )
				vOut.push_back( sCur );
			sCur = "";
			if( c == '\0' )
				break;
		}
		else if( c != '\r' )
			sCur += c;
	}
	return vOut;
}

static void WriteHistory( const vector<RString> &vLines )
{
	SceUID fd = sceIoOpen( VITA_LIVEAREA_PATH,
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777 );
	if( fd < 0 )
		return;

	for( size_t i = 0; i < vLines.size(); ++i )
	{
		const RString sLine = vLines[i] + "\n";
		sceIoWrite( fd, sLine.c_str(), (SceSize)sLine.size() );
	}
	sceIoClose( fd );
}

/* The "browser" style exposes frame1..frame8 as the window-list rows on the
 * left (472x48 each) and keeps the gate on the right, so we render the recent
 * songs as rows that mimic the browser's window list. */
static void PushRow( int iRow, bool bHasEntry, const RString &sLine )
{
	RString sXml;
	sXml += ssprintf( "<frame id=\"frame%d\" multi=\"o\" autoflip=\"0\">\n", iRow );
	sXml += "<liveitem>\n";

	if( bHasEntry )
	{
		RString vFields[4];
		SplitEntry( sLine, vFields );

		RString sText = ssprintf( "%u. ", (unsigned)iRow );
		sText += FitLeft( vFields[0], VITA_LIVEAREA_TITLE_W );
		if( !vFields[1].empty() )
			sText += " - " + FitLeft( vFields[1], VITA_LIVEAREA_ARTIST_W );
		sText = FitLeft( sText, VITA_LIVEAREA_ROW_W );

		const RString sGrade = FitRight(
			vFields[2].empty() ? RString("?") : vFields[2], 4 );

		sXml += "<image align=\"center\" origin=\"frame\" valign=\"center\">"
		        VITA_LIVEAREA_ROW "</image>\n";
		sXml += "<text origin=\"frame\" align=\"left\" valign=\"center\" "
		        "text-valign=\"center\" line-align=\"left\" margin-left=\"14\" "
		        "ellipsis=\"on\" word-wrap=\"off\" line-break=\"off\">\n";
		sXml += "<str color=\"#ffffff\" size=\"16\" shadow=\"on\">"
		        + XmlEscape( sText ) + "</str>\n";
		sXml += "<str color=\"#ffd24d\" size=\"16\" bold=\"on\" shadow=\"on\">"
		        + XmlEscape( sGrade ) + "</str>\n";
		sXml += "</text>\n";
	}

	sXml += "</liveitem>\n</frame>\n";

	/* SCE_LIVEAREA_TARGET_TYPE_NORMAL == 0. SceShell applies the update
	 * asynchronously; the return value only says whether it was accepted. */
	int iRet = sceLiveAreaUpdateFrameSync( VITA_LIVEAREA_FORMAT, sXml.c_str(),
		(int)sXml.size(), VITA_LIVEAREA_CONTENTS, 0 );
	LOG->Trace( "VitaLiveArea: frame%d -> 0x%08x (%d bytes)",
		iRow, (unsigned)iRet, (int)sXml.size() );
}

static void PushFrame( const vector<RString> &vLines )
{
	static bool s_bModuleLoaded = false;
	if( !s_bModuleLoaded )
	{
		sceSysmoduleLoadModule( SCE_SYSMODULE_LIVEAREA );
		s_bModuleLoaded = true;
	}

	for( int i = 0; i < VITA_LIVEAREA_MAX; ++i )
	{
		const bool bHasEntry = i < (int)vLines.size();
		PushRow( i + 1, bHasEntry, bHasEntry ? vLines[i] : RString("") );
	}
}

void VitaLiveAreaRecordStage( const StageStats *pStageStats )
{
	if( pStageStats == NULL || pStageStats->m_vpPlayedSongs.empty() )
		return;

	Song *pSong = pStageStats->m_vpPlayedSongs[0];
	if( pSong == NULL )
		return;

	const PlayerStageStats &pss = pStageStats->m_player[PLAYER_1];

	const RString sEntry = pSong->GetDisplayFullTitle() + "\t"
		+ pSong->GetDisplayArtist() + "\t"
		+ GradeToString( pss.GetGrade() ) + "\t"
		+ ssprintf( "%u", pss.m_iScore );

	vector<RString> vLines = ReadHistory();
	vLines.insert( vLines.begin(), sEntry );
	while( (int)vLines.size() > VITA_LIVEAREA_MAX )
		vLines.pop_back();

	WriteHistory( vLines );
	PushFrame( vLines );

	LOG->Trace( "VitaLiveArea: recorded \"%s\" [%s]",
		pSong->GetDisplayFullTitle().c_str(), GradeToString( pss.GetGrade() ).c_str() );
}

#endif /* VITA */
