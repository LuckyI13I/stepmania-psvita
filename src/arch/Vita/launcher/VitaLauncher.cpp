#include "VitaLauncher.h"
#include "LauncherPacks.h"
#include "LauncherHttp.h"
#include "LauncherUtil.h"
#include "LauncherImages.h"
#include "UiFont.h"

#include <vita2d.h>
#include <psp2/gxm.h>
#include <psp2/sysmodule.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/apputil.h>
#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
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

	/* Detail screen geometry: the pack opens straight onto the blurred,
	 * darkened backdrop. No panel, no frame: title, cover, facts and the
	 * download button only. */
	const int DETAIL_PAD = 48;
	const int DETAIL_TITLE_Y = 96;
	const int DETAIL_GROUP_Y = 126;

	const int DETAIL_COVER_X = DETAIL_PAD;
	const int DETAIL_COVER_Y = 150;
	const int DETAIL_COVER_W = 540;
	const int DETAIL_COVER_H = 304;

	const int DETAIL_INFO_X = 621;
	const int DETAIL_FACT_Y = 196;
	const int DETAIL_FACT_STEP = 72;
	const int DETAIL_FACT_VALUE_DY = 30;

	const int DETAIL_BTN_W = 291;
	const int DETAIL_BTN_H = 52;
	const int DETAIL_BTN_X = DETAIL_INFO_X;
	const int DETAIL_BTN_Y = 400;
	const int DETAIL_HINT_Y = 506;

	/* Navigation badges (Cross/Circle/Triangle/Square). The art is optional:
	 * when the files are missing the hints fall back to the button names. */
	const int HINT_ICON = 24;
	const int HINT_ICON_GAP = 7;
	const int HINT_ITEM_GAP = 24;

	/* sceAppUtilReceiveAppEvent() reports LiveArea launches with this type; the
	 * <target>psla:...<target> argument inside template.xml follows. */
	const unsigned APPUTIL_EVENT_LIVEAREA = 0x05;
	const int ALREADY_LOADED = (int)0x8002D080;

	/* Arcade palette: warm gold/amber, dark brown ink for text that sits on the
	 * light search field. vita2d takes colours as 0xAABBGGRR, so everything is
	 * built through this helper instead of writing 0xAARRGGBB by hand. */
	static unsigned Rgba( unsigned r, unsigned g, unsigned b, unsigned a = 0xFF )
	{
		return ( ( a & 0xFF ) << 24 ) | ( ( b & 0xFF ) << 16 ) | ( ( g & 0xFF ) << 8 ) | ( r & 0xFF );
	}

	const unsigned COL_INK = Rgba( 0x3A, 0x1E, 0x00 );
	const unsigned COL_TEXT = Rgba( 0xFF, 0xFF, 0xFF );
	const unsigned COL_GOLD = Rgba( 0xFF, 0xDD, 0x00 );
	const unsigned COL_AMBER = Rgba( 0xFF, 0x77, 0x00 );
	const unsigned COL_OK = Rgba( 0x8C, 0xE8, 0x96 );
	const unsigned COL_ERR = Rgba( 0xFF, 0x6A, 0x5A );
	const unsigned COL_SHADOW = Rgba( 0x00, 0x00, 0x00, 0x90 );
	const unsigned COL_CARD = Rgba( 0x2E, 0x15, 0x05 );
	const unsigned COL_CARD_HI = Rgba( 0x4E, 0x28, 0x0A );
	const unsigned COL_CREAM = Rgba( 0xF7, 0xE3, 0xB4 );

	const char *kUiDir = "app0:/ui/";
	const char *kFontPath = "app0:/fonts/Ubuntu-R.ttf";
	const char *kFontBoldPath = "app0:/fonts/Ubuntu-B.ttf";

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

	/* Rounded rectangle built from two bodies and four corner caps. */
	static void DrawRoundRect( float x, float y, float w, float h, float r, unsigned int color )
	{
		if( w <= 0.0f || h <= 0.0f || color == 0 )
			return;
		const float fMax = ( w < h ? w : h ) * 0.5f;
		if( r > fMax ) r = fMax;
		if( r < 0.5f )
		{
			vita2d_draw_rectangle( x, y, w, h, color );
			return;
		}
		vita2d_draw_rectangle( x + r, y, w - 2.0f * r, h, color );
		vita2d_draw_rectangle( x, y + r, r, h - 2.0f * r, color );
		vita2d_draw_rectangle( x + w - r, y + r, r, h - 2.0f * r, color );
		vita2d_draw_fill_circle( x + r, y + r, r, color );
		vita2d_draw_fill_circle( x + w - r, y + r, r, color );
		vita2d_draw_fill_circle( x + r, y + h - r, r, color );
		vita2d_draw_fill_circle( x + w - r, y + h - r, r, color );
	}

	/* The bubble art is already a capsule, so pills stay fully rounded. */
	static void DrawPill( float x, float y, float w, float h, unsigned int color )
	{
		DrawRoundRect( x, y, w, h, h * 0.5f, color );
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

	/* A cached file counts only when it actually holds bytes; an interrupted
	 * run can leave a zero-length entry behind. */
	bool FileHasData( const std::string &sPath )
	{
		const SceUID fd = sceIoOpen( sPath.c_str(), SCE_O_RDONLY, 0 );
		if( fd < 0 )
			return false;
		const SceOff iSize = sceIoLseek( fd, 0, SCE_SEEK_END );
		sceIoClose( fd );
		return iSize > 0;
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
				0x10000100, 0x100000, 0, 0, NULL );
			if( m_hThread < 0 )
				return false;

			/* sceKernelStartThread copies arglen bytes onto the new stack and
			 * hands the entry point a pointer to that copy, so the argument
			 * has to be the pointer value itself, not the object it points
			 * at. Passing "this" made the worker read its members from
			 * uninitialised stack memory and die on the first semaphore
			 * call, which is what killed the pack browser at startup. */
			BannerLoader *pSelf = this;
			if( sceKernelStartThread( m_hThread, sizeof(pSelf), &pSelf ) < 0 )
				return false;
			LauncherUtil::Log( "banner: thread started" );
			return true;
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

		/* A cover that is downloaded and decoded but not yet uploaded to a
		 * texture: the pixels are ready, the GXM upload waits for the main
		 * thread. */
		struct Result
		{
			std::string sUrl;
			LauncherImages::RawImage img;
		};

		/* Main thread: returns finished covers (at most iMax per call). Each
		 * result already holds decoded pixels, so the caller only uploads. */
		void PopReady( std::vector<Result> &vDone, std::vector<std::string> &vFailed, int iMax )
		{
			if( m_hThread < 0 )
				return;
			sceKernelLockMutex( m_hMutex, 1, NULL );
			for( int i = 0; i < iMax && !m_vReady.empty(); ++i )
			{
				vDone.push_back( std::move( m_vReady.front() ) );
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
			/* pArg points at the copy of the argument block; the block holds
			 * the BannerLoader pointer. */
			BannerLoader *pThis = pArg != NULL ? *(BannerLoader **)pArg : NULL;
			if( pThis == NULL )
				return 0;
			for( ;; )
			{
				Job job;
				bool bGot = false;
				bool bStop = false;
				sceKernelLockMutex( pThis->m_hMutex, 1, NULL );
				bStop = pThis->m_bStop;
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
				{
					/* Only sleep when there is nothing to do; the list
					 * prefetches a whole screenful at once and the semaphore
					 * saturates at 32, which used to strand queued covers. */
					sceKernelWaitSema( pThis->m_hSema, 1, NULL );
					continue;
				}

				std::string sError;
				bool bOk = FileHasData( job.sFile );
				if( bOk )
					LauncherUtil::Log( "banner: using cached %s", job.sUrl.c_str() );
				else
					bOk = LauncherHttp::GetToFile( job.sUrl, job.sFile, NULL, NULL, sError, -1 );

				Result res;
				res.sUrl = job.sUrl;
				if( bOk )
				{
					std::vector<unsigned char> vData;
					if( ReadFileAll( job.sFile, vData ) )
					{
						LauncherUtil::Log( "banner: %s -> %d bytes (%02X%02X%02X)",
							job.sUrl.c_str(), (int)vData.size(),
							vData.size() > 0 ? vData[0] : 0,
							vData.size() > 1 ? vData[1] : 0,
							vData.size() > 2 ? vData[2] : 0 );
						bOk = LauncherImages::DecodeToRgba( &vData[0], vData.size(), 512, res.img );
						if( !bOk )
						{
							LauncherUtil::Log( "banner: could not decode art for %s", job.sUrl.c_str() );
							LauncherUtil::RemoveFile( job.sFile );
						}
					}
					else
						bOk = false;
				}
				else
					LauncherUtil::Log( "banner: %s failed: %s", job.sUrl.c_str(), sError.c_str() );

				sceKernelLockMutex( pThis->m_hMutex, 1, NULL );
				if( bOk )
					pThis->m_vReady.push_back( std::move( res ) );
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
		std::vector<Result> m_vReady;
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
		Ui() : m_pFont( NULL ), m_pFontBold( NULL ), m_pPgf( NULL ),
			m_pBg( NULL ), m_pBgBlur( NULL ), m_pHat( NULL ), m_pBanner( NULL ),
			m_pSearch( NULL ), m_pSearchIcon( NULL ), m_pBubble( NULL ),
			m_pLoading( NULL ), m_pDownload( NULL ), m_pBubbleSel( NULL ),
			m_pBtnCross( NULL ), m_pBtnCircle( NULL ), m_pBtnTriangle( NULL ),
			m_pBtnSquare( NULL ), m_pBtnDpad( NULL ), m_uiPrev( 0 ),
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

			vita2d_texture *pTex[] = { m_pBg, m_pBgBlur, m_pHat, m_pBanner, m_pSearch,
				m_pSearchIcon, m_pBubble, m_pBubbleSel, m_pLoading, m_pDownload,
				m_pBtnCross, m_pBtnCircle, m_pBtnTriangle, m_pBtnSquare, m_pBtnDpad };
			for( size_t i = 0; i < sizeof(pTex) / sizeof(pTex[0]); ++i )
				if( pTex[i] != NULL )
					vita2d_free_texture( pTex[i] );

			if( m_pFont != NULL )
				delete m_pFont;
			if( m_pFontBold != NULL )
				delete m_pFontBold;
			if( m_pPgf != NULL )
				vita2d_free_pgf( m_pPgf );
			vita2d_fini();
			sceGxmTerminate();
			sceSysmoduleUnloadModule( SCE_SYSMODULE_PGF );
		}

		bool Init()
		{
			sceSysmoduleLoadModule( SCE_SYSMODULE_PGF );
			if( vita2d_init() != 1 )
				return false;
			vita2d_set_clear_color( Rgba( 0x1A, 0x0C, 0x02 ) );

			/* Ubuntu is shipped in the VPK; the system PGF stays as a fallback
			 * so a missing font file only costs the look, not the browser. */
			m_pFont = new UiFont::Font();
			if( !m_pFont->Load( kFontPath ) )
			{
				delete m_pFont;
				m_pFont = NULL;
			}
			m_pFontBold = new UiFont::Font();
			if( !m_pFontBold->Load( kFontBoldPath ) )
			{
				delete m_pFontBold;
				m_pFontBold = NULL;
			}
			if( m_pFont == NULL )
			{
				LauncherUtil::Log( "ui: %s missing, using the system font", kFontPath );
				m_pPgf = vita2d_load_default_pgf();
				if( m_pPgf == NULL )
					return false;
			}

			sceCtrlSetSamplingMode( SCE_CTRL_MODE_ANALOG );
			sceTouchSetSamplingState( SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START );

			m_pBg = LoadUi( "background.png" );
			m_pBgBlur = LoadUi( "background-blur.png" );
			m_pHat = LoadUi( "hat.png" );
			m_pBanner = LoadUi( "banner.png" );
			m_pSearch = LoadUi( "search-bar.png" );
			m_pSearchIcon = LoadUi( "search-icon.png" );
			m_pBubble = LoadUi( "pack-bubble.png" );
			m_pBubbleSel = LoadUi( "pack-bubble-selected.png" );
			m_pLoading = LoadUi( "loading.png" );
			m_pDownload = LoadUi( "download-button.png" );

			/* Optional badges for the navigation hints. */
			m_pBtnCross = LoadUi( "btn-cross.png" );
			m_pBtnCircle = LoadUi( "btn-circle.png" );
			m_pBtnTriangle = LoadUi( "btn-triangle.png" );
			m_pBtnSquare = LoadUi( "btn-square.png" );
			m_pBtnDpad = LoadUi( "btn-d-pad.png" );
			if( m_pBtnCross == NULL && m_pBtnCircle == NULL &&
				m_pBtnTriangle == NULL && m_pBtnSquare == NULL && m_pBtnDpad == NULL )
				LauncherUtil::Log( "ui: no button badge art, hints use text" );

			{
				const char *szNames[] = { "background.png", "background-blur.png", "hat.png",
					"banner.png", "search-bar.png", "search-icon.png", "pack-bubble.png",
					"pack-bubble-selected.png", "loading.png", "download-button.png" };
				vita2d_texture *pTex[] = { m_pBg, m_pBgBlur, m_pHat, m_pBanner, m_pSearch,
					m_pSearchIcon, m_pBubble, m_pBubbleSel, m_pLoading, m_pDownload };
				for( size_t i = 0; i < sizeof(pTex) / sizeof(pTex[0]); ++i )
					if( pTex[i] == NULL )
						LauncherUtil::Log( "ui: missing asset '%s%s'", kUiDir, szNames[i] );
			}

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
		/* The UI was tuned against a 24 px em; the bundled font runs a little
		 * larger, which also reads better on the Vita panel. Sizes snap to a
		 * small set so the glyph cache stays compact. */
		unsigned TextPx( float fScale ) const
		{
			const float fPx = fScale * 30.0f + 2.0f;
			if( fPx < 18.0f )
				return 18;
			if( fPx < 21.0f )
				return 21;
			if( fPx < 24.0f )
				return 24;
			if( fPx < 27.0f )
				return 27;
			return 32;
		}

		void DrawRaw( int x, int y, unsigned iColor, float fScale, const char *szText, bool bBold = false )
		{
			if( m_pFont != NULL )
			{
				UiFont::Font *pFont = ( bBold && m_pFontBold != NULL ) ? m_pFontBold : m_pFont;
				pFont->Draw( x, y, TextPx( fScale ), iColor, szText );
			}
			else if( m_pPgf != NULL )
				vita2d_pgf_draw_text( m_pPgf, x, y, iColor, fScale, szText );
		}

		int MeasureRaw( float fScale, const char *szText, bool bBold = false )
		{
			if( m_pFont != NULL )
			{
				UiFont::Font *pFont = ( bBold && m_pFontBold != NULL ) ? m_pFontBold : m_pFont;
				return pFont->Width( TextPx( fScale ), szText );
			}
			if( m_pPgf != NULL )
				return vita2d_pgf_text_width( m_pPgf, fScale, szText );
			return 0;
		}

		void Text( int x, int y, float fScale, unsigned iColor, const char *szFmt, ... )
		{
			char szBuf[512];
			va_list args;
			va_start( args, szFmt );
			vsnprintf( szBuf, sizeof(szBuf), szFmt, args );
			va_end( args );
			DrawRaw( x, y, iColor, fScale, szBuf );
		}

		void TextShadow( int x, int y, float fScale, unsigned iColor, const char *szFmt, ... )
		{
			char szBuf[512];
			va_list args;
			va_start( args, szFmt );
			vsnprintf( szBuf, sizeof(szBuf), szFmt, args );
			va_end( args );
			const float fA = (float)( ( iColor >> 24 ) & 0xFF ) / 255.0f;
			DrawRaw( x + 1, y + 2, WithAlpha( COL_SHADOW, fA * 0.55f ), fScale, szBuf );
			DrawRaw( x, y, iColor, fScale, szBuf );
		}

		void TextBold( int x, int y, float fScale, unsigned iColor, const char *szFmt, ... )
		{
			char szBuf[512];
			va_list args;
			va_start( args, szFmt );
			vsnprintf( szBuf, sizeof(szBuf), szFmt, args );
			va_end( args );
			const float fA = (float)( ( iColor >> 24 ) & 0xFF ) / 255.0f;
			DrawRaw( x + 1, y + 2, WithAlpha( COL_SHADOW, fA * 0.55f ), fScale, szBuf, true );
			DrawRaw( x, y, iColor, fScale, szBuf, true );
		}

		int TextWidth( float fScale, const char *szText )
		{
			return MeasureRaw( fScale, szText );
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

		/* ------------------------------------------------------ nav hints */
		struct HintItem
		{
			const char *szIcon;
			const char *szLabel;
		};

		vita2d_texture *HintArt( const char *szIcon ) const
		{
			if( strcmp( szIcon, "cross" ) == 0 )
				return m_pBtnCross;
			if( strcmp( szIcon, "circle" ) == 0 )
				return m_pBtnCircle;
			if( strcmp( szIcon, "triangle" ) == 0 )
				return m_pBtnTriangle;
			if( strcmp( szIcon, "square" ) == 0 )
				return m_pBtnSquare;
			if( strcmp( szIcon, "dpad" ) == 0 )
				return m_pBtnDpad;
			return NULL;
		}

		const char *HintName( const char *szIcon ) const
		{
			if( strcmp( szIcon, "cross" ) == 0 )
				return "X";
			if( strcmp( szIcon, "circle" ) == 0 )
				return "O";
			if( strcmp( szIcon, "triangle" ) == 0 )
				return "TRIANGLE";
			if( strcmp( szIcon, "square" ) == 0 )
				return "SQUARE";
			/* Only a fallback: the badge art is used whenever it loaded. */
			if( strcmp( szIcon, "dpad" ) == 0 )
				return "D-PAD";
			return "";
		}

		int HintWidth( float fScale, const HintItem *pItems, int iCount )
		{
			int iW = 0;
			for( int i = 0; i < iCount; ++i )
			{
				if( HintArt( pItems[i].szIcon ) != NULL )
					iW += HINT_ICON + HINT_ICON_GAP;
				else
					iW += TextWidth( fScale, HintName( pItems[i].szIcon ) ) + 10;
				iW += TextWidth( fScale, pItems[i].szLabel );
				if( i + 1 < iCount )
					iW += HINT_ITEM_GAP;
			}
			return iW;
		}

		/* Badge + label pairs. The badge art keeps its authored colours, so
		 * only the hint alpha is applied to it; the label uses iColor. */
		void DrawHints( int x, int y, unsigned iColor, float fScale,
			const HintItem *pItems, int iCount )
		{
			const float fA = (float)( ( iColor >> 24 ) & 0xFF ) / 255.0f;
			int iPen = x;
			for( int i = 0; i < iCount; ++i )
			{
				vita2d_texture *pArt = HintArt( pItems[i].szIcon );
				if( pArt != NULL )
				{
					const float fW = (float)vita2d_texture_get_width( pArt );
					const float fH = (float)vita2d_texture_get_height( pArt );
					if( fW > 0.0f && fH > 0.0f )
						vita2d_draw_texture_tint_scale( pArt, (float)iPen,
							(float)( y - HINT_ICON + 4 ), (float)HINT_ICON / fW,
							(float)HINT_ICON / fH, WithAlpha( 0xFFFFFFFF, fA ) );
					iPen += HINT_ICON + HINT_ICON_GAP;
				}
				else
				{
					DrawRaw( iPen, y, iColor, fScale, HintName( pItems[i].szIcon ) );
					iPen += TextWidth( fScale, HintName( pItems[i].szIcon ) ) + 10;
				}
				DrawRaw( iPen, y, iColor, fScale, pItems[i].szLabel );
				iPen += TextWidth( fScale, pItems[i].szLabel );
				if( i + 1 < iCount )
					iPen += HINT_ITEM_GAP;
			}
		}

		/* ---------------------------------------------------------- assets */
		void DrawBackground( float fAlpha )
		{
			if( m_pBg != NULL )
				vita2d_draw_texture_tint( m_pBg, 0, 0, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
				vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
					WithAlpha( Rgba( 0x8A, 0x3A, 0x06 ), fAlpha ) );
		}

		/* Blurred, pre-darkened backdrop used by the detail card. */
		void DrawBlurredBackground( float fAlpha )
		{
			if( m_pBgBlur != NULL )
				vita2d_draw_texture_tint( m_pBgBlur, 0, 0, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
				DrawBackground( fAlpha );
		}

		void DrawChrome( const std::string &sQuery, float fAlpha, bool bSearchActive )
		{
			if( m_pHat != NULL )
				vita2d_draw_texture_tint( m_pHat, 0, 0, WithAlpha( 0xFFFFFFFF, fAlpha ) );
			else
				vita2d_draw_rectangle( 0, 0, SCREEN_W, HAT_H,
					WithAlpha( Rgba( 0x40, 0x24, 0x08, 0xE0 ), fAlpha ) );

			if( m_pBanner != NULL )
				vita2d_draw_texture_tint( m_pBanner, BANNER_X, BANNER_Y,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );

			/* Search field: the bar art is already a translucent glass strip, so
			 * it is drawn as authored and the orange backdrop shows through. */
			if( m_pSearch != NULL )
				vita2d_draw_texture_tint( m_pSearch, SEARCH_X, SEARCH_Y,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );

			const int iTextLeft = SEARCH_X + 20;
			const int iTextRight = SEARCH_X + SEARCH_W - 64;
			const int iBaseline = SEARCH_Y + 34;
			const float fScale = 0.7f;
			const unsigned iShadow = WithAlpha( COL_SHADOW, 0.6f * fAlpha );
			if( sQuery.empty() )
			{
				const unsigned iCol = WithAlpha( Rgba( 0xFF, 0xF1, 0xDA ),
					fAlpha * ( bSearchActive ? 0.65f : 0.9f ) );
				DrawRaw( iTextLeft + 1, iBaseline + 1, iShadow, fScale, "Search packs..." );
				DrawRaw( iTextLeft, iBaseline, iCol, fScale, "Search packs..." );
			}
			else
			{
				std::string sShown = sQuery;
				while( !sShown.empty() &&
					TextWidth( fScale, sShown.c_str() ) > iTextRight - iTextLeft )
					sShown.erase( 0, 1 );
				DrawRaw( iTextLeft + 1, iBaseline + 1, iShadow, fScale, sShown.c_str() );
				DrawRaw( iTextLeft, iBaseline, WithAlpha( COL_TEXT, fAlpha ), fScale, sShown.c_str() );
				DrawRaw( iTextLeft + TextWidth( fScale, sShown.c_str() ) + 2, iBaseline,
					WithAlpha( COL_AMBER, fAlpha * 0.9f ), fScale, "|" );
			}

			if( m_pSearchIcon != NULL )
				vita2d_draw_texture_tint( m_pSearchIcon,
					SEARCH_X + SEARCH_W - 35 - 14, SEARCH_Y + 7,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );
		}

		/* ---------------------------------------------------------- banners */
		/* Everything is keyed by the banner URL: the pack URL identifies the
		 * archive, the banner URL identifies the art, and they are different
		 * strings. Mixing them up meant a downloaded cover was stored under
		 * one key and looked up under the other, so it never appeared. */
		vita2d_texture *GetBanner( const PackEntry &p )
		{
			if( p.sBanner.empty() )
				return NULL;
			std::map<std::string, BannerSlot>::iterator it = m_banners.find( p.sBanner );
			if( it == m_banners.end() )
				return NULL;
			it->second.uiUsed = m_iTick;
			return it->second.pTex;
		}

		bool BannerKnown( const PackEntry &p ) const
		{
			if( p.sBanner.empty() )
				return true;
			return m_banners.find( p.sBanner ) != m_banners.end() ||
				m_requested.find( p.sBanner ) != m_requested.end();
		}

		void RequestBanner( const PackEntry &p )
		{
			if( !m_bBannerCache || p.sBanner.empty() || BannerKnown( p ) )
				return;
			/* A cover that failed once is likely a hiccup on the mirror, so let
			 * it retry a couple of times instead of staying grey for the run. */
			if( m_attempts[p.sBanner] >= 3 )
				return;
			std::map<std::string, unsigned>::const_iterator itRetry = m_retryAt.find( p.sBanner );
			if( itRetry != m_retryAt.end() && m_iTick < itRetry->second )
				return;

			const std::string sFile = CacheFile( p.sBanner );
			m_requested[p.sBanner] = true;
			++m_attempts[p.sBanner];

			/* The worker reads a cached file and decodes it off the main
			 * thread; doing that here used to stall the list for every row
			 * that scrolled into view. */
			m_loader.Request( p.sBanner, sFile );
		}

		void PumpBanners()
		{
			std::vector<BannerLoader::Result> vDone;
			std::vector<std::string> vFailed;
			/* Uploading two 512 px covers costs a couple of milliseconds, so
			 * even a screenful cannot push the frame over 16 ms. */
			m_loader.PopReady( vDone, vFailed, 2 );
			for( size_t i = 0; i < vFailed.size(); ++i )
			{
				m_failed[vFailed[i]] = true;
				m_requested.erase( vFailed[i] );
				m_retryAt[vFailed[i]] = m_iTick + 180;
				LauncherUtil::Log( "banner: download failed (%d/3) %s",
					m_attempts[vFailed[i]], vFailed[i].c_str() );
			}
			for( size_t i = 0; i < vDone.size(); ++i )
			{
				if( StoreBanner( vDone[i].sUrl, vDone[i].img ) )
					continue;
				m_failed[vDone[i].sUrl] = true;
				m_requested.erase( vDone[i].sUrl );
				m_retryAt[vDone[i].sUrl] = m_iTick + 180;
			}
		}

		/* Thumbnail inside a bubble. The whole cover is scaled down to fit the
		 * 96x54 slot and centred, so no part of the art is cropped away; the
		 * capsule shows through the leftover space. The bubble stays clean:
		 * no placeholder shapes when the art is not there yet, the cover
		 * simply appears a moment later. */
		void DrawThumb( const PackEntry &p, float x, float y, float fAlpha )
		{
			vita2d_texture *pTex = GetBanner( p );
			if( pTex == NULL )
				return;

			const float fTexW = (float)vita2d_texture_get_width( pTex );
			const float fTexH = (float)vita2d_texture_get_height( pTex );
			if( fTexW <= 0.0f || fTexH <= 0.0f )
				return;

			const float fScale = fminf( (float)THUMB_W / fTexW, (float)THUMB_H / fTexH );
			const float fW = fTexW * fScale;
			const float fH = fTexH * fScale;
			vita2d_draw_texture_tint_scale( pTex,
				x + ( (float)THUMB_W - fW ) * 0.5f,
				y + ( (float)THUMB_H - fH ) * 0.5f,
				fScale, fScale, WithAlpha( 0xFFFFFFFF, fAlpha ) );
		}

		/* Large cover for the detail screen: the whole image is scaled to fit
		 * the cover area and centred, letterboxed by the backdrop instead of
		 * being cropped at the edges. */
		void DrawBannerFill( const PackEntry &p, float x, float y, float w, float h, float fAlpha )
		{
			vita2d_texture *pTex = GetBanner( p );
			if( pTex == NULL )
			{
				/* Barely darker patch while the cover is in flight: no frame,
				 * no lettering, nothing that looks like a placeholder card. */
				vita2d_draw_rectangle( x, y, w, h,
					WithAlpha( Rgba( 0x1E, 0x0E, 0x04 ), 0.28f * fAlpha ) );
				return;
			}
			const float fTexW = (float)vita2d_texture_get_width( pTex );
			const float fTexH = (float)vita2d_texture_get_height( pTex );
			if( fTexW <= 0.0f || fTexH <= 0.0f )
				return;

			const float fScale = fminf( w / fTexW, h / fTexH );
			const float fW = fTexW * fScale;
			const float fH = fTexH * fScale;
			vita2d_draw_texture_tint_scale( pTex,
				x + ( w - fW ) * 0.5f, y + ( h - fH ) * 0.5f,
				fScale, fScale, WithAlpha( 0xFFFFFFFF, fAlpha ) );
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
						DrawRaw( (int)( SCREEN_W * 0.5f - TextWidth( 0.7f, szStage ) * 0.5f ),
							(int)fCenter + 56, WithAlpha( COL_INK, 0.92f ), 0.7f, szStage );
					if( fProgress > 0.0f )
						Text( (int)( SCREEN_W * 0.5f - 22 ), (int)fCenter + 84,
							WithAlpha( COL_INK, 0.85f ), 0.68f, "%d%%", (int)( fProgress * 100.0f ) );
				}
				else
				{
					Text( 60, 200, 1.0f, COL_INK,
						sQuery.empty() ? "No packs to show." : "Nothing matches \"%s\".",
						sQuery.c_str() );
					DrawRaw( 60, 244, WithAlpha( COL_INK, 0.85f ), 0.62f, "Press" );
					const HintItem hints[] = { { "triangle", "search" }, { "square", "clear" } };
					DrawHints( 60 + TextWidth( 0.62f, "Press" ) + 14, 244,
						WithAlpha( COL_INK, 0.85f ), 0.62f, hints, 2 );
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

			/* Footer: position on the left, the control badges on the right.
			 * The row used to be dark ink straight on the wallpaper, which the
			 * pale bottom of the art swallowed completely - the controls looked
			 * gone. It sits on a soft dark strip now and stays up even when the
			 * search has no matches, so the pad is always discoverable. */
			const float fBarY = (float)LIST_BOTTOM - 8.0f;
			vita2d_draw_rectangle( 0.0f, fBarY, (float)SCREEN_W, (float)SCREEN_H - fBarY,
				WithAlpha( Rgba( 0x2A, 0x14, 0x06 ), 0.55f ) );
			if( !bLoading && !vView.empty() )
			{
				char sPos[64];
				snprintf( sPos, sizeof(sPos), "%d / %d", iSel + 1, (int)vView.size() );
				DrawRaw( 34, LIST_BOTTOM + 14, WithAlpha( COL_CREAM, 0.92f ), 0.62f, sPos );
			}
			if( bLoading )
			{
				const HintItem hints[] = { { "cross", "cancel" } };
				DrawHints( SCREEN_W - 34 - HintWidth( 0.62f, hints, 1 ),
					LIST_BOTTOM + 14, WithAlpha( COL_CREAM, 0.92f ), 0.62f, hints, 1 );
			}
			else
			{
				const HintItem hints[] = { { "dpad", "scroll" }, { "cross", "details" },
					{ "triangle", "search" }, { "square", "clear" }, { "circle", "close" } };
				const int iHintCount = (int)( sizeof(hints) / sizeof(hints[0]) );
				DrawHints( SCREEN_W - 34 - HintWidth( 0.62f, hints, iHintCount ),
					LIST_BOTTOM + 14, WithAlpha( COL_CREAM, 0.92f ), 0.62f, hints, iHintCount );
			}
		}

		/* The capsule art is translucent by design: draw it exactly once so the
		 * orange backdrop keeps showing through it. The selected state swaps in
		 * its own darker sprite instead of stacking another pass. */
		void DrawBubble( const PackEntry &p, float x, float y, bool bSelected, float fAlpha )
		{
			vita2d_texture *pArt = ( bSelected && m_pBubbleSel != NULL ) ? m_pBubbleSel : m_pBubble;
			if( pArt != NULL )
			{
				const float fTexW = (float)vita2d_texture_get_width( pArt );
				const float fTexH = (float)vita2d_texture_get_height( pArt );
				if( fTexW <= 0.0f || fTexH <= 0.0f )
					return;
				const float fSx = (float)BUBBLE_W / fTexW;
				const float fSy = (float)BUBBLE_H / fTexH;

				vita2d_draw_texture_tint_scale( pArt, x, y, fSx, fSy,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );

				/* Fallback when the selected art is missing: lighten the plain
				 * capsule a little so the cursor is still visible. */
				if( bSelected && m_pBubbleSel == NULL )
				{
					const float fPulse = 0.5f + 0.5f * sinf( (float)m_iTick * 0.08f );
					vita2d_draw_texture_tint_scale( pArt, x, y, fSx, fSy,
						WithAlpha( Rgba( 0xFF, 0xF3, 0xE0 ), fAlpha * ( 0.20f + 0.10f * fPulse ) ) );
				}
			}
			else
			{
				DrawPill( x, y, (float)BUBBLE_W, (float)BUBBLE_H,
					WithAlpha( COL_AMBER, fAlpha * 0.85f ) );
			}

			DrawThumb( p, x + 20, y + 10, fAlpha );

			const int iTextX = (int)x + 128;
			const std::string sName = FitText( p.sName, 0.8f, BUBBLE_W - 300 );
			DrawRaw( iTextX + 1, (int)y + 33, WithAlpha( COL_SHADOW, 0.45f * fAlpha ),
				0.8f, sName.c_str(), true );
			DrawRaw( iTextX, (int)y + 31, WithAlpha( COL_TEXT, fAlpha ), 0.8f, sName.c_str(), true );

			char sMeta[128];
			if( p.iSongs > 0 && !p.sDate.empty() )
				snprintf( sMeta, sizeof(sMeta), "%d songs   %s", p.iSongs, p.sDate.c_str() );
			else if( p.iSongs > 0 )
				snprintf( sMeta, sizeof(sMeta), "%d songs", p.iSongs );
			else
				snprintf( sMeta, sizeof(sMeta), "%s", p.sDate.c_str() );
			DrawRaw( iTextX + 1, (int)y + 57, WithAlpha( COL_SHADOW, 0.38f * fAlpha ), 0.58f, sMeta );
			DrawRaw( iTextX, (int)y + 55, WithAlpha( COL_TEXT, fAlpha ),
				0.58f, sMeta );

			if( p.fSizeMB > 0.0 )
			{
				char sSize[32];
				if( p.fSizeMB >= 1024.0 )
					snprintf( sSize, sizeof(sSize), "%.1f GB", p.fSizeMB / 1024.0 );
				else
					snprintf( sSize, sizeof(sSize), "%.0f MB", p.fSizeMB );
				const int iW = TextWidth( 0.72f, sSize );
				DrawRaw( (int)( x + BUBBLE_W - 27 - iW ), (int)y + 42,
					WithAlpha( COL_SHADOW, 0.38f * fAlpha ), 0.72f, sSize, true );
				DrawRaw( (int)( x + BUBBLE_W - 26 - iW ), (int)y + 41,
					WithAlpha( COL_TEXT, fAlpha ), 0.72f, sSize, true );
			}

			if( p.bInstalled )
				TextBold( (int)( x + BUBBLE_W - 26 - TextWidth( 0.54f, "installed" ) ),
					(int)y + 60, 0.54f, WithAlpha( COL_CREAM, fAlpha ), "installed" );
		}

		/* ----------------------------------------------------------- detail */
		/* The list dissolves under a blurred, dimmed backdrop and the pack
		 * content is drawn straight onto it: no panel, no rounded frame. */
		void SceneDetail( const PackEntry &p, float fListFade, float fCard, bool bButtonDown )
		{
			const float fB = Clampf( fListFade, 0.0f, 1.0f );
			if( fB <= 0.001f )
				return;

			DrawBlurredBackground( fB );
			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
				WithAlpha( Rgba( 0x12, 0x07, 0x01 ), 0.40f * fB ) );

			const float fA = Clampf( fCard, 0.0f, 1.0f );
			if( fA <= 0.001f )
				return;

			const float fEase = 1.0f - ( 1.0f - fA ) * ( 1.0f - fA );
			const int iDy = (int)( ( 1.0f - fEase ) * 18.0f );
			const float fAlpha = fEase;

			/* Cover art, edge to edge, over the backdrop. */
			DrawBannerFill( p, (float)DETAIL_COVER_X, (float)( DETAIL_COVER_Y + iDy ),
				(float)DETAIL_COVER_W, (float)DETAIL_COVER_H, fAlpha );

			/* Title sits above the cover, in the free strip under the top; it
			 * can use the full width because the cover starts lower. */
			const int iTitleW = SCREEN_W - DETAIL_PAD * 2;
			const std::string sName = FitText( p.sName, 1.0f, iTitleW );
			TextBold( DETAIL_PAD, DETAIL_TITLE_Y + iDy, 1.0f, WithAlpha( COL_GOLD, fAlpha ),
				"%s", sName.c_str() );
			if( !p.sGroup.empty() && p.sGroup != p.sName )
			{
				const std::string sGroup = FitText( p.sGroup, 0.58f, iTitleW );
				DrawRaw( DETAIL_PAD, DETAIL_GROUP_Y + iDy,
					WithAlpha( COL_CREAM, 0.80f * fAlpha ), 0.58f, sGroup.c_str() );
			}

			/* Facts column: dim label, bright value underneath. */
			int iLine = DETAIL_FACT_Y + iDy;
			const unsigned iLabel = WithAlpha( Rgba( 0xFF, 0xE6, 0xC0 ), 0.60f * fAlpha );
			if( p.iSongs > 0 )
			{
				DrawRaw( DETAIL_INFO_X, iLine, iLabel, 0.5f, "SONGS" );
				TextBold( DETAIL_INFO_X, iLine + DETAIL_FACT_VALUE_DY, 0.68f,
					WithAlpha( COL_TEXT, fAlpha ), "%d", p.iSongs );
				iLine += DETAIL_FACT_STEP;
			}
			if( !p.sDate.empty() )
			{
				DrawRaw( DETAIL_INFO_X, iLine, iLabel, 0.5f, "RELEASED" );
				TextBold( DETAIL_INFO_X, iLine + DETAIL_FACT_VALUE_DY, 0.68f,
					WithAlpha( COL_TEXT, fAlpha ), "%s", p.sDate.c_str() );
				iLine += DETAIL_FACT_STEP;
			}
			if( p.fSizeMB > 0.0 )
			{
				DrawRaw( DETAIL_INFO_X, iLine, iLabel, 0.5f, "SIZE" );
				TextBold( DETAIL_INFO_X, iLine + DETAIL_FACT_VALUE_DY, 0.68f,
					WithAlpha( COL_TEXT, fAlpha ), p.fSizeMB >= 1024.0 ? "%.2f GB" : "%.1f MB",
					p.fSizeMB >= 1024.0 ? p.fSizeMB / 1024.0 : p.fSizeMB );
			}

			/* Download button (authored art). */
			const float fBtnX = (float)DETAIL_BTN_X;
			const float fBtnY = (float)( DETAIL_BTN_Y + iDy ) + ( bButtonDown ? 2.0f : 0.0f );
			if( p.bInstalled )
			{
				TextBold( DETAIL_BTN_X, DETAIL_BTN_Y + iDy + 40, 0.78f,
					WithAlpha( COL_OK, fAlpha ), "INSTALLED" );
			}
			else if( m_pDownload != NULL )
			{
				vita2d_draw_texture_tint_scale( m_pDownload, fBtnX, fBtnY, 1.0f, 1.0f,
					WithAlpha( 0xFFFFFFFF, fAlpha ) );
				if( bButtonDown )
				{
					/* Pressed: sinks 2 px and darkens, like a real key. */
					vita2d_draw_texture_tint_scale( m_pDownload, fBtnX, fBtnY, 1.0f, 1.0f,
						WithAlpha( Rgba( 0x8A, 0x50, 0x24 ), 0.45f * fAlpha ) );
				}
			}
			else
			{
				TextBold( DETAIL_BTN_X, DETAIL_BTN_Y + iDy + 40, 0.78f,
					WithAlpha( COL_GOLD, fAlpha ), "DOWNLOAD" );
			}

			if( p.bInstalled )
			{
				const HintItem hints[] = { { "circle", "back" } };
				DrawHints( DETAIL_PAD, DETAIL_HINT_Y + iDy,
					WithAlpha( COL_TEXT, 0.90f * fAlpha ), 0.58f, hints, 1 );
			}
			else
			{
				const HintItem hints[] = { { "cross", "download" },
					{ "circle", "back" } };
				DrawHints( DETAIL_PAD, DETAIL_HINT_Y + iDy,
					WithAlpha( COL_TEXT, 0.90f * fAlpha ), 0.58f, hints, 2 );
			}
		}

		/* ------------------------------------------------------------- busy */
		void SceneBusy( const char *szStage, float fProgress, long long iDone, long long iTotal,
			float fFade = 1.0f )
		{
			DrawBackground( 1.0f );
			DrawChrome( std::string(), 1.0f, true );

			/* The bundled spinner is dark navy, so the veil stays light enough
			 * for it to read; a heavier dim swallowed it completely. */
			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
				WithAlpha( Rgba( 0x14, 0x08, 0x02 ), 0.45f * fFade ) );
			Spinner( SCREEN_W * 0.5f, SCREEN_H * 0.5f - 10.0f, 96.0f * ( 0.85f + 0.15f * fFade ), fFade );

			/* Status line sits straight on the dimmed screen: no extra plate. */
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
			vita2d_draw_rectangle( iBarX, iBarY, iBarW, 16,
				WithAlpha( Rgba( 0x2A, 0x14, 0x06 ), 0.85f ) );
			if( fProgress > 0.0f )
				vita2d_draw_rectangle( iBarX, iBarY, (int)( iBarW * Clampf( fProgress, 0.0f, 1.0f ) ),
					16, WithAlpha( COL_AMBER, 1.0f ) );

			TextShadow( 38, SCREEN_H - 24, 0.68f, WithAlpha( COL_TEXT, fFade ), "%s", sStatus );
		}

		void SceneMessage( const char *szTitle, const char *szBody, bool bError )
		{
			DrawBackground( 1.0f );
			DrawChrome( std::string(), 1.0f, true );
			vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
				WithAlpha( Rgba( 0x10, 0x06, 0x02 ), 0.62f ) );

			const float fW = 720.0f;
			const float fH = 236.0f;
			const float fX = ( SCREEN_W - fW ) * 0.5f;
			const float fY = ( SCREEN_H - fH ) * 0.5f;

			DrawRoundRect( fX + 6.0f, fY + 10.0f, fW, fH, 20.0f,
				WithAlpha( Rgba( 0, 0, 0 ), 0.4f ) );
			DrawRoundRect( fX, fY, fW, fH, 20.0f, WithAlpha( COL_GOLD, 0.85f ) );
			DrawRoundRect( fX + 3.0f, fY + 3.0f, fW - 6.0f, fH - 6.0f, 17.0f,
				WithAlpha( COL_CARD, 0.98f ) );
			DrawRoundRect( fX + 7.0f, fY + 7.0f, fW - 14.0f, fH - 14.0f, 14.0f,
				WithAlpha( COL_CARD_HI, 0.96f ) );

			TextBold( (int)fX + 34, (int)fY + 66, 0.9f, bError ? COL_ERR : COL_GOLD,
				"%s", szTitle != NULL ? szTitle : "" );

			const std::string sBody = FitText( szBody != NULL ? szBody : "", 0.62f,
				(int)fW - 68 );
			DrawRaw( (int)fX + 34, (int)fY + 116, COL_CREAM, 0.62f, sBody.c_str() );
			DrawRaw( (int)fX + 34, (int)fY + (int)fH - 30,
				WithAlpha( COL_CREAM, 0.75f ), 0.58f, "X  continue" );
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

		/* Failures are reported in the system message window: the box is drawn
		 * by the common dialog (ScePaf) the firmware and the keyboard already
		 * use, so an error reads as a system prompt instead of as part of the
		 * browser skin. End() hands every frame to
		 * vita2d_common_dialog_update(), which is what composites the box, and
		 * the dialog runs its own button handling. */
		void SystemAlert( const char *szTitle, const char *szBody )
		{
			/* The param and the text must outlive sceMsgDialogInit(). */
			static SceMsgDialogUserMessageParam userParam;
			static std::string sMessage;

			sMessage = szTitle != NULL ? szTitle : "";
			if( szBody != NULL && szBody[0] != 0 )
			{
				if( !sMessage.empty() )
					sMessage += "\n";
				sMessage += szBody;
			}
			if( sMessage.size() > SCE_MSG_DIALOG_USER_MSG_SIZE - 1 )
				sMessage.resize( SCE_MSG_DIALOG_USER_MSG_SIZE - 1 );

			/* The common dialog is already up: the search keyboard in AskText()
			 * runs through the same module, and it needs no explicit load. */
			SceMsgDialogParam param;
			sceMsgDialogParamInit( &param );
			memset( &userParam, 0, sizeof(userParam) );
			userParam.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
			userParam.msg = (const SceChar8 *)sMessage.c_str();
			param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
			param.userMsgParam = &userParam;

			int iRet = sceMsgDialogInit( &param );
			if( iRet == SCE_COMMON_DIALOG_ERROR_NOT_CONFIGURED )
			{
				SceCommonDialogConfigParam cfg;
				sceCommonDialogConfigParamInit( &cfg );
				sceCommonDialogSetConfigParam( &cfg );
				iRet = sceMsgDialogInit( &param );
			}
			if( iRet < 0 )
			{
				LauncherUtil::Log( "dialog: sceMsgDialogInit failed (0x%08X), drawing the panel instead",
					(unsigned)iRet );
				Message( szTitle, szBody, true );
				return;
			}

			LauncherUtil::Log( "dialog: \"%s\"", sMessage.c_str() );
			for( ;; )
			{
				const SceCommonDialogStatus eStatus = sceMsgDialogGetStatus();
				if( eStatus == SCE_COMMON_DIALOG_STATUS_FINISHED )
				{
					SceMsgDialogResult res;
					memset( &res, 0, sizeof(res) );
					sceMsgDialogGetResult( &res );
					break;
				}
				if( eStatus != SCE_COMMON_DIALOG_STATUS_RUNNING )
					break;
				/* The browser stays visible behind the dimmed backdrop. */
				Begin();
				DrawBackground( 1.0f );
				DrawChrome( std::string(), 1.0f, true );
				vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
					WithAlpha( Rgba( 0x10, 0x06, 0x02 ), 0.55f ) );
				End();
				sceDisplayWaitVblankStart();
			}
			sceMsgDialogTerm();

			/* Drop the press that closed the window so it does not act on the
			 * screen behind it. */
			for( int i = 0; i < 120; ++i )
			{
				Poll();
				if( m_Pad.buttons == 0 && !m_bTouchDown )
					break;
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

		bool StoreBanner( const std::string &sUrl, const LauncherImages::RawImage &img )
		{
			m_requested.erase( sUrl );
			vita2d_texture *pTex = LauncherImages::CreateTexture( img );
			if( pTex == NULL )
			{
				m_failed[sUrl] = true;
				LauncherUtil::Log( "banner: could not upload art for %s", sUrl.c_str() );
				return false;
			}

			/* Keep the texture footprint bounded; drop the least recently drawn. */
			while( m_banners.size() >= 16 )
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
			return true;
		}

		UiFont::Font *m_pFont;
		UiFont::Font *m_pFontBold;
		vita2d_pgf *m_pPgf;
		vita2d_texture *m_pBg;
		vita2d_texture *m_pBgBlur;
		vita2d_texture *m_pHat;
		vita2d_texture *m_pBanner;
		vita2d_texture *m_pSearch;
		vita2d_texture *m_pSearchIcon;
		vita2d_texture *m_pBubble;
		vita2d_texture *m_pBubbleSel;
		vita2d_texture *m_pLoading;
		vita2d_texture *m_pDownload;
		vita2d_texture *m_pBtnCross;
		vita2d_texture *m_pBtnCircle;
		vita2d_texture *m_pBtnTriangle;
		vita2d_texture *m_pBtnSquare;
		vita2d_texture *m_pBtnDpad;

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
		std::map<std::string, int> m_attempts;
		std::map<std::string, unsigned> m_retryAt;
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
		/* One redraw per display frame: the spinner has to turn at 60 fps, and
		 * the old 50 ms throttle made it look like a slideshow. */
		if( s_uiLastDraw == 0 || uiNow - s_uiLastDraw >= 16 * 1000 )
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
		if( s_uiLastDraw == 0 || uiNow - s_uiLastDraw >= 16 * 1000 )
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
		LauncherUtil::Log( "list: enter" );
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
		float fListFade = 0.0f;   /* blurred backdrop over the list */
		float fCard = 0.0f;       /* detail card in/out */
		bool bDetailClosing = false;
		int iIdleFrames = 0;
		float fAfterFade = 0.0f;

		if( !FetchIndex( ui, packs, sQuery, "Loading packs", sError ) )
		{
			ui.SystemAlert( "Could not load the pack list", sError.c_str() );
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
								ui.SystemAlert( "Could not load the full catalogue", sFetchError.c_str() );
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
					ui.SystemAlert( "Could not refresh the list", sError.c_str() );
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
					fListFade = 0.0f;
					fCard = 0.0f;
					bDetailClosing = false;
					ui.RequestBanner( vPacks[(size_t)vView[(size_t)iDetail]] );
					continue;
				}
			}
			else
			{
				const PackEntry &p = vPacks[(size_t)vView[(size_t)iDetail]];
				/* The cover may still be in flight when the card opens. */
				ui.RequestBanner( p );

				const bool bOverButton = ui.TouchX() >= DETAIL_BTN_X &&
					ui.TouchX() <= DETAIL_BTN_X + DETAIL_BTN_W &&
					ui.TouchY() >= DETAIL_BTN_Y && ui.TouchY() <= DETAIL_BTN_Y + DETAIL_BTN_H;
				const bool bButtonHit = bTapped && bOverButton;
				/* With no panel around the content, a tap that misses the
				 * button is the "back" gesture. */
				const bool bOutside = bTapped && !bOverButton;

				if( bDetailClosing )
				{
					fCard = Clampf( fCard - 0.16f, 0.0f, 1.0f );
					if( fCard <= 0.0f )
					{
						fListFade = Clampf( fListFade - 0.14f, 0.0f, 1.0f );
						if( fListFade <= 0.0f )
						{
							iDetail = -1;
							bDetailClosing = false;
							continue;
						}
					}
				}
				else
				{
					/* List dissolves first, then the card slides up. */
					fListFade = Clampf( fListFade + 0.22f, 0.0f, 1.0f );
					if( fListFade > 0.6f )
						fCard = Clampf( fCard + 0.17f, 0.0f, 1.0f );

					if( fCard >= 1.0f )
					{
						if( ui.Pressed( SCE_CTRL_CIRCLE ) || bOutside )
							bDetailClosing = true;
						else if( !p.bInstalled && ( ui.Pressed( SCE_CTRL_CROSS ) || bButtonHit ) )
						{
							if( InstallPack( ui, packs, (size_t)vView[(size_t)iDetail], bAll, sError ) )
								packs.RefreshInstalledFor( vView, bAll );
							else if( !sError.empty() && sError != "cancelled" )
								ui.SystemAlert( "Install failed", sError.c_str() );
							fAfterFade = 0.65f;
							bDetailClosing = true;
						}
					}
				}
			}

			/* ------------------------------------------------ banner prefetch */
			/* Fetch covers for everything on screen right away: waiting for the
			 * list to stand still left most bubbles without art for a long time. */
			if( !vView.empty() && iDetail < 0 )
			{
				const int iFirstP = (int)floorf( fScroll / (float)BUBBLE_PITCH );
				for( int i = iFirstP - 1; i < iFirstP + VISIBLE_ROWS + 2 && i < iCount; ++i )
					if( i >= 0 )
						ui.RequestBanner( vPacks[(size_t)vView[(size_t)i]] );
			}

			/* ------------------------------------------------------ draw */
			const bool bButtonDown = iDetail >= 0 && ui.TouchDown() &&
				ui.TouchX() >= DETAIL_BTN_X && ui.TouchX() <= DETAIL_BTN_X + DETAIL_BTN_W &&
				ui.TouchY() >= DETAIL_BTN_Y && ui.TouchY() <= DETAIL_BTN_Y + DETAIL_BTN_H;

			ui.Begin();
			ui.SceneList( vPacks, vView, iSel, fScroll, sQuery, false, NULL, -1.0f );
			if( iDetail >= 0 )
				ui.SceneDetail( vPacks[(size_t)vView[(size_t)iDetail]], fListFade, fCard, bButtonDown );
			if( fAfterFade > 0.0f )
			{
				vita2d_draw_rectangle( 0, 0, SCREEN_W, SCREEN_H,
					WithAlpha( Rgba( 0x00, 0x00, 0x00 ), fAfterFade ) );
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
			LauncherUtil::Log( "downloader: ui ready" );
			LauncherPacks packs;
			RunList( ui, packs );
		}
	}

	LauncherHttp::Shutdown();
	LauncherUtil::Log( "downloader: done, booting the game" );
}
