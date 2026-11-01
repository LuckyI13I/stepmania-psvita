#include "global.h"
#include "VitaTouch.h"
#include "Preference.h"
#include "RageLog.h"

#ifdef VITA
#include <psp2/touch.h>
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#endif

/* The preferences exist on every build (so Preferences.ini and the option rows
 * stay consistent); only the sampling and drawing are Vita-only. */
Preference<bool> g_bVitaTouchControls( "VitaTouchControls", false );
Preference<bool> g_bVitaTouchOverlay( "VitaTouchOverlay", true );
Preference<int>  g_iVitaTouchOverlayOpacity( "VitaTouchOverlayOpacity", 40 );

bool VitaTouch::s_bDown[VitaTouch::NUM_ZONES] = { false, false, false, false };
bool VitaTouch::s_bInit = false;
bool VitaTouch::s_bPlayActive = false;
bool VitaTouch::s_bWheelActive = false;
bool VitaTouch::s_bTouching = false;
int VitaTouch::s_iLastY = 0;
int VitaTouch::s_iAccumY = 0;
int VitaTouch::s_iPendingTaps = 0;

#ifdef VITA
/* Front panel reports are in panel coordinates (1920x1088 by default). */
static int s_iPanelWidth = 1920;

static const unsigned char s_ZoneColor[VitaTouch::NUM_ZONES][3] =
{
	{ 232,  72, 112 }, /* left  - pink   */
	{  72, 160, 232 }, /* down  - blue   */
	{  96, 200, 104 }, /* up    - green  */
	{ 232, 196,  72 }, /* right - yellow */
};

struct OverlayVertex
{
	float x, y, z;
	float u, v;
	unsigned char r, g, b, a;
};

static inline void SetVert( OverlayVertex &o, float x, float y,
	const unsigned char *rgb, unsigned char a )
{
	o.x = x; o.y = y; o.z = 0.0f;
	o.u = 0.0f; o.v = 0.0f;
	o.r = rgb[0]; o.g = rgb[1]; o.b = rgb[2]; o.a = a;
}

static inline void PushQuad( OverlayVertex *v, int &n,
	float x0, float y0, float x1, float y1,
	const unsigned char *rgb, unsigned char a )
{
	SetVert( v[n + 0], x0, y0, rgb, a );
	SetVert( v[n + 1], x1, y0, rgb, a );
	SetVert( v[n + 2], x1, y1, rgb, a );
	SetVert( v[n + 3], x0, y0, rgb, a );
	SetVert( v[n + 4], x1, y1, rgb, a );
	SetVert( v[n + 5], x0, y1, rgb, a );
	n += 6;
}
#endif /* VITA */

void VitaTouch::Init()
{
#ifdef VITA
	if( s_bInit )
		return;
	s_bInit = true;

	SceTouchPanelInfo info;
	if( sceTouchGetPanelInfo( SCE_TOUCH_PORT_FRONT, &info ) >= 0 )
	{
		const int iWidth = (int)info.maxAaX - (int)info.minAaX + 1;
		if( iWidth > 0 )
			s_iPanelWidth = iWidth;
	}

	const int iFront = sceTouchSetSamplingState( SCE_TOUCH_PORT_FRONT,
		SCE_TOUCH_SAMPLING_STATE_START );
	const int iBack = sceTouchSetSamplingState( SCE_TOUCH_PORT_BACK,
		SCE_TOUCH_SAMPLING_STATE_STOP );

	LOG->Info( "VitaTouch: front panel %dpx, sampling start=0x%x back-stop=0x%x",
		s_iPanelWidth, (unsigned)iFront, (unsigned)iBack );
#endif
}

void VitaTouch::Poll()
{
	for( int i = 0; i < NUM_ZONES; ++i )
		s_bDown[i] = false;

#ifdef VITA
	const bool bLanes = InputActive();
	const bool bWheel = s_bWheelActive && !s_bPlayActive;

	if( !bLanes && !bWheel )
	{
		s_bTouching = false;
		s_iAccumY = 0;
		s_iPendingTaps = 0;
		return;
	}

	SceTouchData data;
	if( sceTouchPeek( SCE_TOUCH_PORT_FRONT, &data, 1 ) < 0 )
		return;

	uint32_t iReports = data.reportNum;
	if( iReports > SCE_TOUCH_MAX_REPORT )
		iReports = SCE_TOUCH_MAX_REPORT;

	if( bLanes )
	{
		for( uint32_t i = 0; i < iReports; ++i )
		{
			int iZone = (int)( (int)data.report[i].x * NUM_ZONES / s_iPanelWidth );
			if( iZone < 0 )
				iZone = 0;
			else if( iZone >= NUM_ZONES )
				iZone = NUM_ZONES - 1;
			s_bDown[iZone] = true;
		}
	}

	if( bWheel )
	{
		/* Vertical drag = song wheel fling. One item per LANE_STEP pixels,
		 * so a half-screen swipe moves ~7 entries. */
		const int iStep = 36;
		if( iReports == 0 )
		{
			s_bTouching = false;
			s_iAccumY = 0;
			return;
		}

		const int iY = (int)data.report[0].y;
		if( !s_bTouching )
		{
			s_bTouching = true;
			s_iLastY = iY;
			s_iAccumY = 0;
			return;
		}

		s_iAccumY += iY - s_iLastY;
		s_iLastY = iY;

		/* Swipe up (decreasing Y) = next song, matching the wheel direction. */
		while( s_iAccumY <= -iStep )
		{
			++s_iPendingTaps;
			s_iAccumY += iStep;
		}
		while( s_iAccumY >= iStep )
		{
			--s_iPendingTaps;
			s_iAccumY -= iStep;
		}
		if( s_iPendingTaps > 12 )
			s_iPendingTaps = 12;
		else if( s_iPendingTaps < -12 )
			s_iPendingTaps = -12;
	}
#endif
}

