#include "global.h"
#include "StepMania.h"

#ifdef VITA
#include "arch/Vita/launcher/VitaLauncher.h"
#endif

int main(int argc, char* argv[]) {
#ifdef VITA
	/* The LiveArea "Download Song Packs" tile starts the app with psla:packs.
	 * Only then does the downloader appear, in front of the engine and using
	 * no engine resources, so the game boots normally once it closes. */
	if (VitaLauncher::WantsPackBrowser(argc, argv))
		VitaLauncher::RunPackBrowser();
#endif
	return sm_main(argc, argv);
}

/*
 * (c) 2014 Jason Felds
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
