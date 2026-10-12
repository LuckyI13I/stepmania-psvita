#include "global.h"
#include "LoadingWindow_Vita.h"
#include "RageUtil.h"
#if defined(VITA)
#include "arch/Vita/VitaBootSplash.h"
#endif

LoadingWindow_Vita::LoadingWindow_Vita()
	: m_progress(0), m_totalWork(0)
{
	m_progress = 0;
	m_totalWork = 0;
	m_indeterminate = false;
}

void LoadingWindow_Vita::SetText( RString sText )
{
#if defined(VITA)
	/* SongManager sends "phase\nGroup\nSong": show the last line. */
	size_t pos = sText.find_last_of( '\n' );
	RString last = (pos == RString::npos) ? sText : sText.substr( pos + 1 );
	Trim( last );
	VitaBootSplash::SetSubLine( last.c_str() );
#endif
}

void LoadingWindow_Vita::SetProgress( const int progress )
{
	m_progress = progress;
#if defined(VITA)
	VitaBootSplash::SetProgress( m_progress, m_totalWork );
#endif
}

void LoadingWindow_Vita::SetTotalWork( const int totalWork )
{
	m_totalWork = totalWork;
#if defined(VITA)
	VitaBootSplash::SetProgress( m_progress, m_totalWork );
#endif
}

void LoadingWindow_Vita::SetIndeterminate( bool indeterminate )
{
	m_indeterminate = indeterminate;
#if defined(VITA)
	if( indeterminate )
		VitaBootSplash::SetProgress( 0, 0 );
#endif
}