int VitaTouch::ConsumeWheelTaps()
{
	/* A fling can queue a dozen entries; apply them over several frames so the
	 * wheel animation and music samples keep up with the selection. */
	const int iMaxPerFrame = 3;
	int iTaps = s_iPendingTaps;
	if( iTaps > iMaxPerFrame )
		iTaps = iMaxPerFrame;
	else if( iTaps < -iMaxPerFrame )
		iTaps = -iMaxPerFrame;
	s_iPendingTaps -= iTaps;
	return iTaps;
}

void VitaTouch::SetWheelActive( bool bActive )
{
	if( s_bWheelActive == bActive )
		return;
	s_bWheelActive = bActive;
	s_bTouching = false;
	s_iAccumY = 0;
	s_iPendingTaps = 0;
}

bool VitaTouch::Enabled()
{
	return g_bVitaTouchControls.Get();
}

bool VitaTouch::InputActive()
{
	return s_bPlayActive && Enabled();
}

bool VitaTouch::ZoneDown( int iZone )
{
	if( iZone < 0 || iZone >= NUM_ZONES )
		return false;
	return s_bDown[iZone];
}

void VitaTouch::SetPlayActive( bool bActive )
{
	s_bPlayActive = bActive;
	if( !bActive )
	{
		/* Drop any held lanes so the release events fire immediately. */
		for( int i = 0; i < NUM_ZONES; ++i )
			s_bDown[i] = false;
	}
}

bool VitaTouch::ShouldDrawOverlay()
{
	return InputActive() && g_bVitaTouchOverlay.Get();
}

void VitaTouch::DrawOverlay( int iWidth, int iHeight )
{
#ifdef VITA
	if( iWidth <= 0 || iHeight <= 0 )
		return;

	int iOpacity = g_iVitaTouchOverlayOpacity.Get();
	if( iOpacity <= 0 )
		return;
	if( iOpacity > 100 )
		iOpacity = 100;
	const float fOpacity = (float)iOpacity / 100.0f;

	/* Screen space with a top-left origin. Column-major: glLoadMatrixf. */
	const float fProj[16] =
	{
		2.0f / (float)iWidth, 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / (float)iHeight, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		-1.0f, 1.0f, 0.0f, 1.0f
	};

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( fProj );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMatrixMode( GL_TEXTURE );
	glLoadIdentity();

	glDisable( GL_TEXTURE_2D );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glEnable( GL_BLEND );
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	OverlayVertex verts[NUM_ZONES * 18]; /* three quads per lane */
	int n = 0;

	const float fLaneW = (float)iWidth / (float)NUM_ZONES;
	const float fTop = (float)iHeight * 0.40f;
	const float fBandY = (float)iHeight * 0.72f;
	const float fBandH = (float)iHeight * 0.12f;
	const float fBottom = (float)iHeight - 2.0f;

	for( int i = 0; i < NUM_ZONES; ++i )
	{
		const float x0 = fLaneW * (float)i + 2.0f;
		const float x1 = fLaneW * (float)( i + 1 ) - 2.0f;
		const unsigned char *rgb = s_ZoneColor[i];
		const bool bDown = s_bDown[i];

		/* Lane body: barely visible, just outlines the zone. */
		const unsigned char aBody = (unsigned char)
			( 255.0f * fOpacity * ( bDown ? 0.45f : 0.14f ) );
		PushQuad( verts, n, x0, fTop, x1, fBottom, rgb, aBody );

		/* Receptor band: the part that lines up with the arrow targets. */
		const unsigned char aBand = (unsigned char)
			( 255.0f * fOpacity * ( bDown ? 1.00f : 0.35f ) );
		PushQuad( verts, n, x0, fBandY, x1, fBandY + fBandH, rgb, aBand );

		/* Bottom accent keeps the lane edge readable. */
		const unsigned char aEdge = (unsigned char)
			( 255.0f * fOpacity * ( bDown ? 1.00f : 0.55f ) );
		PushQuad( verts, n, x0, (float)iHeight - 7.0f, x1, fBottom, rgb, aEdge );
	}

	glVertexPointer( 3, GL_FLOAT, (GLsizei)sizeof(OverlayVertex), &verts[0].x );
	glTexCoordPointer( 2, GL_FLOAT, (GLsizei)sizeof(OverlayVertex), &verts[0].u );
	glColorPointer( 4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(OverlayVertex), &verts[0].r );
	glDrawArrays( GL_TRIANGLES, 0, n );

	glDepthMask( GL_TRUE );
#else
	(void)iWidth;
	(void)iHeight;
#endif
}
