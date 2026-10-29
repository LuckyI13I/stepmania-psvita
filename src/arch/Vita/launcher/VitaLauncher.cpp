#include "VitaLauncher.h"
#include "LauncherPacks.h"
#include "LauncherHttp.h"
#include "LauncherUtil.h"
#include "LauncherImages.h"

#include <vita2d.h>
#include <psp2/gxm.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/apputil.h>
#include <psp2/ime_dialog.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace
{
	/* ---------------------------------------------------------------- layout */
	const int SCREEN_W = 960;
	const int SCREEN_H = 544;

	const int HAT_H = 116;          /* dark gradient strip on top */
	const int BANNER_X = 20;
	const int BANNER_Y = 26;

	const int SEARCH_X = 470;
	const int SEARCH_Y = 34;
	const int SEARCH_W = 460;
	const int SEARCH_H = 47;

	const int LIST_TOP = 120;
	const int LIST_BOTTOM = 520;
	const int BUBBLE_W = 900;
	const int BUBBLE_H = 70;
	const int BUBBLE_GAP = 10;
	const int BUBBLE_PITCH = BUBBLE_H + BUBBLE_GAP;
	const int VISIBLE_ROWS = ( LIST_BOTTOM - LIST_TOP ) / BUBBLE_PITCH;
	const float EDGE_FADE = 52.0f;

	const int THUMB_W = 96;
	const int THUMB_H = 54;

	/* Detail panel geometry (also used by the hit tests). */
	const int DETAIL_X = 70;
	const int DETAIL_Y = 72;
	const int DETAIL_W = SCREEN_W - 140;
	const int DETAIL_H = 400;
	const int DETAIL_INFO_X = DETAIL_X + 356;
	const int DETAIL_BUTTON_Y = DETAIL_Y + DETAIL_H - 92;

	/* sceAppUtilReceiveAppEvent() reports LiveArea launches with this type; the
	 * <target>psla:...<target> argument inside template.xml follows. */
	const unsigned APPUTIL_EVENT_LIVEAREA = 0x05;
	const int ALREADY_LOADED = (int)0x8002D080;

	/* Arcade palette: amber highlight on the orange bubble art, gold accents,
	 * dark brown ink for text that sits on the light search field. */
	const unsigned COL_INK = 0xFF3A1E00;
	const unsigned COL_INK_DIM = 0xFF7A5A32;
	const unsigned COL_TEXT = 0xFFFFFFFF;
	const unsigned COL_DIM = 0xFFF0DCC0;
	const unsigned COL_GOLD = 0xFFFFDD00;
	const unsigned COL_AMBER = 0xFFFF7700;
	const unsigned COL_MARKER = 0xFFFFCC00;
	const unsigned COL_OK = 0xFF7BE08A;
	const unsigned COL_ERR = 0xFFFF6A5A;
	const unsigned COL_SHADOW = 0x90000000;
	const unsigned COL_STATUS = 0xFF4A2A00;   /* dark brown status ink */

	const char *kUiDir = "app0:/ui/";

	static unsigned WithAlpha( unsigned iColor, float fAlpha )
	{
		if( fAlpha < 0.0f ) fAlpha = 0.0f;
		if( fAlpha > 1.0f ) fAlpha = 1.0f;
		const unsigned iA = (unsigned)( ( ( iColor >> 24 ) & 0xFF ) * fAlpha );
		return ( iColor & 0x00FFFFFF ) | ( iA << 24 );
	}

	static float Clampf( float f, float fMin, float fMax )
	{
		return f < fMin ? fMin : ( f > fMax ? fMax : f );
	}

	/* Rounded rectangle built from one body and two end caps. */
	static void DrawPill( float x, float y, float w, float h, unsigned int color )
	{
		const float r = h * 0.5f;
		if( w <= h || color == 0 )
			return;
		vita2d_draw_rectangle( x + r, y, w - 2.0f * r, h, color );
		vita2d_draw_fill_circle( x + r, y + r, r, color );
		vita2d_draw_fill_circle( x + w - r, y + r, r, color );
	}

	std::string ToLowerAscii( const std::string &sIn )
	{
		std::string sOut( sIn );
		for( size_t i = 0; i < sOut.size(); ++i )
		{
			const char c = sOut[i];
			if( c >= 'A' && c <= 'Z' )
				sOut[i] = (char)( c - 'A' + 'a' );
		}
		return sOut;
	}

	bool ContainsNoCase( const std::string &sHaystack, const std::string &sNeedle )
	{
		if( sNeedle.empty() )
			return true;
		return ToLowerAscii( sHaystack ).find( ToLowerAscii( sNeedle ) ) != std::string::npos;
	}

	bool ReadFileAll( const std::string &sPath, std::vector<unsigned char> &vOut )
	{
		const SceUID fd = sceIoOpen( sPath.c_str(), SCE_O_RDONLY, 0 );
		if( fd < 0 )
			return false;
		const SceOff iSize = sceIoLseek( fd, 0, SCE_SEEK_END );
		sceIoLseek( fd, 0, SCE_SEEK_SET );
		if( iSize <= 0 || iSize > 4 * 1024 * 1024 )
		{
			sceIoClose( fd );
			return false;
		}
		vOut.resize( (size_t)iSize );
		int iRead = 0;
		while( iRead < (int)iSize )
		{
			const int iChunk = sceIoRead( fd, &vOut[(size_t)iRead], (unsigned)( iSize - iRead ) );
			if( iChunk <= 0 )
				break;
			iRead += iChunk;
		}
		sceIoClose( fd );
		vOut.resize( (size_t)iRead );
		return iRead > 0;
	}

	/* Stable cache name for a banner URL. */
	std::string UrlHash( const std::string &sUrl )
	{
		unsigned iHash = 2166136261u;
		for( size_t i = 0; i < sUrl.size(); ++i )
		{
			iHash ^= (unsigned char)sUrl[i];
			iHash *= 16777619u;
		}
		char sBuf[16];
		snprintf( sBuf, sizeof(sBuf), "%08x", iHash );
		return std::string( sBuf );
	}

	/* ------------------------------------------------------- banner downloader */
	/* Cover art is fetched on a worker thread: a TLS handshake takes long
	 * enough to be visible, and the browser must keep its inertia. The thread
	 * only writes the bytes to ux0; texture creation stays on the main thread
	 * where the GXM context lives. */
	class BannerLoader
	{
	public:
		BannerLoader() : m_bStop( false ), m_hThread( -1 ), m_hSema( -1 ), m_hMutex( -1 )
		{
		}

		~BannerLoader()
		{
			Stop();
		}

		bool Start()
		{
			if( m_hThread >= 0 )
				return true;
			m_hSema = sceKernelCreateSema( "smbanner_sema", 0, 0, 32, NULL );
			m_hMutex = sceKernelCreateMutex( "smbanner_mutex", 0, 0, NULL );
			if( m_hSema < 0 || m_hMutex < 0 )
				return false;
			m_hThread = sceKernelCreateThread( "smbanner", &BannerLoader::ThreadEntry,
				0x10000100, 0x80000, 0, 0, NULL );
			if( m_hThread < 0 )
				return false;
			return sceKernelStartThread( m_hThread, sizeof(this), this ) >= 0;
		}

		void Stop()
		{
			if( m_hThread < 0 )
				return;
			sceKernelLockMutex( m_hMutex, 1, NULL );
			m_bStop = true;
			sceKernelUnlockMutex( m_hMutex, 1 );
			sceKernelSignalSema( m_hSema, 1 );
			sceKernelWaitThreadEnd( m_hThread, NULL, NULL );
			sceKernelDeleteThread( m_hThread );
			sceKernelDeleteSema( m_hSema );
			sceKernelDeleteMutex( m_hMutex );
			m_hThread = -1;
		}

		void Request( const std::string &sUrl, const std::string &sFile )
		{
			if( m_hThread < 0 )
				return;
			Job job;
			job.sUrl = sUrl;
			job.sFile = sFile;
			sceKernelLockMutex( m_hMutex, 1, NULL );
			m_vQueue.push_back( job );
			sceKernelUnlockMutex( m_hMutex, 1 );
			sceKernelSignalSema( m_hSema, 1 );
		}

		struct Job
		{
			std::string sUrl;
			std::string sFile;
		};

		/* Main thread: returns finished downloads (at most iMax per call). */
		void PopReady( std::vector<Job> &vDone, std::vector<std::string> &vFailed, int iMax )
		{
			if( m_hThread < 0 )
				return;
			sceKernelLockMutex( m_hMutex, 1, NULL );
			for( int i = 0; i < iMax && !m_vReady.empty(); ++i )
			{
				vDone.push_back( m_vReady.front() );
				m_vReady.erase( m_vReady.begin() );
			}
			for( size_t i = 0; i < m_vFailed.size(); ++i )
				vFailed.push_back( m_vFailed[i] );
			m_vFailed.clear();
			sceKernelUnlockMutex( m_hMutex, 1 );
		}

	private:
		static int ThreadEntry( SceSize iArgs, void *pArg )
		{
			BannerLoader *pThis = (BannerLoader *)pArg;
			for( ;; )
			{
				sceKernelWaitSema( pThis->m_hSema, 1, NULL );

				Job job;
				bool bGot = false;
				sceKernelLockMutex( pThis->m_hMutex, 1, NULL );
				const bool bStop = pThis->m_bStop;
				if( !pThis->m_vQueue.empty() )
				{
					job = pThis->m_vQueue.front();
					pThis->m_vQueue.erase( pThis->m_vQueue.begin() );
					bGot = true;
				}
				sceKernelUnlockMutex( pThis->m_hMutex, 1 );
				if( bStop )
					break;
				if( !bGot )
					continue;

				std::string sError;
				const bool bOk = LauncherHttp::GetToFile( job.sUrl, job.sFile, NULL, NULL, sError, -1 );
				if( !bOk )
					LauncherUtil::Log( "banner: %s failed: %s", job.sUrl.c_str(), sError.c_str() );

				sceKernelLockMutex( pThis->m_hMutex, 1, NULL );
				if( bOk )
					pThis->m_vReady.push_back( job );
				else
					pThis->m_vFailed.push_back( job.sUrl );
				sceKernelUnlockMutex( pThis->m_hMutex, 1 );
			}
			return 0;
		}

		bool m_bStop;
		SceUID m_hThread;
		SceUID m_hSema;
		SceUID m_hMutex;
		std::vector<Job> m_vQueue;
		std::vector<Job> m_vReady;
		std::vector<std::string> m_vFailed;
	};

	/* --------------------------------------------------------------- interface */
	struct BusyCtx
	{
		class Ui *pUi;
		bool bCancel;
		const std::string *pQuery;
		SceUInt64 uiStart;
	};

	class Ui
	{
	public:
		Ui() : m_pFont( NULL ), m_pBg( NULL ), m_pHat( NULL ), m_pBanner( NULL ),
			m_pSearch( NULL ), m_pSearchIcon( NULL ), m_pBubble( NULL ),
			m_pLoading( NULL ), m_pDownload( NULL ), m_uiPrev( 0 ),
			m_iHoldDir( 0 ), m_iHoldFrames( 0 ), m_bTouchDown( false ),
			m_bTouchWasDown( false ), m_fLastTouchX( 0.0f ), m_fLastTouchY( 0.0f ),
			m_iTick( 0 ), m_bBannerCache( false )
		{
			memset( &m_Pad, 0, sizeof(m_Pad) );
			memset( &m_Touch, 0, sizeof(m_Touch) );
		}

		~Ui()
		{
			m_loader.Stop();
			for( std::map<std::string, BannerSlot>::iterator it = m_banners.begin();
				it != m_banners.end(); ++it )
				vita2d_free_texture( it->second.pTex );
			m_banners.clear();

			vita2d_texture *pTex[] = { m_pBg, m_pHat, m_pBanner, m_pSearch,
				m_pSearchIcon, m_pBubble, m_pLoading, m_pDownload };
			for( size_t i = 0; i < sizeof(pTex) / sizeof(pTex[0]); ++i )
				if( pTex[i] != NULL )
					vita2d_free_texture( pTex[i] );

			if( m_pFont != NULL )
				vita2d_free_pgf( m_pFont );
			vita2d_fini();
			sceGxmTerminate();
			sceSysmoduleUnloadModule( SCE_SYSMODULE_PGF );
		}

		bool Init()
		{
			sceSysmoduleLoadModule( SCE_SYSMODULE_PGF );
			if( vita2d_init() != 1 )
				return false;
			vita2d_set_clear_color( 0xFF000000 );

			m_pFont = vita2d_load_default_pgf();
			if( m_pFont == NULL )
				return false;

			sceCtrlSetSamplingMode( SCE_CTRL_MODE_ANALOG );
			sceTouchSetSamplingState( SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START );

			m_pBg = LoadUi( "background.png" );
			m_pHat = LoadUi( "hat.png" );
			m_pBanner = LoadUi( "banner.png" );
			m_pSearch = LoadUi( "search-bar.png" );
			m_pSearchIcon = LoadUi( "search-icon.png" );
			m_pBubble = LoadUi( "pack-bubble.png" );
			m_pLoading = LoadUi( "loading.png" );
			m_pDownload = LoadUi( "download-button.png" );

			LauncherUtil::Mkdirs( LauncherUtil::DataPath( "Banners" ) );

			std::string sError;
			LauncherHttp::EnsureInit( sError );
			m_bBannerCache = m_loader.Start();
			if( !m_bBannerCache )
				LauncherUtil::Log( "ui: banner thread not started" );
			return true;
		}

		void Poll()
		{
			m_uiPrev = m_Pad.buttons;
			sceCtrlPeekBufferPositive( 0, &m_Pad, 1 );
			m_bTouchWasDown = m_bTouchDown;
			m_bTouchDown = sceTouchPeek( SCE_TOUCH_PORT_FRONT, &m_Touch, 1 ) > 0 && m_Touch.reportNum > 0;
			++m_iTick;
		}

		bool Pressed( unsigned iBtn ) const
		{
			return ( m_Pad.buttons & iBtn ) && !( m_uiPrev & iBtn );
		}

		bool Repeat( unsigned iBtn )
		{
			if( !( m_Pad.buttons & iBtn ) )
			{
				if( m_iHoldDir == (int)iBtn )
				{
					m_iHoldDir = 0;
					m_iHoldFrames = 0;
				}
				return false;
			}
			if( m_uiPrev & iBtn )
			{
				if( m_iHoldDir != (int)iBtn )
				{
					m_iHoldDir = (int)iBtn;
					m_iHoldFrames = 0;
				}
				++m_iHoldFrames;
				if( m_iHoldFrames < 18 )
					return false;
				const int iPhase = m_iHoldFrames - 18;
				const int iInterval = iPhase < 50 ? 4 : 1;
				return ( iPhase % iInterval ) == 0;
			}
			m_iHoldDir = (int)iBtn;
			m_iHoldFrames = 0;
			return true;
		}

		bool TouchDown() const { return m_bTouchDown; }
		bool TouchJustPressed() const { return m_bTouchDown && !m_bTouchWasDown; }
		bool TouchJustReleased() const { return !m_bTouchDown && m_bTouchWasDown; }
		float TouchX() const
		{
			return m_bTouchDown ? m_Touch.report[0].x * 0.5f : m_fLastTouchX;
		}
		float TouchY() const
		{
			return m_bTouchDown ? m_Touch.report[0].y * 0.5f : m_fLastTouchY;
		}

		void RememberTouch()
		{
			if( m_bTouchDown )
			{
				m_fLastTouchX = m_Touch.report[0].x * 0.5f;
				m_fLastTouchY = m_Touch.report[0].y * 0.5f;
			}
		}

		void Begin()
		{
			vita2d_start_drawing();
			vita2d_clear_screen();
		}

		void End()
		{
			vita2d_end_drawing();
			vita2d_common_dialog_update();
			vita2d_swap_buffers();
		}

		/* ------------------------------------------------------------- text */
		void Text( int x, int y, float fScale, unsigned iColor, const char *szFmt, ... )
		{
			char szBuf[512];
			va_list args;
			va_start( args, szFmt );
			vsnprintf( szBuf, sizeof(szBuf), szFmt, args );
			va_end( args );
			vita2d_pgf_draw_text( m_pFont, x, y, iColor, fScale, szBuf );
		}

		void TextShadow( int x, int y, float fScale, unsigned iColor, const char *szFmt, ... )
		{
			char szBuf[512];
			va_list args;
			va_start( args, szFmt );
			vsnprintf( szBuf, sizeof(szBuf), szFmt, args );
			va_end( args );
			vita2d_pgf_draw_text( m_pFont, x + 1, y + 2, WithAlpha( COL_SHADOW, 0.55f ), fScale, szBuf );
			vita2d_pgf_draw_text( m_pFont, x, y, iColor, fScale, szBuf );
		}

		int TextWidth( float fScale, const char *szText )
		{
			return vita2d_pgf_text_width( m_pFont, fScale, szText );
		}

		/* Shortens a string with an ellipsis until it fits iMaxWidth pixels. */
		std::string FitText( const std::string &sText, float fScale, int iMaxWidth )
		{
			if( TextWidth( fScale, sText.c_str() ) <= iMaxWidth )
				return sText;
			std::string sCut = sText;
			while( !sCut.empty() )
			{
				sCut.erase( sCut.size() - 1 );
				const std::string sTry = sCut + "...";
				if( TextWidth( fScale, sTry.c_str() ) <= iMaxWidth )
					return sTry;
			}
			return std::string();
		}

		/* ---------------------------------------------------------- assets */
		void DrawBackground( float fAlpha )
		{
			if( m_pBg != NULL )
				vita2d_draw_texture_tint( m_pBg, 0, 0, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
				vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H, WithAlpha( 0xFF201008, fAlpha ) );
		}

		void DrawChrome( const std::string &sQuery, float fAlpha, bool bSearchActive )
		{
			if( m_pHat != NULL )
				vita2d_draw_texture_tint( m_pHat, 0, 0, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
				vita2d_draw_rectangle( 0, 0, SCREEN_W, HAT_H, WithAlpha( 0xE0402408, fAlpha ) );

			if( m_pBanner != NULL )
				vita2d_draw_texture_tint( m_pBanner, BANNER_X, BANNER_Y,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );

			/* Search field: the bar art is light, so the ink is dark brown. */
			if( m_pSearch != NULL )
				vita2d_draw_texture_tint( m_pSearch, SEARCH_X, SEARCH_Y,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
			{
				DrawPill( SEARCH_X, SEARCH_Y, SEARCH_W, SEARCH_H,
					WithAlpha( 0xFFF6EEE0, fAlpha ) );
			}

			const int iTextLeft = SEARCH_X + 18;
			const int iTextRight = SEARCH_X + SEARCH_W - 60;
			const int iBaseline = SEARCH_Y + 32;
			const float fScale = 0.72f;
			if( sQuery.empty() )
			{
				vita2d_pgf_draw_text( m_pFont, iTextLeft, iBaseline,
					WithAlpha( COL_DIM, fAlpha * ( bSearchActive ? 0.55f : 0.8f ) ),
					fScale, "Search packs..." );
			}
			else
			{
				std::string sShown = sQuery;
				while( !sShown.empty() &&
					TextWidth( fScale, sShown.c_str() ) > iTextRight - iTextLeft )
					sShown.erase( 0, 1 );
				vita2d_pgf_draw_text( m_pFont, iTextLeft, iBaseline,
					WithAlpha( COL_TEXT, fAlpha ), fScale, sShown.c_str() );
				vita2d_pgf_draw_text( m_pFont, iTextLeft + TextWidth( fScale, sShown.c_str() ) + 2,
					iBaseline, WithAlpha( COL_GOLD, fAlpha * 0.8f ), fScale, "|" );
			}

			if( m_pSearchIcon != NULL )
			{
				vita2d_draw_texture_tint( m_pSearchIcon,
					SEARCH_X + SEARCH_W - 35 - 14, SEARCH_Y + 7,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );
			}
			else
			{
				vita2d_draw_fill_circle( SEARCH_X + SEARCH_W - 32, SEARCH_Y + 20, 9,
					WithAlpha( COL_INK, fAlpha ) );
			}
		}

		/* ---------------------------------------------------------- banners */
		vita2d_texture *GetBanner( const PackEntry &p )
		{
			std::map<std::string, BannerSlot>::iterator it = m_banners.find( p.sUrl );
			if( it == m_banners.end() )
				return NULL;
			it->second.uiUsed = m_iTick;
			return it->second.pTex;
		}

		bool BannerKnown( const PackEntry &p ) const
		{
			return m_banners.find( p.sUrl ) != m_banners.end() ||
				m_failed.find( p.sUrl ) != m_failed.end() ||
				m_requested.find( p.sUrl ) != m_requested.end();
		}

		void RequestBanner( const PackEntry &p )
		{
			if( !m_bBannerCache || p.sBanner.empty() || BannerKnown( p ) )
				return;
			const std::string sFile = CacheFile( p.sUrl );
			m_requested[p.sUrl] = true;

			/* Already downloaded on an earlier run: decode straight away. */
			std::vector<unsigned char> vData;
			if( ReadFileAll( sFile, vData ) )
			{
				StoreBanner( p.sUrl, vData );
				return;
			}
			m_loader.Request( p.sBanner, sFile );
		}

		void PumpBanners()
		{
			std::vector<BannerLoader::Job> vDone;
			std::vector<std::string> vFailed;
			m_loader.PopReady( vDone, vFailed, 2 );
			for( size_t i = 0; i < vFailed.size(); ++i )
			{
				m_failed[vFailed[i]] = true;
				m_requested.erase( vFailed[i] );
			}
			for( size_t i = 0; i < vDone.size(); ++i )
			{
				std::vector<unsigned char> vData;
				const bool bRead = ReadFileAll( vDone[i].sFile, vData );
				if( bRead )
					StoreBanner( vDone[i].sUrl, vData );
				else
					m_failed[vDone[i].sUrl] = true;
				LauncherUtil::RemoveFile( vDone[i].sFile );
			}
		}

		/* Thumbnail inside a bubble; cover-cropped so the aspect stays 16:9. */
		void DrawThumb( const PackEntry &p, float x, float y, float fAlpha )
		{
			vita2d_texture *pTex = GetBanner( p );
			if( pTex == NULL )
			{
				DrawPill( x, y, (float)THUMB_W, (float)THUMB_H, WithAlpha( 0x66000000, fAlpha ) );
				DrawPill( x + 2, y + 2, THUMB_W - 4.0f, THUMB_H - 4.0f,
					WithAlpha( 0x30FFDD88, fAlpha ) );
				return;
			}

			const float fTexW = (float)vita2d_texture_get_width( pTex );
			const float fTexH = (float)vita2d_texture_get_height( pTex );
			if( fTexW <= 0.0f || fTexH <= 0.0f )
				return;

			const float fDstAspect = (float)THUMB_W / (float)THUMB_H;
			float fSrcW = fTexW, fSrcH = fTexH, fSrcX = 0.0f, fSrcY = 0.0f;
			if( fTexW / fTexH > fDstAspect )
			{
				fSrcW = fTexH * fDstAspect;
				fSrcX = ( fTexW - fSrcW ) * 0.5f;
			}
			else
			{
				fSrcH = fTexW / fDstAspect;
				fSrcY = ( fTexH - fSrcH ) * 0.5f;
			}
			vita2d_draw_texture_tint_part_scale( pTex, x, y, fSrcX, fSrcY, fSrcW, fSrcH,
				(float)THUMB_W / fSrcW, (float)THUMB_H / fSrcH, WithAlpha( 0xFFFFFFFF, fAlpha ) );
		}

		/* Large cover for the detail page; letterboxed inside the box. */
		void DrawBannerFit( const PackEntry &p, float x, float y, float w, float h, float fAlpha )
		{
			vita2d_texture *pTex = GetBanner( p );
			if( pTex == NULL )
			{
				DrawPill( x, y, w, h, WithAlpha( 0x55000000, fAlpha ) );
				DrawPill( x + 3, y + 3, w - 6, h - 6, WithAlpha( 0x22FFDD88, fAlpha ) );
				TextShadow( (int)( x + w * 0.5f - 30 ), (int)( y + h * 0.5f ), 0.7f,
					WithAlpha( COL_DIM, fAlpha ), "no art" );
				return;
			}
			const float fTexW = (float)vita2d_texture_get_width( pTex );
			const float fTexH = (float)vita2d_texture_get_height( pTex );
			if( fTexW <= 0.0f || fTexH <= 0.0f )
				return;
			const float fScale = fminf( w / fTexW, h / fTexH );
			const float fDrawW = fTexW * fScale;
			const float fDrawH = fTexH * fScale;
			vita2d_draw_texture_tint_scale( pTex, x + ( w - fDrawW ) * 0.5f,
				y + ( h - fDrawH ) * 0.5f, fScale, fScale, WithAlpha( 0xFFFFFFFF, fAlpha ) );
		}

		/* ---------------------------------------------------------- spinner */
		void Spinner( float fCx, float fCy, float fSize, float fAlpha )
		{
			if( m_pLoading == NULL )
			{
				vita2d_draw_fill_circle( fCx, fCy, fSize * 0.5f, WithAlpha( COL_GOLD, fAlpha ) );
				return;
			}
			const float fTexW = (float)vita2d_texture_get_width( m_pLoading );
			const float fScale = fSize / ( fTexW > 0.0f ? fTexW : 80.0f );
			const float fAngle = (float)( sceKernelGetProcessTimeWide() % 1200000 ) *
				( 6.2831853f / 1200000.0f );
			vita2d_draw_texture_tint_scale_rotate_hotspot( m_pLoading, fCx, fCy, fScale, fScale,
				fAngle, fTexW * 0.5f, (float)vita2d_texture_get_height( m_pLoading ) * 0.5f,
				WithAlpha( 0xFFFFFFFF, fAlpha ) );
		}

		/* ------------------------------------------------------------- list */
		void SceneList( const std::vector<PackEntry> &vPacks, const std::vector<int> &vView,
			int iSel, float fScroll, const std::string &sQuery, bool bLoading,
			const char *szStage, float fProgress )
		{
			DrawBackground( 1.0f );
			DrawChrome( sQuery, 1.0f, false );

			if( bLoading || vView.empty() )
			{
				const float fCenter = ( LIST_TOP + LIST_BOTTOM ) * 0.5f;
				if( bLoading )
				{
					Spinner( SCREEN_W * 0.5f, fCenter - 14.0f, 72.0f, 1.0f );
					if( szStage != NULL )
						TextShadow( (int)( SCREEN_W * 0.5f - TextWidth( 0.7f, szStage ) * 0.5f ),
							(int)fCenter + 56, 0.7f, COL_DIM, "%s", szStage );
					if( fProgress > 0.0f )
						TextShadow( (int)( SCREEN_W * 0.5f - 22 ), (int)fCenter + 82, 0.65f,
							COL_DIM, "%d%%", (int)( fProgress * 100.0f ) );
				}
				else
				{
					TextShadow( 60, 200, 0.95f, COL_DIM,
						sQuery.empty() ? "No packs to show." : "Nothing matches \"%s\".",
						sQuery.c_str() );
					TextShadow( 60, 238, 0.7f, COL_DIM,
						"Press TRIANGLE to search, SQUARE to clear." );
				}
			}

			if( !bLoading )
			{
				vita2d_set_clip_rectangle( 0, LIST_TOP, SCREEN_W, LIST_BOTTOM );
				const int iFirst = (int)floorf( fScroll / (float)BUBBLE_PITCH );
				for( int iRow = iFirst; iRow <= iFirst + VISIBLE_ROWS + 1; ++iRow )
				{
					if( iRow < 0 || iRow >= (int)vView.size() )
						continue;
					const int iPack = vView[iRow];
					if( iPack < 0 || iPack >= (int)vPacks.size() )
						continue;
					const float fY = LIST_TOP + iRow * BUBBLE_PITCH - fScroll;
					if( fY > LIST_BOTTOM || fY + BUBBLE_H < LIST_TOP )
						continue;

					/* Soft fade so bubbles dissolve at both edges instead of
					 * being cut off by the header/footer. */
					const float fMid = fY + BUBBLE_H * 0.5f;
					float fAlpha = fminf( ( fMid - LIST_TOP ) / EDGE_FADE,
						( LIST_BOTTOM - fMid ) / EDGE_FADE );
					fAlpha = Clampf( fAlpha, 0.0f, 1.0f );
					if( fAlpha <= 0.02f )
						continue;

					DrawBubble( vPacks[iPack], 30.0f, fY, iRow == iSel, fAlpha );
				}
				vita2d_set_clip_rectangle( 0, 0, SCREEN_W, SCREEN_H );
			}

			if( !bLoading && !vView.empty() )
			{
				char sPos[64];
				snprintf( sPos, sizeof(sPos), "%d / %d", iSel + 1, (int)vView.size() );
				TextShadow( 34, LIST_BOTTOM + 20, 0.62f, WithAlpha( COL_DIM, 0.85f ), "%s", sPos );
				const char *szHelp = "X  details      TRIANGLE  search      O  close";
				TextShadow( SCREEN_W - 34 - TextWidth( 0.62f, szHelp ), LIST_BOTTOM + 20, 0.62f,
					WithAlpha( COL_DIM, 0.85f ), "%s", szHelp );
			}
			else if( bLoading )
			{
				TextShadow( SCREEN_W - 34 - 190, LIST_BOTTOM + 20, 0.62f,
					WithAlpha( COL_DIM, 0.85f ), "X  cancel" );
			}
		}

		void DrawBubble( const PackEntry &p, float x, float y, bool bSelected, float fAlpha )
		{
			if( m_pBubble != NULL )
			{
				vita2d_draw_texture_tint_scale( m_pBubble, x, y,
					(float)BUBBLE_W / (float)vita2d_texture_get_width( m_pBubble ),
					(float)BUBBLE_H / (float)vita2d_texture_get_height( m_pBubble ),
					WithAlpha( 0xFFFFFFFF, fAlpha ) );
			}
			else
			{
				DrawPill( x, y, BUBBLE_W, BUBBLE_H, WithAlpha( 0xD0C08050, fAlpha ) );
			}

			if( bSelected )
			{
				/* Warm amber wash over the art instead of a flat replacement so
				 * the gloss and outline survive. */
				DrawPill( x + 2, y + 2, BUBBLE_W - 4.0f, BUBBLE_H - 4.0f,
					WithAlpha( 0x9AFF7700, fAlpha ) );
				vita2d_draw_rectangle( x + 6, y + 10, 6, BUBBLE_H - 20,
					WithAlpha( COL_MARKER, fAlpha ) );
			}

			DrawThumb( p, x + 12, y + 8, fAlpha );

			const int iTextX = (int)x + 122;
			const std::string sName = FitText( p.sName, 0.82f, BUBBLE_W - 360 );
			vita2d_pgf_draw_text( m_pFont, iTextX + 1, (int)y + 33,
				WithAlpha( COL_SHADOW, 0.5f * fAlpha ), 0.82f, sName.c_str() );
			vita2d_pgf_draw_text( m_pFont, iTextX, (int)y + 32,
				WithAlpha( COL_TEXT, fAlpha ), 0.82f, sName.c_str() );

			char sMeta[128];
			if( p.iSongs > 0 && !p.sDate.empty() )
				snprintf( sMeta, sizeof(sMeta), "%d songs   %s", p.iSongs, p.sDate.c_str() );
			else if( p.iSongs > 0 )
				snprintf( sMeta, sizeof(sMeta), "%d songs", p.iSongs );
			else
				snprintf( sMeta, sizeof(sMeta), "%s", p.sDate.c_str() );
			vita2d_pgf_draw_text( m_pFont, iTextX, (int)y + 55,
				WithAlpha( COL_DIM, fAlpha ), 0.62f, sMeta );

			if( p.fSizeMB > 0.0 )
			{
				char sSize[32];
				if( p.fSizeMB >= 1024.0 )
					snprintf( sSize, sizeof(sSize), "%.1f GB", p.fSizeMB / 1024.0 );
				else
					snprintf( sSize, sizeof(sSize), "%.0f MB", p.fSizeMB );
				const int iW = TextWidth( 0.78f, sSize );
				vita2d_pgf_draw_text( m_pFont, (int)( x + BUBBLE_W - 26 - iW ), (int)y + 42,
					WithAlpha( COL_TEXT, fAlpha ), 0.78f, sSize );
			}

			if( p.bInstalled )
				vita2d_pgf_draw_text( m_pFont, (int)( x + BUBBLE_W - 26 - TextWidth( 0.58f, "installed" ) ),
					(int)y + 60, WithAlpha( 0xFF9BE8A6, fAlpha ), 0.58f, "installed" );
		}

		/* ----------------------------------------------------------- detail */
		void SceneDetail( const PackEntry &p, float fAnim )
		{
			const float fA = Clampf( fAnim, 0.0f, 1.0f );
			const float fSlide = ( 1.0f - fA ) * 26.0f;
			const float fAlpha = fA;

			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H, WithAlpha( 0xB0000000, fAlpha ) );

			const float fPanelX = (float)DETAIL_X;
			const float fPanelY = (float)DETAIL_Y + fSlide;
			const float fPanelW = (float)DETAIL_W;
			const float fPanelH = (float)DETAIL_H;

			DrawPill( fPanelX, fPanelY, fPanelW, fPanelH, WithAlpha( 0xF0201008, fAlpha ) );
			DrawPill( fPanelX + 2, fPanelY + 2, fPanelW - 4, fPanelH - 4,
				WithAlpha( 0xF0361C0A, fAlpha ) );
			vita2d_draw_rectangle( fPanelX + 40, fPanelY + 2, fPanelW - 80, 3,
				WithAlpha( COL_GOLD, fAlpha ) );

			DrawBannerFit( p, fPanelX + 26, fPanelY + 26, 300.0f, 300.0f, fAlpha );

			const int iInfoX = (int)fPanelX + 356;
			std::string sName = FitText( p.sName, 0.92f, (int)fPanelW - 390 );
			TextShadow( iInfoX, (int)fPanelY + 62, 0.92f, WithAlpha( COL_TEXT, fAlpha ),
				"%s", sName.c_str() );
			TextShadow( iInfoX, (int)fPanelY + 96, 0.66f, WithAlpha( COL_GOLD, fAlpha ),
				"%s", p.sGroup.c_str() );

			int iLine = (int)fPanelY + 138;
			if( p.iSongs > 0 )
			{
				TextShadow( iInfoX, iLine, 0.7f, WithAlpha( COL_DIM, fAlpha ),
					"%d songs", p.iSongs );
				iLine += 28;
			}
			if( !p.sDate.empty() )
			{
				TextShadow( iInfoX, iLine, 0.7f, WithAlpha( COL_DIM, fAlpha ),
					"released %s", p.sDate.c_str() );
				iLine += 28;
			}
			if( p.fSizeMB > 0.0 )
			{
				if( p.fSizeMB >= 1024.0 )
					TextShadow( iInfoX, iLine, 0.7f, WithAlpha( COL_DIM, fAlpha ),
						"%.2f GB", p.fSizeMB / 1024.0 );
				else
					TextShadow( iInfoX, iLine, 0.7f, WithAlpha( COL_DIM, fAlpha ),
						"%.1f MB", p.fSizeMB );
				iLine += 28;
			}

			const float fButtonY = fPanelY + fPanelH - 92.0f;
			if( p.bInstalled )
			{
				DrawPill( (float)iInfoX, fButtonY, 291.0f, 52.0f, WithAlpha( 0xC0204020, fAlpha ) );
				TextShadow( iInfoX + 62, (int)fButtonY + 34, 0.85f, WithAlpha( COL_OK, fAlpha ),
					"INSTALLED" );
			}
			else if( m_pDownload != NULL )
			{
				const float fScale = 1.0f;
				vita2d_draw_texture_tint_scale( m_pDownload, (float)iInfoX, fButtonY,
					fScale, fScale, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			}
			else
			{
				DrawPill( (float)iInfoX, fButtonY, 291.0f, 52.0f, WithAlpha( COL_AMBER, fAlpha ) );
				TextShadow( iInfoX + 92, (int)fButtonY + 34, 0.8f, WithAlpha( COL_TEXT, fAlpha ),
					"DOWNLOAD" );
			}

			TextShadow( iInfoX, (int)fPanelY + fPanelH - 18, 0.6f, WithAlpha( COL_DIM, 0.85f ),
				"O  back      X  download" );
		}

		/* ------------------------------------------------------------- busy */
		void SceneBusy( const char *szStage, float fProgress, long long iDone, long long iTotal,
			float fFade = 1.0f )
		{
			DrawBackground( 1.0f );
			DrawChrome( std::string(), 1.0f, true );

			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H, WithAlpha( 0x99000000, fFade ) );
			Spinner( SCREEN_W * 0.5f, SCREEN_H * 0.5f - 10.0f, 96.0f * ( 0.85f + 0.15f * fFade ), fFade );

			/* Status ink is dark brown by request, so it sits on a light plate
			 * in the lower-left corner instead of on the dim itself. */
			char sStatus[192];
			if( iTotal > 0 )
			{
				snprintf( sStatus, sizeof(sStatus), "%s...  %s / %s  (%d%%)",
					szStage != NULL ? szStage : "Working",
					LauncherUtil::FormatBytes( iDone ).c_str(),
					LauncherUtil::FormatBytes( iTotal ).c_str(),
					(int)( fProgress > 0.0f ? fProgress * 100.0f : 0.0f ) );
			}
			else
			{
				snprintf( sStatus, sizeof(sStatus), "%s...", szStage != NULL ? szStage : "Working" );
			}
			const int iBarW = SCREEN_W - 120;
			const int iBarX = 60;
			const int iBarY = SCREEN_H - 84;
			vita2d_draw_rectangle( iBarX, iBarY, iBarW, 16, 0xB0201208 );
			if( fProgress > 0.0f )
				vita2d_draw_rectangle( iBarX, iBarY, (int)( iBarW * Clampf( fProgress, 0.0f, 1.0f ) ),
					16, COL_AMBER );

			const int iTextW = TextWidth( 0.72f, sStatus );
			DrawPill( 24, SCREEN_H - 46, (float)iTextW + 28, 30, 0xD0F0E2C8 );
			vita2d_pgf_draw_text( m_pFont, 38, SCREEN_H - 24, COL_STATUS, 0.72f, sStatus );
		}

		void SceneMessage( const char *szTitle, const char *szBody, bool bError )
		{
			DrawBackground( 1.0f );
			DrawChrome( std::string(), 1.0f, true );
			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H, 0xB0000000 );

			DrawPill( 90, 170, SCREEN_W - 180, 190, 0xF0201008 );
			DrawPill( 92, 172, SCREEN_W - 184, 186, 0xF0361C0A );
			vita2d_draw_rectangle( 130, 172, SCREEN_W - 260, 3, COL_GOLD );

			TextShadow( 130, 226, 0.95f, bError ? COL_ERR : COL_OK, "%s", szTitle );
			vita2d_pgf_draw_text( m_pFont, 130, 274, COL_DIM, 0.68f,
				szBody != NULL ? szBody : "" );
			vita2d_pgf_draw_text( m_pFont, 130, 322, COL_DIM, 0.62f, "X  continue" );
		}

		void Message( const char *szTitle, const char *szBody, bool bError )
		{
			for( ;; )
			{
				Poll();
				if( Pressed( SCE_CTRL_CROSS ) || Pressed( SCE_CTRL_CIRCLE ) )
					return;
				Begin();
				SceneMessage( szTitle, szBody, bError );
				End();
				sceDisplayWaitVblankStart();
			}
		}

		/* ------------------------------------------------------------- ime */
		bool AskText( const std::string &sInitial, std::string &sOut )
		{
			const int kMaxLen = 64;
			SceWChar16 wInitial[kMaxLen + 1];
			SceWChar16 wBuffer[kMaxLen + 1];
			memset( wInitial, 0, sizeof(wInitial) );
			memset( wBuffer, 0, sizeof(wBuffer) );
			for( int i = 0; i < kMaxLen && i < (int)sInitial.size(); ++i )
				wInitial[i] = (SceWChar16)(unsigned char)sInitial[i];

			sceSysmoduleLoadModule( SCE_SYSMODULE_IME );

			SceImeDialogParam param;
			sceImeDialogParamInit( &param );
			param.type = SCE_IME_TYPE_BASIC_LATIN;
			param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
			param.languagesForced = SCE_TRUE;
			param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
			param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
			param.title = (const SceWChar16 *)u"Search packs";
			param.maxTextLength = kMaxLen;
			param.initialText = wInitial;
			param.inputTextBuffer = wBuffer;

			const int iRet = sceImeDialogInit( &param );
			if( iRet < 0 )
			{
				LauncherUtil::Log( "ui: sceImeDialogInit failed (0x%08X)", (unsigned)iRet );
				return false;
			}

			bool bConfirmed = false;
			for( ;; )
			{
				const SceCommonDialogStatus eStatus = sceImeDialogGetStatus();
				if( eStatus == SCE_COMMON_DIALOG_STATUS_FINISHED )
				{
					SceImeDialogResult res;
					memset( &res, 0, sizeof(res) );
					sceImeDialogGetResult( &res );
					bConfirmed = ( res.result == SCE_COMMON_DIALOG_RESULT_OK );
					break;
				}
				if( eStatus != SCE_COMMON_DIALOG_STATUS_RUNNING )
					break;
				/* The system keyboard is composited by the common dialog, which
				 * needs a normal frame (start/end drawing) every vblank. */
				Begin();
				DrawBackground( 1.0f );
				DrawChrome( std::string(), 1.0f, true );
				End();
				sceDisplayWaitVblankStart();
			}

			sceImeDialogTerm();
			if( !bConfirmed )
				return false;

			sOut.clear();
			for( int i = 0; i < kMaxLen && wBuffer[i] != 0; ++i )
			{
				const unsigned c = (unsigned)wBuffer[i];
				if( c < 128 )
					sOut.push_back( (char)c );
			}
			return true;
		}

	private:
		struct BannerSlot
		{
			vita2d_texture *pTex;
			unsigned uiUsed;
		};

		vita2d_texture *LoadUi( const char *szName )
		{
			std::string sPath = std::string( kUiDir ) + szName;
			return LauncherImages::LoadPngFile( sPath.c_str() );
		}

		std::string CacheFile( const std::string &sUrl ) const
		{
			return LauncherUtil::DataPath( "Banners/" + UrlHash( sUrl ) + ".img" );
		}

		void StoreBanner( const std::string &sUrl, const std::vector<unsigned char> &vData )
		{
			m_requested.erase( sUrl );
			if( vData.empty() )
			{
				m_failed[sUrl] = true;
				return;
			}
			vita2d_texture *pTex = LauncherImages::Decode( &vData[0], vData.size(), 512 );
			if( pTex == NULL )
			{
				m_failed[sUrl] = true;
				LauncherUtil::Log( "banner: could not decode art for %s", sUrl.c_str() );
				return;
			}

			/* Keep the VRAM footprint bounded; drop the least recently drawn. */
			while( m_banners.size() >= 10 )
			{
				std::map<std::string, BannerSlot>::iterator itWorst = m_banners.begin();
				for( std::map<std::string, BannerSlot>::iterator it = m_banners.begin();
					it != m_banners.end(); ++it )
					if( it->second.uiUsed < itWorst->second.uiUsed )
						itWorst = it;
				vita2d_free_texture( itWorst->second.pTex );
				m_banners.erase( itWorst );
			}

			BannerSlot slot;
			slot.pTex = pTex;
			slot.uiUsed = m_iTick;
			m_banners[sUrl] = slot;
		}

		vita2d_pgf *m_pFont;
		vita2d_texture *m_pBg;
		vita2d_texture *m_pHat;
		vita2d_texture *m_pBanner;
		vita2d_texture *m_pSearch;
		vita2d_texture *m_pSearchIcon;
		vita2d_texture *m_pBubble;
		vita2d_texture *m_pLoading;
		vita2d_texture *m_pDownload;

		SceCtrlData m_Pad;
		SceTouchData m_Touch;
		unsigned m_uiPrev;
		int m_iHoldDir;
		int m_iHoldFrames;
		bool m_bTouchDown;
		bool m_bTouchWasDown;
		float m_fLastTouchX;
		float m_fLastTouchY;
		unsigned m_iTick;

		BannerLoader m_loader;
		bool m_bBannerCache;
		std::map<std::string, BannerSlot> m_banners;
		std::map<std::string, bool> m_failed;
		std::map<std::string, bool> m_requested;
	};

	/* -------------------------------------------------------------- pumping */
	/* Index download: the list screen stays on screen (empty, with the spinner
	 * in the results area) instead of switching to a separate loading screen. */
	bool ListPump( void *pUser, const char *szStage, float fProgress,
		long long iDone, long long iTotal )
	{
		BusyCtx *pCtx = (BusyCtx *)pUser;
		static SceUInt64 s_uiLastDraw = 0;
		const SceUInt64 uiNow = sceKernelGetProcessTimeWide();
		if( s_uiLastDraw == 0 || uiNow - s_uiLastDraw >= 50 * 1000 )
		{
			std::vector<PackEntry> vEmpty;
			std::vector<int> vView;
			pCtx->pUi->Begin();
			pCtx->pUi->SceneList( vEmpty, vView, 0, 0.0f,
				pCtx->pQuery != NULL ? *pCtx->pQuery : std::string(), true, szStage, fProgress );
			pCtx->pUi->End();
			s_uiLastDraw = uiNow;
		}
		pCtx->pUi->Poll();
		pCtx->pUi->RememberTouch();
		if( pCtx->pUi->Pressed( SCE_CTRL_CROSS ) )
			pCtx->bCancel = true;
		return !pCtx->bCancel;
	}

	/* Download / unpack: dimmed overlay with the spinning StepMania arrow. */
	bool InstallPump( void *pUser, const char *szStage, float fProgress,
		long long iDone, long long iTotal )
	{
		BusyCtx *pCtx = (BusyCtx *)pUser;
		static SceUInt64 s_uiLastDraw = 0;
		const SceUInt64 uiNow = sceKernelGetProcessTimeWide();
		if( s_uiLastDraw == 0 || uiNow - s_uiLastDraw >= 50 * 1000 )
		{
			const float fFade = pCtx->uiStart != 0 && uiNow > pCtx->uiStart
				? Clampf( (float)( uiNow - pCtx->uiStart ) / 180000.0f, 0.0f, 1.0f )
				: 1.0f;
			pCtx->pUi->Begin();
			pCtx->pUi->SceneBusy( szStage, fProgress, iDone, iTotal, fFade );
			pCtx->pUi->End();
			s_uiLastDraw = uiNow;
		}
		pCtx->pUi->Poll();
		if( pCtx->pUi->Pressed( SCE_CTRL_CROSS ) )
			pCtx->bCancel = true;
		return !pCtx->bCancel;
	}

	bool FetchIndex( Ui &ui, LauncherPacks &packs, const std::string &sQuery,
		const char *szStage, std::string &sError )
	{
		BusyCtx ctx;
		ctx.pUi = &ui;
		ctx.bCancel = false;
		ctx.pQuery = &sQuery;
		packs.SetPump( &ListPump, &ctx );
		const bool bOk = packs.FetchIndex( sError );
		packs.SetPump( NULL, NULL );
		return bOk;
	}

	bool FetchAllPacks( Ui &ui, LauncherPacks &packs, const std::string &sQuery, std::string &sError )
	{
		BusyCtx ctx;
		ctx.pUi = &ui;
		ctx.bCancel = false;
		ctx.pQuery = &sQuery;
		packs.SetPump( &ListPump, &ctx );
		const bool bOk = packs.FetchSearchIndex( sError );
		packs.SetPump( NULL, NULL );
		return bOk;
	}

	bool InstallPack( Ui &ui, LauncherPacks &packs, size_t iPack, bool bAll, std::string &sError )
	{
		BusyCtx ctx;
		ctx.pUi = &ui;
		ctx.bCancel = false;
		ctx.pQuery = NULL;
		ctx.uiStart = sceKernelGetProcessTimeWide();
		packs.SetPump( &InstallPump, &ctx );
		const bool bOk = bAll ? packs.InstallSearch( iPack, sError ) : packs.Install( iPack, sError );
		packs.SetPump( NULL, NULL );
		if( !bOk && ctx.bCancel )
			sError = "cancelled";
		return bOk;
	}

	void BuildView( const std::vector<PackEntry> &vPacks, const std::string &sFilter,
		std::vector<int> &vView )
	{
		vView.clear();
		for( size_t i = 0; i < vPacks.size(); ++i )
		{
			if( ContainsNoCase( vPacks[i].sName, sFilter ) )
				vView.push_back( (int)i );
		}
	}

	/* ------------------------------------------------------------ main loop */
	void RunList( Ui &ui, LauncherPacks &packs )
	{
		std::string sError;
		std::string sQuery;
		std::vector<int> vView;
		bool bAll = false;
		int iSel = 0;
		float fScroll = 0.0f;
		float fScrollTarget = 0.0f;
		float fVelocity = 0.0f;
		bool bDragging = false;
		bool bDragMoved = false;
		float fDragLastY = 0.0f;
		float fDragStartY = 0.0f;
		int iDetail = -1;
		float fDetailAnim = 0.0f;
		bool bDetailClosing = false;
		int iIdleFrames = 0;
		float fAfterFade = 0.0f;

		if( !FetchIndex( ui, packs, sQuery, "Loading packs", sError ) )
		{
			ui.Message( "Could not load the pack list", sError.c_str(), true );
		}
		BuildView( packs.GetPacks(), sQuery, vView );
		packs.RefreshInstalledFor( vView, false );

		for( ;; )
		{
			ui.Poll();
			ui.RememberTouch();
			ui.PumpBanners();

			const std::vector<PackEntry> &vPacks = bAll ? packs.GetSearchPacks() : packs.GetPacks();
			const int iCount = (int)vView.size();
			const int iPage = VISIBLE_ROWS;
			const float fListH = (float)( LIST_BOTTOM - LIST_TOP );
			bool bPadMoved = false;
			bool bTapped = false;
			int iTapIndex = -1;

			/* -------------------------------------------------------- touch */
			/* Taps are tracked independently of the scroll drag so the detail
			 * page (which never drags) still reacts to the download button
			 * and to taps outside the panel. */
			if( ui.TouchJustPressed() )
			{
				const float fTy = ui.TouchY();
				bDragging = iDetail < 0 && fTy >= LIST_TOP && fTy < LIST_BOTTOM;
				bDragMoved = false;
				fDragLastY = fTy;
				fDragStartY = fTy;
				fVelocity = 0.0f;
			}
			if( ui.TouchDown() )
			{
				const float fTy = ui.TouchY();
				if( bDragging )
				{
					const float fDy = fTy - fDragLastY;
					if( fabsf( fDy ) > 0.2f )
					{
						fScroll -= fDy;
						fScrollTarget = fScroll;
						fVelocity = -fDy;
					}
					iIdleFrames = 0;
				}
				if( fabsf( fTy - fDragStartY ) > 8.0f )
					bDragMoved = true;
				fDragLastY = fTy;
			}
			if( ui.TouchJustReleased() )
			{
				if( !bDragMoved )
				{
					bTapped = true;
					if( bDragging )
						iTapIndex = (int)floorf( ( ui.TouchY() - LIST_TOP + fScroll ) / BUBBLE_PITCH );
				}
				bDragging = false;
			}

			/* --------------------------------------------------- inertia */
			if( !bDragging )
			{
				if( fabsf( fVelocity ) > 0.2f )
				{
					fScroll += fVelocity;
					fVelocity *= 0.90f;
					fScrollTarget = fScroll;
				}
				else
				{
					fVelocity = 0.0f;
					if( fabsf( fScrollTarget - fScroll ) > 0.5f )
					{
						fScroll += ( fScrollTarget - fScroll ) * 0.28f;
						iIdleFrames = 0;
					}
					else
						fScroll = fScrollTarget;
				}
			}

			/* ------------------------------------------------------ pad */
			if( iCount > 0 && iDetail < 0 )
			{
				if( ui.Repeat( SCE_CTRL_UP ) && iSel > 0 )
				{
					--iSel;
					bPadMoved = true;
				}
				if( ui.Repeat( SCE_CTRL_DOWN ) && iSel + 1 < iCount )
				{
					++iSel;
					bPadMoved = true;
				}
				if( ui.Pressed( SCE_CTRL_LTRIGGER ) )
				{
					iSel = iSel - iPage > 0 ? iSel - iPage : 0;
					bPadMoved = true;
				}
				if( ui.Pressed( SCE_CTRL_RTRIGGER ) )
				{
					iSel = iSel + iPage < iCount ? iSel + iPage : iCount - 1;
					bPadMoved = true;
				}
			}

			if( iSel < 0 )
				iSel = 0;
			if( iCount > 0 && iSel > iCount - 1 )
				iSel = iCount - 1;

			if( bPadMoved )
			{
				iIdleFrames = 0;
				const float fSelTop = iSel * (float)BUBBLE_PITCH;
				const float fSelBottom = fSelTop + BUBBLE_H;
				if( fSelTop < fScrollTarget )
					fScrollTarget = fSelTop;
				if( fSelBottom > fScrollTarget + fListH )
					fScrollTarget = fSelBottom - fListH;
			}

			/* ------------------------------------------------- clamp */
			const float fMaxScroll = fmaxf( 0.0f, ( iCount * BUBBLE_PITCH - BUBBLE_GAP ) - fListH );
			if( fScroll < 0.0f )
			{
				fScroll = 0.0f;
				fVelocity = 0.0f;
			}
			if( fScroll > fMaxScroll )
			{
				fScroll = fMaxScroll;
				fVelocity = 0.0f;
			}
			fScrollTarget = Clampf( fScrollTarget, 0.0f, fMaxScroll );

			/* ---------------------------------------------------- search */
			bool bSearchTapped = bTapped &&
				ui.TouchX() >= SEARCH_X && ui.TouchX() <= SEARCH_X + SEARCH_W &&
				ui.TouchY() >= SEARCH_Y && ui.TouchY() <= SEARCH_Y + SEARCH_H;

			if( ( ui.Pressed( SCE_CTRL_TRIANGLE ) || bSearchTapped ) && iDetail < 0 )
			{
				std::string sText;
				if( ui.AskText( sQuery, sText ) )
				{
					bool bApply = true;
					if( !sText.empty() && !packs.HasSearchIndex() )
					{
						std::string sFetchError;
						if( FetchAllPacks( ui, packs, sText, sFetchError ) )
							bAll = true;
						else
						{
							if( sFetchError != "cancelled" )
								ui.Message( "Could not load the full catalogue", sFetchError.c_str(), true );
							bApply = false;
						}
					}
					if( bApply )
					{
						sQuery = sText;
						bAll = !sQuery.empty() && packs.HasSearchIndex();
						BuildView( bAll ? packs.GetSearchPacks() : packs.GetPacks(), sQuery, vView );
						packs.RefreshInstalledFor( vView, bAll );
						iSel = 0;
						fScroll = 0.0f;
						fScrollTarget = 0.0f;
						fVelocity = 0.0f;
						iIdleFrames = 0;
						LauncherUtil::Log( "packs: query \"%s\" -> %d matches (%s)",
							sQuery.c_str(), (int)vView.size(), bAll ? "all" : "recommended" );
					}
				}
				continue;
			}

			if( ui.Pressed( SCE_CTRL_SQUARE ) && ( !sQuery.empty() || bAll ) && iDetail < 0 )
			{
				sQuery.clear();
				bAll = false;
				BuildView( packs.GetPacks(), sQuery, vView );
				packs.RefreshInstalledFor( vView, false );
				iSel = 0;
				fScroll = 0.0f;
				fScrollTarget = 0.0f;
				continue;
			}

			if( ui.Pressed( SCE_CTRL_SELECT ) && iDetail < 0 )
			{
				const bool bOk = bAll ? FetchAllPacks( ui, packs, sQuery, sError )
					: FetchIndex( ui, packs, sQuery, "Refreshing packs", sError );
				if( !bOk )
					ui.Message( "Could not refresh the list", sError.c_str(), true );
				BuildView( bAll ? packs.GetSearchPacks() : packs.GetPacks(), sQuery, vView );
				packs.RefreshInstalledFor( vView, bAll );
				iSel = 0;
				fScroll = 0.0f;
				fScrollTarget = 0.0f;
				continue;
			}

			/* ----------------------------------------------------- exit/sel */
			if( iDetail < 0 && ui.Pressed( SCE_CTRL_CIRCLE ) )
				return;

			/* --------------------------------------------------- detail */
			if( iDetail < 0 )
			{
				const bool bActivate = ui.Pressed( SCE_CTRL_CROSS ) ||
					( bTapped && iTapIndex >= 0 && iTapIndex < iCount && !bSearchTapped );
				if( bActivate && iCount > 0 && !bSearchTapped )
				{
					if( bTapped && iTapIndex >= 0 && iTapIndex < iCount )
						iSel = iTapIndex;
					iDetail = iSel;
					fDetailAnim = 0.0f;
					bDetailClosing = false;
					ui.RequestBanner( vPacks[(size_t)vView[(size_t)iDetail]] );
					continue;
				}
			}
			else
			{
				const PackEntry &p = vPacks[(size_t)vView[(size_t)iDetail]];
				const bool bButtonHit = bTapped &&
					ui.TouchX() >= DETAIL_INFO_X && ui.TouchX() <= DETAIL_INFO_X + 291 &&
					ui.TouchY() >= DETAIL_BUTTON_Y && ui.TouchY() <= DETAIL_BUTTON_Y + 52;
				const bool bOutside = bTapped &&
					( ui.TouchX() < DETAIL_X || ui.TouchX() > DETAIL_X + DETAIL_W ||
					  ui.TouchY() < DETAIL_Y || ui.TouchY() > DETAIL_Y + DETAIL_H );

				if( bDetailClosing )
				{
					fDetailAnim -= 0.14f;
					if( fDetailAnim <= 0.0f )
					{
						iDetail = -1;
						bDetailClosing = false;
						fDetailAnim = 0.0f;
					}
					if( iDetail < 0 )
						continue;
				}
				else
				{
					fDetailAnim = Clampf( fDetailAnim + 0.16f, 0.0f, 1.0f );

					if( ui.Pressed( SCE_CTRL_CIRCLE ) || bOutside )
						bDetailClosing = true;
					else if( !p.bInstalled && ( ui.Pressed( SCE_CTRL_CROSS ) || bButtonHit ) )
					{
						if( InstallPack( ui, packs, (size_t)vView[(size_t)iDetail], bAll, sError ) )
							packs.RefreshInstalledFor( vView, bAll );
						else if( !sError.empty() && sError != "cancelled" )
							ui.Message( "Install failed", sError.c_str(), true );
						fAfterFade = 0.65f;
						bDetailClosing = true;
					}
					if( bDetailClosing )
						continue;
				}
			}

			/* -------------------------------------------------- prefetch */
			if( iDetail < 0 && !bDragging && fVelocity == 0.0f )
				++iIdleFrames;
			else
				iIdleFrames = 0;

			if( iIdleFrames > 24 && !vView.empty() )
			{
				const int iFirst = (int)floorf( fScroll / (float)BUBBLE_PITCH );
				for( int i = iFirst; i < iFirst + VISIBLE_ROWS + 1 && i < iCount; ++i )
					ui.RequestBanner( vPacks[(size_t)vView[(size_t)i]] );
			}

			/* ------------------------------------------------------ draw */
			ui.Begin();
			ui.SceneList( vPacks, vView, iSel, fScroll, sQuery, false, NULL, -1.0f );
			if( iDetail >= 0 )
				ui.SceneDetail( vPacks[(size_t)vView[(size_t)iDetail]], fDetailAnim );
			if( fAfterFade > 0.0f )
			{
				vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
					WithAlpha( 0xFF000000, fAfterFade ) );
				fAfterFade -= 0.09f;
				if( fAfterFade < 0.0f )
					fAfterFade = 0.0f;
			}
			ui.End();
			sceDisplayWaitVblankStart();
		}
	}
}

