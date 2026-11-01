#ifndef VITA_LAUNCHER_H
#define VITA_LAUNCHER_H

/* Pack downloader.
 *
 * StepMania always boots straight into the game; the downloader only runs when
 * the app is started from the LiveArea "Download Song Packs" tile. That tile is
 * declared in sce_sys/livearea/contents/template.xml as
 * <target>psla:packs</target>, and the launch argument "packs" is handed back
 * to us by sceAppUtilReceiveAppEvent()/sceAppUtilAppEventParseLiveArea().
 *
 * The UI lives in front of the engine (no RageFile, no vitaGL, no song scan),
 * so nothing heavy has to be torn down before sm_main() runs; the downloader
 * releases vita2d and the whole network stack when it closes. */
namespace VitaLauncher
{
	/* True when this launch asked for the downloader (LiveArea tile, or
	 * "-packs" on the command line). */
	bool WantsPackBrowser( int argc, char **argv );

	/* Runs the downloader until the player closes it. */
	void RunPackBrowser();
}

#endif
