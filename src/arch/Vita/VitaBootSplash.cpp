#include "global.h"
#include "VitaBootSplash.h"
#include "vita_bootstages.h"
#include "vita_bootfont.h"
#include "RageLog.h"
#include "RageSurface.h"

#ifdef VITA
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#include <psp2/gxm.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BOOT_W 960
#define BOOT_H 544

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
	bool s_active = false;
	bool s_glUp = false;

	unsigned s_bgTex = 0;
	unsigned s_stageTex[BOOTSTAGE_COUNT] = { 0 };
	unsigned s_fontTex = 0;

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

	RageSurface *LoadRawSurface( const char *path, int w, int h )
	{
		/* NOTE: plain malloc + new; caller deletes. Pixels are uploaded
		 * to GL immediately, so CPU-cache visibility is irrelevant. */
		uint8_t *px = (uint8_t *)malloc( (size_t)w * h * 4 );
		if( px == NULL )
			return NULL;
		if( !ReadWhole( path, px, w * h * 4 ) )
		{
			free( px );
			return NULL;
		}
		RageSurface *img = CreateSurface(w, h, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		memcpy( img->pixels, px, (size_t)w * h * 4 );
		free( px );
		return img;
	}

	unsigned UploadTexture( RageSurface *img )
	{
		if( img == NULL )
			return 0;
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		/* The GL error flag is sticky: clear it first so only fresh
		 * errors from THIS upload are attributed (a stale flag would
		 * delete a perfectly good texture below). */
		glGetError();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img->w, img->h, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
		GLenum upErr = glGetError();
		int upW = img->w, upH = img->h;
		delete img;
		/* A failed upload must not cache its handle: incomplete textures
		 * sample as garbage/black forever with no error later. Return 0
		 * so the next Present retries (e.g. GXM still busy at boot). */
		if( upErr != GL_NO_ERROR )
		{
			glDeleteTextures(1, &tex);
			if( LOG )
				LOG->Info("VitaBootSplash: upload %dx%d failed, glGetError=0x%x",
					upW, upH, (unsigned)upErr);
			return 0;
		}
		return (unsigned)tex;
	}

	void UploadAll()
	{
		if( s_bgTex == 0 )
			s_bgTex = UploadTexture( LoadRawSurface( "app0:bootart/bg.raw", 960, 544 ) );
		for( int i = 0; i < VITA_BOOT_STAGE_COUNT && i < BOOTSTAGE_COUNT; ++i )
		{
			if( s_stageTex[i] != 0 )
				continue;
			int w = VITA_BOOT_STAGE_ARTS[i].w, h = VITA_BOOT_STAGE_ARTS[i].h;
			s_stageTex[i] = UploadTexture( LoadRawSurface( VITA_BOOT_STAGE_ARTS[i].file, w, h ) );
		}
		if( s_fontTex == 0 )
		{
			/* Font atlas: 95 cells of 16x16 in one 1520x16 texture. */
			uint8_t *px = (uint8_t *)malloc( 95 * 16 * 16 * 4 );
			if( px != NULL && ReadWhole( "app0:bootart/font16.raw", px, 95 * 16 * 16 * 4 ) )
			{
				RageSurface *img = CreateSurface(95 * 16, 16, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
				for( int ch = 0; ch < 95; ++ch )
					for( int r = 0; r < 16; ++r )
						memcpy( img->pixels + ((size_t)r * 95 * 16 + (size_t)ch * 16) * 4,
							px + ((size_t)ch * 256 + (size_t)r * 16) * 4, 16 * 4 );
				free( px );
				s_fontTex = UploadTexture( img );
			}
			else
			{
				free( px );
			}
		}
	}

	void SetupOrtho()
	{
		glViewport(0, 0, BOOT_W, BOOT_H);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, BOOT_W, BOOT_H, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
	}

	/* Textured quad, y-down coords, v=0 at image top. Blended iff blend. */
	void DrawTexQuad( unsigned tex, float x0, float y0, float w, float h, bool blend )
	{
		float pos[6 * 3] = { x0,y0,0,  x0+w,y0,0,  x0+w,y0+h,0,  x0,y0,0,  x0+w,y0+h,0,  x0,y0+h,0 };
		static const float tc[6 * 2] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
		if( blend )
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else
		{
			glDisable(GL_BLEND);
		}
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
		glVertexPointer(3, GL_FLOAT, 0, pos);
		glTexCoordPointer(2, GL_FLOAT, 0, tc);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	struct FlatQ { float x0, y0, x1, y1; uint8_t r, g, b; };

	/* Batched opaque flat quads (single draw). */
	void DrawFlats( const FlatQ *qs, int nq )
	{
		float P[44 * 6 * 3];
		uint8_t C[44 * 6 * 4];
		if( nq > 44 ) nq = 44;
		int nv = 0;
		for( int q = 0; q < nq; ++q )
		{
			float *pp = &P[nv * 3];
			uint8_t *cc = &C[nv * 4];
			float xs[6] = { qs[q].x0, qs[q].x1, qs[q].x1, qs[q].x0, qs[q].x1, qs[q].x0 };
			float ys[6] = { qs[q].y0, qs[q].y0, qs[q].y1, qs[q].y0, qs[q].y1, qs[q].y1 };
			for( int k = 0; k < 6; ++k )
			{
				pp[k * 3 + 0] = xs[k]; pp[k * 3 + 1] = ys[k]; pp[k * 3 + 2] = 0;
				cc[k * 4 + 0] = qs[q].r; cc[k * 4 + 1] = qs[q].g; cc[k * 4 + 2] = qs[q].b; cc[k * 4 + 3] = 255;
			}
			nv += 6;
		}
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, C);
		glDrawArrays(GL_TRIANGLES, 0, nv);
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

	/* One batched white-text line via the font atlas (blended). */
	void RestoreState()
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnableClientState(GL_COLOR_ARRAY);
	}

	/* Word-wrap ASCII text to pixel width using the baked font. */
	int WrapLines( const char *text, char lines[][128], int maxLines, int maxPx )
	{
		int n = 0;
		const char *p = text;
		char cur[256];
		while( *p && n < maxLines )
		{
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

bool VitaBootSplash::IsGlUp()
{
#ifdef VITA
	return s_glUp;
#else
	return false;
#endif
}

bool VitaBootSplash::Init()
{
#ifdef VITA
	if( s_active )
		return true;
	/* Display first: GXM comes up here at process start, so every later
	 * phase (extraction, engine boot, screen loads) draws over this. */
	vglInitExtended(0x1000000, BOOT_W, BOOT_H, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);
	vglWaitVblankStart(GL_TRUE);
	s_glUp = true;
	UploadAll();
	s_active = true;
	Present();
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
	if( s_bgTex != 0 ) { GLuint t = (GLuint)s_bgTex; glDeleteTextures(1, &t); s_bgTex = 0; }
	for( int i = 0; i < BOOTSTAGE_COUNT; ++i )
	{
		if( s_stageTex[i] != 0 ) { GLuint t = (GLuint)s_stageTex[i]; glDeleteTextures(1, &t); s_stageTex[i] = 0; }
	}
	if( s_fontTex != 0 ) { GLuint t = (GLuint)s_fontTex; glDeleteTextures(1, &t); s_fontTex = 0; }
	/* NOTE: GXM itself stays up (LowLevelWindow keeps rendering). */
#endif
}

void VitaBootSplash::SetStage( VitaBootStage s )
{
#ifdef VITA
	if( s < 0 || s >= BOOTSTAGE_COUNT )
		s = BOOTSTAGE_LOADING;
	s_stage = s;
	/* State first: callers after Shutdown read it back for GL presents. */
	if( !s_active )
		return;
	Present();
#endif
}

void VitaBootSplash::SetProgress( int done, int total )
{
#ifdef VITA
	s_done = done;
	s_total = total;
	if( !s_active )
		return;
	Present();
#endif
}

void VitaBootSplash::SetSubLine( const char *text )
{
#ifdef VITA
	if( text == NULL )
		text = "";
	snprintf( s_sub, sizeof(s_sub), "%s", text );
	/* Single line: keep the tail (song name matters more than prefix). */
	size_t len = strlen( s_sub );
	if( len >= 70 )
		memmove( s_sub, s_sub + len - 69, 70 );
	if( !s_active )
		return;
	Present();
#endif
}

void VitaBootSplash::Present()
{
#ifdef VITA
	if( !s_active || !s_glUp )
		return;
	UploadAll();

	int stage = (int)s_stage;
	if( stage < 0 || stage >= BOOTSTAGE_COUNT )
		stage = 0;

	SetupOrtho();

	/* Background (opaque). Skipped if the upload is still failing, but
	 * the flat dialog below draws regardless. */
	if( s_bgTex != 0 )
		DrawTexQuad( s_bgTex, 0, 0, BOOT_W, BOOT_H, false );

	/* Brown dialog + bar slot + XP blocks, one flat batch. */
	{
		FlatQ qs[44];
		int nq = 0;
		qs[nq++] = { 0, 216, 960, 328, 110, 66, 28 };
		qs[nq++] = { 0, 216, 960, 219, 58, 33, 12 };
		qs[nq++] = { 0, 325, 960, 328, 58, 33, 12 };
		qs[nq++] = { 230, 284, 730, 306, 40, 22, 8 };
		int filled = 0;
		if( s_total > 0 )
		{
			int done = s_done;
			if( done < 0 ) done = 0;
			if( done > s_total ) done = s_total;
			filled = (int)((long long)done * BAR_BLOCKS / s_total);
		}
		int avail = (BAR_X1 - BAR_X0) - (BAR_BLOCKS - 1) * BAR_GAP;
		int bw = avail / BAR_BLOCKS;
		int x = BAR_X0 + (avail - bw * BAR_BLOCKS) / 2;
		for( int i = 0; i < BAR_BLOCKS && nq + 2 <= 44; ++i )
		{
			if( i < filled )
			{
				qs[nq++] = { (float)x, 286, (float)(x + bw), 304, 255, 138, 0 };
				qs[nq++] = { (float)x, 286, (float)(x + bw), 289, 255, 190, 90 };
			}
			x += bw + BAR_GAP;
		}
		DrawFlats( qs, nq );
	}

	/* Stage label. */
	if( (int)s_stage >= 0 && (int)s_stage < BOOTSTAGE_COUNT &&
		s_stageTex[s_stage] != 0 &&
		(int)s_stage < VITA_BOOT_STAGE_COUNT )
	{
		int w = VITA_BOOT_STAGE_ARTS[s_stage].w, h = VITA_BOOT_STAGE_ARTS[s_stage].h;
		DrawTexQuad( s_stageTex[s_stage], (BOOT_W - w) / 2.0f, STAGE_BOTTOM - h, (float)w, (float)h, true );
	}

	/* Sub-line (song names). */
	if( s_fontTex != 0 && s_sub[0] != '\0' )
	{
		float P[70 * 6 * 3];
		float T[70 * 6 * 2];
		int nv = 0;
		int totalW = 0;
		const char *t = s_sub;
		int count = 0;
		while( *t && count < 69 )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			totalW += VITA_BOOTFONT_ADV[c - 32];
			++count;
		}
		float x = (BOOT_W - totalW) / 2.0f;
		t = s_sub;
		count = 0;
		while( *t && count < 69 )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			int adv = VITA_BOOTFONT_ADV[c - 32];
			float u0 = ((c - 32) * 16) / 1520.0f, u1 = ((c - 32) * 16 + 16) / 1520.0f;
			float xs[6] = { x, x + 16, x + 16, x, x + 16, x };
			float ys[6] = { (float)SUB_Y, (float)SUB_Y, (float)(SUB_Y + 16), (float)SUB_Y, (float)(SUB_Y + 16), (float)(SUB_Y + 16) };
			float us[6] = { u0, u1, u1, u0, u1, u0 };
			float vs[6] = { 0, 0, 1, 0, 1, 1 };
			for( int k = 0; k < 6; ++k )
			{
				P[(nv + k) * 3 + 0] = xs[k]; P[(nv + k) * 3 + 1] = ys[k]; P[(nv + k) * 3 + 2] = 0;
				T[(nv + k) * 2 + 0] = us[k]; T[(nv + k) * 2 + 1] = vs[k];
			}
			nv += 6;
			x += adv;
			++count;
		}
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_fontTex);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glTexCoordPointer(2, GL_FLOAT, 0, T);
		glDrawArrays(GL_TRIANGLES, 0, nv);
	}

	RestoreState();
	glFlush();
	vglSwapBuffers(GL_TRUE);

	if( LOG )
	{
		/* vitaGL logo-splash state (extern internal): 1 while the logo
		 * owns the display queue (our swaps are swallowed), 0 after our
		 * first draw kills it. If early presents stay black, this tells
		 * whether the logo is the swallower. */
		extern GLboolean is_splashscreen_active;
		int st = 0;
		for( int i = 0; i < BOOTSTAGE_COUNT; ++i )
			if( s_stageTex[i] != 0 )
				++st;
		LOG->Info("VitaBootSplash: present stage=%d (%s) %d/%d bg=%u st=%d font=%u logo=%d glGetError=0x%x",
			stage, VITA_BOOT_STAGE_ARTS[stage].id, s_done, s_total,
			s_bgTex, st, s_fontTex, (int)is_splashscreen_active, (unsigned)glGetError());
	}
#endif
}

void VitaBootSplash::ShowFatal( const char *message )
{
#ifdef VITA
	if( !s_glUp && !Init() )
		return;
	/* Textures may have been freed by Shutdown: re-upload what's missing. */
	UploadAll();
	if( !s_active )
	{
		/* Fatal after Shutdown (post-display): re-arm drawing only. */
		s_active = true;
	}
	if( message == NULL )
		message = "unknown error";

	SetupOrtho();
	if( s_bgTex != 0 )
		DrawTexQuad( s_bgTex, 0, 0, BOOT_W, BOOT_H, false );
	else
	{
		FlatQ q = { 0, 0, BOOT_W, BOOT_H, 16, 10, 6 };
		DrawFlats( &q, 1 );
	}

	{
		FlatQ box[3] = {
			{ 0, ERR_BOX_TOP, BOOT_W, ERR_BOX_BOT, 110, 66, 28 },
			{ 0, ERR_BOX_TOP, BOOT_W, ERR_BOX_TOP + 3, 58, 33, 12 },
			{ 0, ERR_BOX_BOT - 3, BOOT_W, ERR_BOX_BOT, 58, 33, 12 },
		};
		DrawFlats( box, 3 );
	}

	if( s_stageTex[BOOTSTAGE_ERROR] != 0 && BOOTSTAGE_ERROR < VITA_BOOT_STAGE_COUNT )
	{
		int w = VITA_BOOT_STAGE_ARTS[BOOTSTAGE_ERROR].w, h = VITA_BOOT_STAGE_ARTS[BOOTSTAGE_ERROR].h;
		DrawTexQuad( s_stageTex[BOOTSTAGE_ERROR], (BOOT_W - w) / 2.0f, (float)(ERR_BOX_TOP + 12), (float)w, (float)h, true );
	}

	char lines[ERR_MAX_BODY + 2][128];
	int count = WrapLines( message, lines, 4, BOOT_W - 80 );
	ReadLogTail( lines, count, ERR_MAX_BODY );
	for( int i = 0; i < count; ++i )
	{
		int w = 0;
		const char *t = lines[i];
		while( *t )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			w += VITA_BOOTFONT_ADV[c - 32];
		}
		float P[120 * 3];
		float T[120 * 2];
		int nv = 0;
		float x = (BOOT_W - w) / 2.0f;
		t = lines[i];
		while( *t && nv + 6 <= 120 )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			int adv = VITA_BOOTFONT_ADV[c - 32];
			float u0 = ((c - 32) * 16) / 1520.0f, u1 = ((c - 32) * 16 + 16) / 1520.0f;
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP + 16); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 1;
			++nv;
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP + 16); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 1;
			++nv;
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = (float)(ERR_BODY_Y + i * ERR_LINE_STEP + 16); P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 1;
			++nv;
			x += adv;
		}
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_fontTex);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glTexCoordPointer(2, GL_FLOAT, 0, T);
		glDrawArrays(GL_TRIANGLES, 0, nv);
	}
	{
		const char *hint = "press X to exit";
		int w = 0;
		for( const char *t = hint; *t; ++t )
			w += VITA_BOOTFONT_ADV[(unsigned char)*t - 32];
		float P[120 * 3];
		float T[120 * 2];
		int nv = 0;
		float x = (BOOT_W - w) / 2.0f;
		for( const char *t = hint; *t && nv + 6 <= 120; ++t )
		{
			unsigned char c = (unsigned char)*t;
			int adv = VITA_BOOTFONT_ADV[c - 32];
			float u0 = ((c - 32) * 16) / 1520.0f, u1 = ((c - 32) * 16 + 16) / 1520.0f;
			float y = (float)(ERR_BOX_BOT - 30);
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = y; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = y; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = y + 16; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 1;
			++nv;
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = y; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 0;
			++nv;
			P[nv * 3 + 0] = x + 16; P[nv * 3 + 1] = y + 16; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u1; T[nv * 2 + 1] = 1;
			++nv;
			P[nv * 3 + 0] = x; P[nv * 3 + 1] = y + 16; P[nv * 3 + 2] = 0;
			T[nv * 2 + 0] = u0; T[nv * 2 + 1] = 1;
			++nv;
			x += adv;
		}
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 0.55f, 0.15f, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_fontTex);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glTexCoordPointer(2, GL_FLOAT, 0, T);
		glDrawArrays(GL_TRIANGLES, 0, nv);
	}

	RestoreState();
	glFlush();
	vglSwapBuffers(GL_TRUE);

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
