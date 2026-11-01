#ifndef VITA_LAUNCHER_UTIL_H
#define VITA_LAUNCHER_UTIL_H

#include <string>

/* Small helpers shared by the pre-game launcher modules. Everything here is
 * engine-free on purpose: the launcher runs before StepMania initialises its
 * file manager, logging, threading or graphics, so none of the engine headers
 * may be pulled in. */
namespace LauncherUtil
{
	/* Writable data root, the same tree the game uses. */
	extern const char *kDataRoot;

	std::string DataPath( const std::string &sRel );

	/* Appends one line to ux0:data/stepmania-5.0/launcher.log. */
	void Log( const char *szFmt, ... );

	bool DirExists( const std::string &sPath );
	bool Mkdirs( const std::string &sPath );
	bool RemoveFile( const std::string &sPath );

	/* Filesystem-safe folder name for a pack. */
	std::string Sanitize( const std::string &sIn );

	std::string FormatBytes( long long iBytes );

	std::string Lower( const std::string &sIn );
}

#endif
