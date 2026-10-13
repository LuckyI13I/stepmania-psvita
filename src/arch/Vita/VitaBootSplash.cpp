#include "VitaBootSplash.h"
#include "vita_bootstages.h"
#include "vita_bootfont.h"

#ifdef VITA
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BOOT_W 960
#define BOOT_H 544
#define BOOT_PITCH 960

/* Brown dialog box: wall to wall, vertically centered. */
#define BOX_TOP 216
#define BOX_BOT 328
#define BOX_R 110
#define BOX_G 66
#define BOX_B 28
#define EDGE_R 58
#define EDGE_G 33
#define EDGE_B 12
#define EDGE_W 3

/* XP-style segmented bar (orange blocks). */
#define BAR_X0 230
#define BAR_X1 730
#define BAR_Y0 284
#define BAR_Y1 306
#define BAR_BLOCKS 20
#define BAR_GAP 4
#define SLOT_R 40
#define SLOT_G 22
#define SLOT_B 8
#define BLOCK_R 255
#define BLOCK_G 138
#define BLOCK_B 0
#define BLOCK_HI_R 255
#define BLOCK_HI_G 190
#define BLOCK_HI_B 90

#define STAGE_BOTTOM 255 /* stage sprite bottom edge (bar sits below it) */
#define SUB_Y 309	 /* dynamic sub-line (song names), 16px font */

#define ERR_BOX_TOP 96
#define ERR_BOX_BOT 448
#define ERR_BODY_Y 150
#define ERR_LINE_STEP 20
#define ERR_MAX_BODY 13

namespace
{
#ifdef VITA
	struct Pix { uint8_t r, g, b, a; };

	bool s_active = false;
	uint8_t *s_fb[2] = { NULL, NULL };
	int s_back = 0;
	uint8_t *s_bg = NULL; /* 960x544 RGBA */
	struct Sprite { uint8_t *px; int w, h; };
	Sprite s_stages[BOOTSTAGE_COUNT];
	uint8_t *s_font = NULL; /* 95 cells of 16x16 RGBA white w/alpha */

	VitaBootStage s_stage = BOOTSTAGE_LOADING;
	int s_done = 0;
	int s_total = 0;
	char s_sub[128];

	bool ReadWhole( const char *path, uint8_t *dst, int size )
	{
		SceUID fd = sceIoOpen( path, SCE_O_RDONLY, 0 );
		if( fd < 0 )
			return false;
		int left = size;
		uint8_t *p = dst;
		while( left > 0 )
		{
			int n = sceIoRead( fd, p, (SceSize)left );
			if( n <= 0 )
				break;
			p += n;
			left -= n;
		}
		sceIoClose( fd );
		return left == 0;
	}

	void Present()
	{
		SceDisplayFrameBuf fb;
		memset( &fb, 0, sizeof(fb) );
		fb.size = sizeof(fb);
		fb.base = s_fb[s_back];
		fb.pitch = BOOT_PITCH;
		fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
		fb.width = BOOT_W;
		fb.height = BOOT_H;
		sceDisplaySetFrameBuf( &fb, SCE_DISPLAY_SETBUF_NEXTFRAME );
		s_back ^= 1;
	}

	inline uint8_t *Row( int y ) { return s_fb[s_back] + (size_t)y * BOOT_PITCH * 4; }

	void FillRect( int x0, int y0, int x1, int y1, int r, int g, int b )
	{
		if( x0 < 0 ) x0 = 0;
		if( y0 < 0 ) y0 = 0;
		if( x1 > BOOT_W ) x1 = BOOT_W;
		if( y1 > BOOT_H ) y1 = BOOT_H;
		for( int y = y0; y < y1; ++y )
		{
			uint8_t *p = Row( y ) + (size_t)x0 * 4;
			for( int x = x0; x < x1; ++x )
			{
				p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255;
				p += 4;
			}
		}
	}

