#ifndef LOADING_WINDOW_VITA_H
#define LOADING_WINDOW_VITA_H

#include "LoadingWindow.h"

/* Loading window for PS Vita: no native dialogs here, so progress and text
 * are forwarded to the raw framebuffer boot splash (VitaBootSplash). */
class LoadingWindow_Vita: public LoadingWindow
{
public:
	LoadingWindow_Vita();
	virtual RString Init() { return RString(); }
	virtual void SetText( RString sText );
	virtual void SetProgress( const int progress );
	virtual void SetTotalWork( const int totalWork );
	virtual void SetIndeterminate( bool indeterminate );

private:
	int m_progress;
	int m_totalWork;
};

#define USE_LOADING_WINDOW_VITA

#endif
