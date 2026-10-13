#include "global.h"
#include "ArchHooks_Vita.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageFileManager.h"

#ifdef VITA
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "miniz.h"
#endif
#if defined(VITA)
#include "VitaBootSplash.h"
#endif
#if defined(VITA) && __has_include("vita_build_number.h")
#include "vita_build_number.h"
#endif

#ifdef VITA
// Create the writable data tree before anything (log, profiles, cache)
// tries to use it. The VPK ships no ux0:data content, so first run must
// create it; sceIoMkdir fails harmlessly if a directory already exists.
static void VitaEnsureDataTree()
{
	static const char *dirs[] = {
		"ux0:data/stepmania-5.0",
		"ux0:data/stepmania-5.0/Announcers",
		"ux0:data/stepmania-5.0/BGAnimations",
		"ux0:data/stepmania-5.0/BackgroundEffects",
		"ux0:data/stepmania-5.0/BackgroundTransitions",
		"ux0:data/stepmania-5.0/Cache",
		"ux0:data/stepmania-5.0/CDTitles",
		"ux0:data/stepmania-5.0/Characters",
		"ux0:data/stepmania-5.0/Courses",
		"ux0:data/stepmania-5.0/Data",
		"ux0:data/stepmania-5.0/Downloads",
		"ux0:data/stepmania-5.0/Logs",
		"ux0:data/stepmania-5.0/NoteSkins",
		"ux0:data/stepmania-5.0/Packages",
		"ux0:data/stepmania-5.0/Save",
		"ux0:data/stepmania-5.0/Screenshots",
		"ux0:data/stepmania-5.0/Songs",
		"ux0:data/stepmania-5.0/RandomMovies",
		"ux0:data/stepmania-5.0/Themes",
		NULL
	};
	for (const char **p = dirs; *p != NULL; ++p)
		sceIoMkdir(*p, 0777);
}
#endif

#ifdef VITA
// First-boot game data extraction: the VPK ships data.zip instead of 1300+
// loose files. Unpacked to ux0:data/stepmania-5.0/ once per build number.
static bool VitaFileExists(const char *path)
{
	SceUID fd = sceIoOpen(path, 0x0001 /*SCE_O_RDONLY*/, 0);
	if (fd < 0)
		return false;
	sceIoClose(fd);
	return true;
}

static void VitaMkdirParents(char *path)
{
	for (char *p = path + 1; *p != '\0'; ++p)
	{
		if (*p == '/')
		{
			*p = '\0';
			sceIoMkdir(path, 0777);
			*p = '/';
		}
	}
}

static void VitaExtractDataZip()
{
	static const char *kMarker = "ux0:data/stepmania-5.0/.data_ready";
	static const char *kZip = "app0:data.zip";

	int haveBuild = -1;
	{
		SceUID fd = sceIoOpen(kMarker, 0x0001 /*SCE_O_RDONLY*/, 0);
		if (fd >= 0)
		{
			char buf[32];
			int n = sceIoRead(fd, buf, (SceSize)sizeof(buf) - 1);
			sceIoClose(fd);
			if (n > 0)
			{
				buf[n] = '\0';
				haveBuild = atoi(buf);
			}
		}
	}
#ifdef VITA_BUILD_NUMBER
	if (haveBuild == VITA_BUILD_NUMBER)
		return;
#else
	if (haveBuild >= 0)
		return;
#endif

	if (!VitaFileExists(kZip))
		return; // dev build without data.zip; rely on loose app0: files.

	mz_zip_archive zip;
	memset(&zip, 0, sizeof(zip));
	if (!mz_zip_reader_init_file(&zip, kZip, 0))
		return;

	mz_uint total = mz_zip_reader_get_num_files(&zip);
 	{
 		char mark[64];
 		snprintf(mark, sizeof(mark), "extract-start %u files", (unsigned)total);
 		VitaBootMark(mark);
 	}
#if defined(VITA)
	VitaBootSplash::SetStage( BOOTSTAGE_EXTRACT );
#endif

	char out[256];
	for (mz_uint i = 0; i < total; ++i)
	{
		if (mz_zip_reader_is_file_a_directory(&zip, i))
			continue;
		mz_zip_archive_file_stat st;
		if (!mz_zip_reader_file_stat(&zip, i, &st))
			continue;
		if (st.m_filename[0] == '\0' || st.m_filename[0] == '/')
			continue;
		snprintf(out, sizeof(out), "ux0:data/stepmania-5.0/%s", st.m_filename);
		VitaMkdirParents(out);
		mz_zip_reader_extract_to_file(&zip, i, out, 0);
		if ((i & 255) == 0)
		{
			char mark[64];
			snprintf(mark, sizeof(mark), "extract %u/%u", (unsigned)i, (unsigned)total);
			VitaBootMark(mark);
#if defined(VITA)
			VitaBootSplash::SetProgress( (int)i, (int)total );
#endif
		}
	}
	mz_zip_reader_end(&zip);

	{
		SceUID fd = sceIoOpen(kMarker, 0x0002 | 0x0200 | 0x0400 /*W|CREAT|TRUNC*/, 0777);
		if (fd >= 0)
		{
#ifdef VITA_BUILD_NUMBER
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", VITA_BUILD_NUMBER);
			sceIoWrite(fd, buf, (SceSize)strlen(buf));
#else
			sceIoWrite(fd, "1", 1);
#endif
			sceIoClose(fd);
		}
	}
	VitaBootMark("extract-done");
}
#endif