	/* Alpha-blend raw RGBA sprite onto the back buffer. */
	void Blit( int dx, int dy, const uint8_t *src, int w, int h )
	{
		for( int y = 0; y < h; ++y )
		{
			int yy = dy + y;
			if( yy < 0 || yy >= BOOT_H )
				continue;
			uint8_t *d = Row( yy );
			const uint8_t *s = src + (size_t)y * w * 4;
			for( int x = 0; x < w; ++x )
			{
				int xx = dx + x;
				if( xx < 0 || xx >= BOOT_W )
				{
					s += 4;
					continue;
				}
				uint8_t *p = d + (size_t)xx * 4;
				int a = s[3];
				if( a == 255 )
				{
					p[0] = s[0]; p[1] = s[1]; p[2] = s[2]; p[3] = 255;
				}
				else if( a != 0 )
				{
					p[0] = (uint8_t)((s[0] * a + p[0] * (255 - a)) / 255);
					p[1] = (uint8_t)((s[1] * a + p[1] * (255 - a)) / 255);
					p[2] = (uint8_t)((s[2] * a + p[2] * (255 - a)) / 255);
					p[3] = 255;
				}
				s += 4;
			}
		}
	}

	int TextWidth( const char *t )
	{
		int w = 0;
		for( ; *t; ++t )
		{
			unsigned char c = (unsigned char)*t;
			if( c < VITA_BOOTFONT_FIRST || c > VITA_BOOTFONT_LAST )
				c = '?';
			w += VITA_BOOTFONT_ADV[c - VITA_BOOTFONT_FIRST];
		}
		return w;
	}

	void DrawText( int x, int y, const char *t, int r, int g, int b )
	{
		if( s_font == NULL )
			return;
		for( ; *t; ++t )
		{
			unsigned char c = (unsigned char)*t;
			if( c < VITA_BOOTFONT_FIRST || c > VITA_BOOTFONT_LAST )
				c = '?';
			const uint8_t *cell = s_font + (size_t)(c - VITA_BOOTFONT_FIRST) * 256 * 4;
			/* Tint the white cell, keep its alpha. */
			for( int yy = 0; yy < 16; ++yy )
			{
				int dyy = y + yy;
				if( dyy < 0 || dyy >= BOOT_H )
					continue;
				uint8_t *d = Row( dyy );
				const uint8_t *s = cell + (size_t)yy * 16 * 4;
				for( int xx = 0; xx < 16; ++xx )
				{
					int dxx = x + xx;
					if( dxx < 0 || dxx >= BOOT_W )
					{
						s += 4;
						continue;
					}
					int a = s[3];
					if( a != 0 )
					{
						uint8_t *p = d + (size_t)dxx * 4;
						int sr = (r * s[0]) / 255, sg = (g * s[1]) / 255, sb = (b * s[2]) / 255;
						p[0] = (uint8_t)((sr * a + p[0] * (255 - a)) / 255);
						p[1] = (uint8_t)((sg * a + p[1] * (255 - a)) / 255);
						p[2] = (uint8_t)((sb * a + p[2] * (255 - a)) / 255);
						p[3] = 255;
					}
					s += 4;
				}
			}
			x += VITA_BOOTFONT_ADV[c - VITA_BOOTFONT_FIRST];
		}
	}

	void DrawTextCentered( int y, const char *t, int r, int g, int b )
	{
		int w = TextWidth( t );
		DrawText( (BOOT_W - w) / 2, y, t, r, g, b );
	}

	void DrawBox( int top, int bot )
	{
		FillRect( 0, top, BOOT_W, bot, BOX_R, BOX_G, BOX_B );
		FillRect( 0, top, BOOT_W, top + EDGE_W, EDGE_R, EDGE_G, EDGE_B );
		FillRect( 0, bot - EDGE_W, BOOT_W, bot, EDGE_R, EDGE_G, EDGE_B );
	}

