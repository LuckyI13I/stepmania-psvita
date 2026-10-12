#ifndef VITA_BOOTSPLASH_H
#define VITA_BOOTSPLASH_H

/* PS Vita pre-display boot splash + fatal error screen.
 *
 * Raw sceDisplay framebuffer output: needs no GL, no FILEMAN, no LOG, so it
 * works from ArchHooks::Init() (data.zip extraction) through SONGMAN loading
 * and in the top-level exception handler. Art + pre-rendered stage labels are
 * plain raw-RGBA files on app0: (see build-bootart-assets.py).
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
	BOOTSTAGE_ERROR,
	BOOTSTAGE_COUNT
};

class VitaBootSplash
{
public:
	static bool Init();		/* set up framebuffer, load art; safe twice */
	static void Shutdown();		/* release buffers; call before vglInit */
	static bool IsActive();
	static void SetStage( VitaBootStage s );
	static void SetProgress( int done, int total ); /* total<=0: indeterminate */
	static void SetSubLine( const char *text );	/* small line under bar */
	static void ShowFatal( const char *message );	/* error screen, waits X/Start */
};

#endif