#ifdef VITA
void VitaBootMark(const char *stage)
{
	// Raw kernel I/O only: safe to call before FILEMAN/LOG exist.
	SceUID fd = sceIoOpen("ux0:data/stepmania-5.0/vita-boot.log",
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
	if (fd < 0)
		return;
	char buf[128];
	int n = 0;
	while (stage[n] != '\0' && n < 100)
	{
		buf[n] = stage[n];
		++n;
	}
	buf[n++] = '\n';
	sceIoWrite(fd, buf, n);
	sceIoClose(fd);
}
#endif

void ArchHooks_Vita::Init()
{
#ifdef VITA
	// Data tree first: the very first boot mark needs the directory.
	// NOTE: LOG does not exist yet (created later in sm_main), so no LOG use here.
	VitaEnsureDataTree();
	VitaBootMark("hooks-init-enter");
#if defined(VITA)
	/* Boot splash first: extraction below can take minutes on first boot. */
	VitaBootSplash::Init();
#endif
	VitaExtractDataZip();
	VitaBootMark("hooks-init-exit");
#endif
}

void ArchHooks_Vita::DumpDebugInfo()
{
	LOG->Info("ArchHooks_Vita: 960x544, app0: + ux0:data/stepmania-5.0");
}

void ArchHooks_Vita::SetTime(tm newtime)
{
	// No settime on retail Vita; ignore.
	(void)newtime;
}

bool ArchHooks_Vita::GoToURL(RString sUrl)
{
	(void)sUrl;
	return false;
}

// ---- ArchHooks statics, Vita implementations (cf. ArchHooks_Unix.cpp) ----

int64_t ArchHooks::GetMicrosecondsSinceStart(bool /*bAccurate*/)
{
#ifdef VITA
	return int64_t(sceKernelGetProcessTimeWide());
#else
	return 0;
#endif
}

RString ArchHooks::GetPreferredLanguage()
{
	// TODO(Vita): sceAppUtilSystemParamGetById(SCE_SYSTEM_PARAM_ID_LANG,...)
	return "en";
}

void ArchHooks::MountInitialFilesystems(const RString &sDirOfExecutable)
{
#ifdef VITA
	VitaBootMark("mount-initial-enter");
#endif
#ifdef VITA
	// NOTE: Vita device paths have NO slash after the colon
	// ("app0:Data", NOT "app0:/Data") — the slash form fails to open.
	(void)sDirOfExecutable;
	FILEMAN->Mount("dirro", "app0:", "/");
	FILEMAN->Mount("dir", "app0:Announcers", "/Announcers");
	FILEMAN->Mount("dir", "app0:BGAnimations", "/BGAnimations");
	FILEMAN->Mount("dir", "app0:BackgroundEffects", "/BackgroundEffects");
	FILEMAN->Mount("dir", "app0:BackgroundTransitions", "/BackgroundTransitions");
	FILEMAN->Mount("dir", "app0:CDTitles", "/CDTitles");
	FILEMAN->Mount("dir", "app0:Characters", "/Characters");
	FILEMAN->Mount("dir", "app0:Courses", "/Courses");
	FILEMAN->Mount("dir", "app0:Data", "/Data");
	FILEMAN->Mount("dir", "app0:Docs", "/Docs");
	FILEMAN->Mount("dir", "app0:NoteSkins", "/NoteSkins");
	FILEMAN->Mount("dir", "app0:Songs", "/Songs");
	FILEMAN->Mount("dir", "app0:Themes", "/Themes");
#else
	FILEMAN->Mount("dirro", sDirOfExecutable, "/");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Announcers", "/Announcers");
	FILEMAN->Mount("dir", sDirOfExecutable + "/BGAnimations", "/BGAnimations");
	FILEMAN->Mount("dir", sDirOfExecutable + "/BackgroundEffects", "/BackgroundEffects");
	FILEMAN->Mount("dir", sDirOfExecutable + "/BackgroundTransitions", "/BackgroundTransitions");
	FILEMAN->Mount("dir", sDirOfExecutable + "/CDTitles", "/CDTitles");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Characters", "/Characters");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Courses", "/Courses");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Data", "/Data");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Docs", "/Docs");
	FILEMAN->Mount("dir", sDirOfExecutable + "/NoteSkins", "/NoteSkins");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Songs", "/Songs");
	FILEMAN->Mount("dir", sDirOfExecutable + "/Themes", "/Themes");