/* ------------------------------------------------------------- entry points */
bool VitaLauncher::WantsPackBrowser( int argc, char **argv )
{
	for( int i = 1; i < argc; ++i )
	{
		if( argv[i] != NULL && strstr( argv[i], "packs" ) != NULL )
			return true;
	}

	LauncherUtil::Mkdirs( LauncherUtil::kDataRoot );

	int iRet = sceSysmoduleLoadModule( SCE_SYSMODULE_APPUTIL );
	if( iRet < 0 && iRet != ALREADY_LOADED )
	{
		LauncherUtil::Log( "livearea: sceSysmoduleLoadModule(APPUTIL) failed (0x%08X)", (unsigned)iRet );
		return false;
	}

	SceAppUtilInitParam initParam;
	SceAppUtilBootParam bootParam;
	memset( &initParam, 0, sizeof(initParam) );
	memset( &bootParam, 0, sizeof(bootParam) );
	iRet = sceAppUtilInit( &initParam, &bootParam );
	const bool bAppUtil = iRet >= 0;
	if( !bAppUtil )
		LauncherUtil::Log( "livearea: sceAppUtilInit failed (0x%08X)", (unsigned)iRet );

	bool bPacks = false;
	SceAppUtilAppEventParam ev;
	memset( &ev, 0, sizeof(ev) );
	iRet = sceAppUtilReceiveAppEvent( &ev );
	if( iRet >= 0 )
	{
		LauncherUtil::Log( "livearea: app event type 0x%08X", (unsigned)ev.type );
		if( ev.type == APPUTIL_EVENT_LIVEAREA )
		{
			char sArgs[2048];
			memset( sArgs, 0, sizeof(sArgs) );
			iRet = sceAppUtilAppEventParseLiveArea( &ev, sArgs );
			if( iRet >= 0 )
			{
				LauncherUtil::Log( "livearea: args \"%s\"", sArgs );
				if( strstr( sArgs, "packs" ) != NULL )
					bPacks = true;
			}
			else
				LauncherUtil::Log( "livearea: parse failed (0x%08X)", (unsigned)iRet );
		}
	}
	else
		LauncherUtil::Log( "livearea: sceAppUtilReceiveAppEvent failed (0x%08X)", (unsigned)iRet );

	if( bAppUtil )
		sceAppUtilShutdown();
	return bPacks;
}

void VitaLauncher::RunPackBrowser()
{
	LauncherUtil::Mkdirs( LauncherUtil::kDataRoot );
	LauncherUtil::Mkdirs( LauncherUtil::DataPath( "Songs" ) );
	LauncherUtil::Mkdirs( LauncherUtil::DataPath( "Downloads" ) );
	LauncherUtil::Log( "downloader: start" );

	{
		Ui ui;
		if( !ui.Init() )
			LauncherUtil::Log( "downloader: vita2d init failed" );
		else
		{
			LauncherPacks packs;
			RunList( ui, packs );
		}
	}

	LauncherHttp::Shutdown();
	LauncherUtil::Log( "downloader: done, booting the game" );
}
