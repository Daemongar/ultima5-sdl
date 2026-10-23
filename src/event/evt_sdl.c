#include "common/common.h"
#include "common/options.h"
#include "graphics/grap.h"

#include "event.h"
#include "hover.h"
#include "key/gamepad.h"

#include <SDL3/SDL.h>

static EVT_Callback* s_callbacks[16];
static int s_registeredCallbackCount;

void EVT_Initialize(void)
{
}

void EVT_Cleanup(void)
{
}

// TODO
void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);

#if defined(ENABLE_MOUSE_AIM)
void KEY_SDL_ProcessMouseMotion(SDL_MouseMotionEvent ev);
void KEY_SDL_ProcessMouseButtonDown(SDL_MouseButtonEvent ev);
#endif

void EVT_PollMessages(void)
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev))
	{
		switch (ev.type)
		{
		case SDL_EVENT_QUIT:
			exit(0);
			break;

		case SDL_EVENT_KEY_DOWN:
#if defined(ENABLE_HOVER_NAMES)
			HOVER_KeyPressed();
#endif
			KEY_SDL_ProcessKeyDown(ev.key);
			break;

		case SDL_EVENT_MOUSE_MOTION:
#if defined(ENABLE_HOVER_NAMES)
			HOVER_MouseMoved(ev.motion.x, ev.motion.y);
#endif
#if defined(ENABLE_MOUSE_AIM)
			if (g_options.mouseAim)
				KEY_SDL_ProcessMouseMotion(ev.motion);
#endif
			break;

#if defined(ENABLE_GAMEPAD)
		case SDL_EVENT_GAMEPAD_ADDED:
		case SDL_EVENT_GAMEPAD_REMOVED:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			GAMEPAD_HandleEvent(&ev);
			break;
#endif

#if defined(ENABLE_HOVER_NAMES)
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			HOVER_MouseLeft();
			break;
#endif

#if defined(ENABLE_MOUSE_AIM)
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (g_options.mouseAim)
				KEY_SDL_ProcessMouseButtonDown(ev.button);
			break;
#endif
		}
	}
}

void EVT_Yield(void)
{
	GRAP_FlushPendingPresent();
	EVT_PollMessages();

	for (int i = 0; i < s_registeredCallbackCount; i++)
	{
		(*s_callbacks[i])();
	}
}

void EVT_RegisterCallback(EVT_Callback* callback)
{
	ASSERT(s_registeredCallbackCount < ARRAYSIZE(s_callbacks));

	s_callbacks[s_registeredCallbackCount++] = callback;
}
