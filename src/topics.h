#ifndef _TOPICS_H
#define _TOPICS_H

#if defined(ENABLE_TALK_TOPICS)

#include <SDL3/SDL.h>

// Which prompt the list is for.
enum
{
    TOPICS_INTEREST, // "Your interest?": the topics found so far
    TOPICS_ANSWER,   // "You respond-": Yes and No, when the question takes them
    TOPICS_NAME,     // "What is thy name?": the Avatar's name
};

// Recording, called from the conversation code (talk.c).
void TOPICS_BeginConversation(int npc); // the talk script is loaded in D_b21e
void TOPICS_EndConversation(void);
void TOPICS_CaptureChar(int c);         // a character of what the person says
void TOPICS_Asked(const char* input);   // what the player asked about

// Reads the player's words, as ULTIMA_3b1c_GetString does, with the topics
// listed over the map to click or pick with Up, Down or Tab.
void TOPICS_GetString(char* out, int max, int prompt);

// The list over the presented frame, and the mouse on it.
void TOPICS_Draw(SDL_Renderer* renderer);
bool TOPICS_IsShowing(void);
void TOPICS_MouseMoved(float x, float y);
bool TOPICS_MouseDown(float x, float y);

#endif

#endif
