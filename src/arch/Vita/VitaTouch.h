#ifndef VITA_TOUCH_H
#define VITA_TOUCH_H

/* PS Vita front touch panel support.
 *
 * The panel is split into four equal vertical columns that map to the four
 * dance panels (left, down, up, right). Multitouch is supported, so chords and
 * jumps can be held. Touches become the same key events the pad emits
 * (KEY_LEFT/KEY_DOWN/KEY_UP/KEY_RIGHT), so the stock InputMapper bindings and
 * the notefield work with no extra configuration.
 *
 * While ScreenGameplay is running the lanes are drawn as a translucent overlay
 * (see RageDisplay_Vita::EndFrame) so the player sees the touch feedback. Both
 * the overlay and touch input can be switched off in Theme Options:
 *   VitaTouchControls          on/off (touch input)
 *   VitaTouchOverlay           on/off (visible zones)
 *   VitaTouchOverlayOpacity    0..100 %
 */
class VitaTouch
{
public:
	enum
	{
		ZONE_LEFT = 0,
		ZONE_DOWN,
		ZONE_UP,
		ZONE_RIGHT,
		NUM_ZONES
	};

	/* Starts front-panel sampling. Safe to call more than once. */
	static void Init();

	/* Samples the panel; call once per input update. */
	static void Poll();

	static bool Enabled();      /* "VitaTouchControls" */
	static bool InputActive();  /* enabled and currently in gameplay */
	static bool ZoneDown( int iZone );

	/* ScreenGameplay tells us when the lanes should be live. */
	static void SetPlayActive( bool bActive );
	static bool PlayActive() { return s_bPlayActive; }

	/* The song wheel uses the panel for swipes instead of lanes; it must tell
	 * us when it is the active screen (ScreenSelectMusic). */
	static void SetWheelActive( bool bActive );

	/* Fling scrolling: number of items to move this frame. Positive = next
	 * song (swipe up), negative = previous. Consumed by the input handler. */
	static int ConsumeWheelTaps();

	/* Overlay, drawn by RageDisplay_Vita at the end of the frame. */
	static bool ShouldDrawOverlay();
	static void DrawOverlay( int iWidth, int iHeight );

private:
	static bool s_bDown[NUM_ZONES];
	static bool s_bInit;
	static bool s_bPlayActive;
	static bool s_bWheelActive;
	static bool s_bTouching;
	static int s_iLastY;
	static int s_iAccumY;
	static int s_iPendingTaps;
};

#endif