#endif
#ifdef VITA
	VitaBootMark("mount-initial-exit");
#endif
}

void ArchHooks::MountUserFilesystems(const RString &/*sDirOfExecutable*/)
{
#ifdef VITA
	// Belt and suspenders: Init() should have done this already.
	VitaEnsureDataTree();
	VitaBootMark("mount-user-enter");
#endif
	// Writable per-user data on the memory card.
	static const char *sUserDataPath = "ux0:data/stepmania-5.0";
	FILEMAN->Mount("dir", ssprintf("%s/Announcers", sUserDataPath), "/Announcers");
	FILEMAN->Mount("dir", ssprintf("%s/BGAnimations", sUserDataPath), "/BGAnimations");
	FILEMAN->Mount("dir", ssprintf("%s/BackgroundEffects", sUserDataPath), "/BackgroundEffects");
	FILEMAN->Mount("dir", ssprintf("%s/BackgroundTransitions", sUserDataPath), "/BackgroundTransitions");
	FILEMAN->Mount("dir", ssprintf("%s/Cache", sUserDataPath), "/Cache");
	FILEMAN->Mount("dir", ssprintf("%s/CDTitles", sUserDataPath), "/CDTitles");
	FILEMAN->Mount("dir", ssprintf("%s/Characters", sUserDataPath), "/Characters");
	FILEMAN->Mount("dir", ssprintf("%s/Courses", sUserDataPath), "/Courses");
	/* /Data was missing here (only app0:Data, which ships nothing): prefs
	 * defaults, translations and AI settings never loaded. ux0 shadows it. */
	FILEMAN->Mount("dir", ssprintf("%s/Data", sUserDataPath), "/Data");
	FILEMAN->Mount("dir", ssprintf("%s/Downloads", sUserDataPath), "/Downloads");
	FILEMAN->Mount("dir", ssprintf("%s/Logs", sUserDataPath), "/Logs");
	FILEMAN->Mount("dir", ssprintf("%s/NoteSkins", sUserDataPath), "/NoteSkins");
	FILEMAN->Mount("dir", ssprintf("%s/Packages", sUserDataPath), "/Packages");
	FILEMAN->Mount("dir", ssprintf("%s/Save", sUserDataPath), "/Save");
	FILEMAN->Mount("dir", ssprintf("%s/Screenshots", sUserDataPath), "/Screenshots");
	FILEMAN->Mount("dir", ssprintf("%s/Songs", sUserDataPath), "/Songs");
	FILEMAN->Mount("dir", ssprintf("%s/RandomMovies", sUserDataPath), "/RandomMovies");
	FILEMAN->Mount("dir", ssprintf("%s/Themes", sUserDataPath), "/Themes");
#ifdef VITA
	VitaBootMark("mount-user-exit");
#endif
}