	void Redraw()
	{
		if( s_bg != NULL )
			memcpy( s_fb[s_back], s_bg, (size_t)BOOT_W * BOOT_H * 4 );
		else
		{
			/* Fallback: dark gradient if art is missing. */
			for( int y = 0; y < BOOT_H; ++y )
			{
				uint8_t *p = Row( y );
				int v = 8 + (y * 20) / BOOT_H;
				for( int x = 0; x < BOOT_W; ++x )
				{
					p[0] = (uint8_t)v; p[1] = (uint8_t)(v * 3 / 4); p[2] = 0; p[3] = 255;
					p += 4;
				}
			}
		}

		DrawBox( BOX_TOP, BOX_BOT );

		/* Stage label (pre-rendered slkscr sprite). */
		if( (int)s_stage >= 0 && (int)s_stage < BOOTSTAGE_COUNT && s_stages[s_stage].px != NULL )
		{
			Sprite &sp = s_stages[s_stage];
			Blit( (BOOT_W - sp.w) / 2, STAGE_BOTTOM - sp.h, sp.px, sp.w, sp.h );
		}

		/* XP-style segmented bar. */
		FillRect( BAR_X0, BAR_Y0, BAR_X1, BAR_Y1, SLOT_R, SLOT_G, SLOT_B );
		int filled = 0;
		if( s_total > 0 )
		{
			if( s_done < 0 ) s_done = 0;
			if( s_done > s_total ) s_done = s_total;
			filled = (int)((long long)s_done * BAR_BLOCKS / s_total);
		}
		int avail = (BAR_X1 - BAR_X0) - (BAR_BLOCKS - 1) * BAR_GAP;
		int bw = avail / BAR_BLOCKS;
		int x = BAR_X0 + (avail - bw * BAR_BLOCKS) / 2;
		for( int i = 0; i < BAR_BLOCKS; ++i )
		{
			if( i < filled )
			{
				FillRect( x, BAR_Y0 + 2, x + bw, BAR_Y1 - 2, BLOCK_R, BLOCK_G, BLOCK_B );
				FillRect( x, BAR_Y0 + 2, x + bw, BAR_Y0 + 5, BLOCK_HI_R, BLOCK_HI_G, BLOCK_HI_B );
			}
			x += bw + BAR_GAP;
		}

		/* Dynamic sub-line (song names). */
		if( s_sub[0] != '\0' )
			DrawTextCentered( SUB_Y, s_sub, 255, 255, 255 );

		Present();
	}

	/* Word-wrap ASCII text to pixel width using the baked font. */
	int WrapLines( const char *text, char lines[][128], int maxLines, int maxPx )
	{
		int n = 0;
		const char *p = text;
		char cur[256];
		while( *p && n < maxLines )
		{
			/* Next paragraph (split on \n). */
			const char *nl = strchr( p, '\n' );
			size_t plen = nl ? (size_t)(nl - p) : strlen( p );
			if( plen >= sizeof(cur) )
				plen = sizeof(cur) - 1;
			memcpy( cur, p, plen );
			cur[plen] = '\0';
			p += plen;
			if( *p == '\n' )
				++p;
			if( cur[0] == '\0' )
				continue;

			/* Greedy wrap by words. */
			char *word = cur;
			char line[128] = "";
			while( *word && n < maxLines )
			{
				while( *word == ' ' )
					++word;
				if( *word == '\0' )
					break;
				char *end = word;
				while( *end && *end != ' ' )
					++end;
				char save = *end;
				*end = '\0';
				char trial[128];
				if( line[0] )
					snprintf( trial, sizeof(trial), "%s %s", line, word );
				else
					snprintf( trial, sizeof(trial), "%s", word );
				if( TextWidth( trial ) > maxPx && line[0] )
				{
					snprintf( lines[n], 128, "%s", line );
					++n;
					snprintf( line, sizeof(line), "%s", word );
				}
				else
				{
					snprintf( line, sizeof(line), "%s", trial );
				}
				*end = save;
				word = (*end) ? end + 1 : end;
			}
			if( line[0] && n < maxLines )
			{
				snprintf( lines[n], 128, "%s", line );
				++n;
			}
		}
		return n;
	}

