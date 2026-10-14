#ifndef VITA_BOOTSPLASH_H
#define VITA_BOOTSPLASH_H

/* PS Vita boot splash + fatal error screen, rendered through vitaGL.
 *
 * vitaGL is initialized here at ArchHooks::Init time (the very first
 * thing the app does), so this splash covers the ENTIRE boot: data.zip
 * extraction, engine init, song loading and the synchronous screen
 * loads. Raw framebuffer output is deliberately NOT used (invisible on
 * real hardware due to CPU cache vs display controller).
 *
 * Needs no FILEMAN/LOG/TEXTUREMAN (raw sceIo reads + raw GL), so it
 * works from HOOKS->Init through the top-level exception handler.
 */

enum VitaBootStage
{
	BOOTSTAGE_LOADING = 0,
	BOOTSTAGE_EXTRACT,
	BOOTSTAGE_THEME,
	BOOTSTAGE_SOUND,
	BOOTSTAGE_SONGS,
	BOOTSTAGE_COURSES,
	BOOTSTAGE_PROFILES,
	BOOTSTAGE_DISPLAY,
	BOOTSTAGE_SCREENS,
	BOOTSTAGE_MENU,
	BOOTSTAGE_ERROR,
	BOOTSTAGE_COUNT
};

class VitaBootSplash
{
public:
	static bool Init();		/* vglInit + upload art; safe twice */
	static void Shutdown();		/* release splash textures (GXM stays) */
	static bool IsActive();
	static bool IsGlUp();		/* true once vglInit ran */
	static void SetStage( VitaBootStage s );
	static void SetProgress( int done, int total ); /* total<=0: indeterminate */
	static void SetSubLine( const char *text );	/* small line under bar */
	static void Present();		/* redraw current state now */
	static void ShowFatal( const char *message );	/* error screen, waits X/Start */

	/* Read-only state (used internally). */
	static int GetStage();
	static int GetDone();
	static int GetTotal();
	static const char *GetSubLine();
};

#endif