	void ReadLogTail( char lines[][128], int &count, int maxLines )
	{
		SceUID fd = sceIoOpen( "ux0:data/stepmania-5.0/Logs/log.txt", SCE_O_RDONLY, 0 );
		if( fd < 0 )
			return;
		long size = (long)sceIoLseek( fd, 0, SCE_SEEK_END );
		long start = size > 4096 ? size - 4096 : 0;
		sceIoLseek( fd, start, SCE_SEEK_SET );
		static char buf[4096];
		int n = sceIoRead( fd, buf, (SceSize)(size - start) );
		sceIoClose( fd );
		if( n <= 0 )
			return;
		buf[n] = '\0';
		/* Last maxLines non-empty lines. */
		int total = 0;
		for( char *p = buf; *p; ++p )
			if( *p == '\n' )
				++total;
		int skip = total > maxLines ? total - maxLines : 0;
		int seen = 0;
		char *line = buf;
		while( *line && count < maxLines )
		{
			char *nl = strchr( line, '\n' );
			if( nl )
				*nl = '\0';
			if( line[0] != '\0' )
			{
				if( seen >= skip )
				{
					snprintf( lines[count], 128, "%s", line + (strlen( line ) > 100 ? strlen( line ) - 100 : 0) );
					++count;
				}
				++seen;
			}
			if( !nl )
				break;
			line = nl + 1;
		}
	}
#endif
}

bool VitaBootSplash::IsActive()
{
#ifdef VITA
	return s_active;
#else
	return false;
#endif
}

bool VitaBootSplash::Init()
{
#ifdef VITA
	if( s_active )
		return true;
	for( int i = 0; i < 2; ++i )
	{
		s_fb[i] = (uint8_t *)malloc( (size_t)BOOT_W * BOOT_H * 4 );
		if( s_fb[i] == NULL )
		{
			for( int j = 0; j < i; ++j )
				free( s_fb[j] );
			return false;
		}
		memset( s_fb[i], 0, (size_t)BOOT_W * BOOT_H * 4 );
	}

	s_bg = (uint8_t *)malloc( (size_t)BOOT_W * BOOT_H * 4 );
	if( s_bg != NULL )
	{
		if( !ReadWhole( "app0:bootart/bg.raw", s_bg, BOOT_W * BOOT_H * 4 ) )
		{
			free( s_bg );
			s_bg = NULL;
		}
	}
	for( int i = 0; i < VITA_BOOT_STAGE_COUNT && i < BOOTSTAGE_COUNT; ++i )
	{
		int px = VITA_BOOT_STAGE_ARTS[i].w * VITA_BOOT_STAGE_ARTS[i].h * 4;
		s_stages[i].px = (uint8_t *)malloc( (size_t)px );
		s_stages[i].w = VITA_BOOT_STAGE_ARTS[i].w;
		s_stages[i].h = VITA_BOOT_STAGE_ARTS[i].h;
		if( s_stages[i].px == NULL || !ReadWhole( VITA_BOOT_STAGE_ARTS[i].file, s_stages[i].px, px ) )
		{
			free( s_stages[i].px );
			s_stages[i].px = NULL;
		}
	}
	s_font = (uint8_t *)malloc( 95 * 16 * 16 * 4 );
	if( s_font != NULL )
	{
		if( !ReadWhole( "app0:bootart/font16.raw", s_font, 95 * 16 * 16 * 4 ) )
		{
			free( s_font );
			s_font = NULL;
		}
	}

	s_active = true;
	Redraw();
	return true;
#else
	return false;
#endif
}

void VitaBootSplash::Shutdown()
{
#ifdef VITA
	if( !s_active )
		return;
	s_active = false;
	s_done = 0;
	s_total = 0;
	s_sub[0] = '\0';
	for( int i = 0; i < 2; ++i )
	{
		free( s_fb[i] );
		s_fb[i] = NULL;
	}
	free( s_bg );
	s_bg = NULL;
	for( int i = 0; i < BOOTSTAGE_COUNT; ++i )
	{
		free( s_stages[i].px );
		s_stages[i].px = NULL;
	}
	free( s_font );
	s_font = NULL;
#endif
}

void VitaBootSplash::SetStage( VitaBootStage s )
{
#ifdef VITA
	if( !s_active )
		return;
	if( s < 0 || s >= BOOTSTAGE_COUNT )
		s = BOOTSTAGE_LOADING;
	s_stage = s;
	Redraw();
#endif
}

void VitaBootSplash::SetProgress( int done, int total )
{
#ifdef VITA
	if( !s_active )
		return;
	s_done = done;
	s_total = total;
	Redraw();
#endif
}

void VitaBootSplash::SetSubLine( const char *text )
{
#ifdef VITA
	if( !s_active )
		return;
	if( text == NULL )
		text = "";
	snprintf( s_sub, sizeof(s_sub), "%s", text );
	/* Single line: keep the tail (song name matters more than prefix). */
	size_t len = strlen( s_sub );
	if( len >= 70 )
		memmove( s_sub, s_sub + len - 69, 70 );
	Redraw();
#endif
}

void VitaBootSplash::ShowFatal( const char *message )
{
#ifdef VITA
	if( !s_active && !Init() )
		return;
	if( message == NULL )
		message = "unknown error";

	if( s_bg != NULL )
		memcpy( s_fb[s_back], s_bg, (size_t)BOOT_W * BOOT_H * 4 );
	else
		FillRect( 0, 0, BOOT_W, BOOT_H, 16, 10, 6 );

	DrawBox( ERR_BOX_TOP, ERR_BOX_BOT );

	/* Title. */
	if( s_stages[BOOTSTAGE_ERROR].px != NULL )
	{
		Sprite &sp = s_stages[BOOTSTAGE_ERROR];
		Blit( (BOOT_W - sp.w) / 2, ERR_BOX_TOP + 12, sp.px, sp.w, sp.h );
	}
	else
	{
		DrawTextCentered( ERR_BOX_TOP + 12, "error", 255, 120, 60 );
	}

	char lines[ERR_MAX_BODY + 2][128];
	int count = 0;
	count = WrapLines( message, lines, 4, BOOT_W - 80 );
	ReadLogTail( lines, count, ERR_MAX_BODY );
	for( int i = 0; i < count; ++i )
		DrawText( 40, ERR_BODY_Y + i * ERR_LINE_STEP, lines[i], 235, 235, 235 );
	DrawTextCentered( ERR_BOX_BOT - 30, "press X to exit", 255, 138, 0 );
	Present();

	sceCtrlSetSamplingMode( SCE_CTRL_MODE_DIGITAL );
	sceKernelDelayThread( 500 * 1000 ); /* let the pad settle */
	for( ;; )
	{
		SceCtrlData pad;
		memset( &pad, 0, sizeof(pad) );
		if( sceCtrlPeekBufferPositive( 0, &pad, 1 ) >= 0 )
		{
			if( pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START) )
				break;
		}
		sceKernelDelayThread( 50 * 1000 );
	}
#endif
}

int VitaBootSplash::GetStage()
{
#ifdef VITA
	return (int)s_stage;
#else
	return 0;
#endif
}

int VitaBootSplash::GetDone()
{
#ifdef VITA
	return s_done;
#else
	return 0;
#endif
}

int VitaBootSplash::GetTotal()
{
#ifdef VITA
	return s_total;
#else
	return 0;
#endif
}

const char *VitaBootSplash::GetSubLine()
{
#ifdef VITA
	return s_sub;
#else
	return "";
#endif
}
