#include "common/common.h"
#include "common/options.h"
#include "graphics/grap_buf.h"
#include "funcs.h"
#include "macros.h"
#include "vars.h"

#include "journal.h"
#include "topics.h"

#if defined(ENABLE_TALK_TOPICS)

#include <string.h>

// Conversation topics: at "Your interest?" a list over the map offers NAME,
// JOB and BYE, and every keyword of the person's that has come up so far,
// as the word they used ("Spirits" for the keyword SPIR). A keyword comes up
// when the person says a word that starts with it, or when the player asks
// about it. Keywords nobody has mentioned stay hidden, as in the original.
// What the journal heard from the person before counts too, so the list
// carries on from the last conversation.
//
// The player clicks a topic, or picks one with Up, Down or Tab, which puts
// it on the input line for Enter to ask; typing works as before.

extern SDL_FRect g_presentRect;
extern int g_presentWidth;
extern int g_presentLayout;
extern bool GRAP_SDL_WindowToRender(float windowX, float windowY, int* renderX, int* renderY);
extern void KEY_SDL_ProcessKeyDown(SDL_KeyboardEvent ev);

#define KEYWORD_MAX 64
#define KEYWORD_LENGTH 12
#define TOPIC_MAX 64
#define TOPIC_LENGTH 16 // the longest input is 20 characters; words are shorter

typedef struct Topic
{
    char keyword[KEYWORD_LENGTH]; // what the game matches, upper case
    char text[TOPIC_LENGTH];      // what the list shows: "Spirits"
    bool asked;
} Topic;

// The person's keywords, upper case, and the topic each has become, or -1.
static char s_keywords[KEYWORD_MAX][KEYWORD_LENGTH];
static int s_keywordTopic[KEYWORD_MAX];
static int s_keywordCount;

static Topic s_topics[TOPIC_MAX];
static int s_topicCount;
static bool s_inConversation;
static char s_word[TOPIC_LENGTH];
static int s_wordLength;

// The list on screen.
static Topic s_shown[TOPIC_MAX];
static int s_shownCount;
static bool s_showing;
static int s_selected = -1; // picked with the keys
static int s_hovered = -1;  // under the mouse
static int s_clicked = -1;  // clicked, for the Enter it sends

// Past the next string of the talk script: false if the keyword section's end
// (0x90) comes first, as TALK_0728_SkipOneTalkString(0, 0x90) finds.
static bool SkipString(const u8** p, const u8* end)
{
    while (*p < end)
    {
        u8 c = *(*p)++;
        if (c == 0)
            return true;
        if (c == 0x90)
            return false;
    }
    return false;
}

// A keyword string as upper-case letters; false if it holds anything else
// (an <OR> or a command).
static bool DecodeKeyword(const u8* p, const u8* end, char* out)
{
    int length = 0;

    for (; p < end && *p != 0; p++)
    {
        char c = (char)(*p & 0x7f);
        if (*p < 0xa0 || length == KEYWORD_LENGTH - 1 || !SDL_isalpha(c))
            return false;
        out[length++] = (char)SDL_toupper(c);
    }
    out[length] = '\0';
    return length > 0;
}

// Keywords sit at strings 5, 7, 9... with their answers between (TALK_099a).
static void ReadKeywords(void)
{
    const u8* p = D_b21e;
    const u8* end = D_b21e + 0x400;

    s_keywordCount = 0;
    for (int i = 0; i < 5; i++)
    {
        if (!SkipString(&p, end))
            return;
    }

    while (s_keywordCount < KEYWORD_MAX && p < end && *p != 0x90)
    {
        if (DecodeKeyword(p, end, s_keywords[s_keywordCount]))
            s_keywordTopic[s_keywordCount++] = -1;
        if (!SkipString(&p, end) || !SkipString(&p, end))
            return;
    }
}

static int AddTopic(const char* keyword, const char* word)
{
    Topic* topic;
    int length = 0;

    if (s_topicCount == TOPIC_MAX)
        return -1;
    topic = &s_topics[s_topicCount];
    SDL_strlcpy(topic->keyword, keyword, sizeof(topic->keyword));
    for (; word[length] && length < TOPIC_LENGTH - 1; length++)
        topic->text[length] = (char)(length == 0 ? SDL_toupper(word[0]) : SDL_tolower(word[length]));
    topic->text[length] = '\0';
    topic->asked = false;
    return s_topicCount++;
}

// A word the person said: a topic, if it starts with one of their keywords.
static void EndWord(void)
{
    s_word[s_wordLength] = '\0';
    for (int i = 0; s_wordLength > 0 && i < s_keywordCount; i++)
    {
        if (s_keywordTopic[i] < 0 && SDL_strncasecmp(s_word, s_keywords[i], strlen(s_keywords[i])) == 0)
        {
            s_keywordTopic[i] = AddTopic(s_keywords[i], s_word);
            break;
        }
    }
    s_wordLength = 0;
}

void TOPICS_CaptureChar(int c)
{
    if (!s_inConversation)
        return;
    if (SDL_isalpha(c))
    {
        if (s_wordLength < TOPIC_LENGTH - 1)
            s_word[s_wordLength++] = (char)c;
    }
    else
    {
        EndWord();
    }
}

// True if input asks about keyword, as the game matches it: at the start of
// one of the input's words.
static bool Asks(const char* keyword, const char* input)
{
    int at = ULTIMA_6f1e((byte*)keyword, (char*)input);
    return at == 0 || (at > 0 && input[at - 1] == ' ');
}

void TOPICS_Asked(const char* input)
{
    for (int i = 0; i < s_topicCount; i++)
    {
        if (Asks(s_topics[i].keyword, input))
            s_topics[i].asked = true;
    }

    // A keyword the player knew to ask about becomes a topic, as they put it.
    for (int i = 0; i < s_keywordCount; i++)
    {
        if (s_keywordTopic[i] < 0 && Asks(s_keywords[i], input))
        {
            int at = ULTIMA_6f1e((byte*)s_keywords[i], (char*)input);
            char word[TOPIC_LENGTH];
            int length = 0;
            while (SDL_isalpha(input[at + length]) && length < TOPIC_LENGTH - 1)
            {
                word[length] = input[at + length];
                length++;
            }
            word[length] = '\0';
            if ((s_keywordTopic[i] = AddTopic(s_keywords[i], word)) >= 0)
                s_topics[s_keywordTopic[i]].asked = true;
        }
    }
}

// Carry on from what the journal heard from this person before.
static void RecallJournal(int npc)
{
#if defined(ENABLE_JOURNAL)
    for (int i = 0; i < JOURNAL_PersonCount(); i++)
    {
        JournalPerson* person = JOURNAL_GetPerson(i);
        if (person->mapId != D_5893_map_id || person->npc != npc)
            continue;

        for (int j = 0; j < person->entryCount; j++)
        {
            JournalEntry* entry = &person->entries[j];
            char asked[sizeof(entry->keyword)];
            char* part;
            char* next;

            for (const char* c = entry->text; *c; c++)
                TOPICS_CaptureChar(*c);
            EndWord();

            // "JOB/WORK": each question asked.
            SDL_strlcpy(asked, entry->keyword, sizeof(asked));
            for (part = asked; part; part = next)
            {
                next = strchr(part, '/');
                if (next)
                    *next++ = '\0';
                TOPICS_Asked(part);
            }
        }
    }
#endif
}

void TOPICS_BeginConversation(int npc)
{
    s_topicCount = 0;
    s_wordLength = 0;
    AddTopic("NAME", "Name");
    AddTopic("JOB", "Job");
    ReadKeywords();

    s_inConversation = true;
    RecallJournal(npc);
}

void TOPICS_EndConversation(void)
{
    s_inConversation = false;
}

// True if one of the answers to the question at D_bcf4 is a start of word:
// "Y" or "YES" for YES. The answers follow the label, its question and the
// reply to anything else (TALK_0bd4).
static bool QuestionTakes(const char* word)
{
    const u8* p = D_b21e;
    const u8* end = D_b21e + 0x400;
    char keyword[KEYWORD_LENGTH];

    while (p < end && *p != D_bcf4)
    {
        while (p < end && *p != 0x90 && *p != 0x9f)
            p++;
        p++;
    }
    for (int i = 0; i < 2; i++)
    {
        while (p < end && *p != 0 && *p != 0x9f)
            p++;
        p++;
    }

    while (p < end)
    {
        if (DecodeKeyword(p, end, keyword) && SDL_strncasecmp(word, keyword, strlen(keyword)) == 0)
            return true;
        if (!SkipString(&p, end) || !SkipString(&p, end))
            return false;
    }
    return false;
}

static void Show(const char* text, bool asked)
{
    Topic* topic = &s_shown[s_shownCount++];
    SDL_strlcpy(topic->text, text, sizeof(topic->text));
    topic->asked = asked;
}

static void BuildList(int prompt)
{
    s_shownCount = 0;

    if (prompt == TOPICS_INTEREST)
    {
        for (int i = 0; i < s_topicCount && s_shownCount < TOPIC_MAX - 1; i++)
            Show(s_topics[i].text, s_topics[i].asked);
        Show("Bye", false);
    }
    else if (prompt == TOPICS_ANSWER)
    {
        if (QuestionTakes("YES"))
            Show("Yes", false);
        if (QuestionTakes("NO"))
            Show("No", false);
    }
    else if (prompt == TOPICS_NAME)
    {
        Show(D_55a8_party[0].name, false);
    }
}

static void Redraw(void)
{
    GRAP_BUF_MarkDirty();
}

// Puts text on the input line in place of what's there.
static int Replace(char* out, int length, int max, const char* text)
{
    ULTIMA_1fa0_Backspace(length);
    for (length = 0; text[length] && length < max; length++)
    {
        out[length] = (char)SDL_toupper(text[length]);
        ULTIMA_16ba_PrintChar(out[length]);
    }
    return length;
}

void TOPICS_GetString(char* out, int max, int prompt)
{
    int saved = D_538c;
    int length = 0;
    int key;

    EndWord();
    BuildList(prompt);
    s_showing = g_options.talkTopics && s_shownCount > 0;
    s_selected = -1;
    s_clicked = -1;
    Redraw();

    // As ULTIMA_3b1c_GetString, with the list's keys.
    D_538c = 0;
    do
    {
        key = ULTIMA_266c_GetChar();
        if (key == U5_KEY_ENTER && s_showing && s_clicked >= 0)
        {
            length = Replace(out, length, max, s_shown[s_clicked].text);
        }
        else if (s_showing && (key == U5_KEY_UP || key == U5_KEY_DOWN || key == '\t'))
        {
            if (key == U5_KEY_UP)
                s_selected = s_selected <= 0 ? s_shownCount - 1 : s_selected - 1;
            else
                s_selected = (s_selected + 1) % s_shownCount;
            length = Replace(out, length, max, s_shown[s_selected].text);
            Redraw();
        }
        else if ((key == U5_KEY_BACKSPACE || key == U5_KEY_LEFT) && length != 0)
        {
            ULTIMA_1fa0_Backspace(1);
            length--;
        }
        else if (key == U5_KEY_ESC && length != 0)
        {
            ULTIMA_1fa0_Backspace(length);
            length = 0;
        }
        else if (0x1f < key && key < 0x80 && length < max)
        {
            out[length++] = (char)key;
            ULTIMA_16ba_PrintChar(key);
        }
    } while (key != U5_KEY_ENTER);
    out[length] = '\0';
    D_538c = saved;

    s_showing = false;
    s_hovered = -1;
    Redraw();
}

// Layout, in the game's render pixels (640x400, wider in the wide view). The
// text is the game's font at the game's size, 16 pixels a letter.
#define FONT_PIXEL 2
#define LETTER (8 * FONT_PIXEL)
#define PAD_X 6
#define PAD_Y 4
#define GAP 6
#define MARGIN 8
#define BUTTON_H (LETTER + 2 * PAD_Y)
#define HEADING "Ask about:"

typedef struct Box
{
    int x, y, w, h;
} Box;

static Box s_panel;
static Box s_buttons[TOPIC_MAX];

// Flows the buttons into rows across the bottom of the map.
static void Layout(void)
{
    int left = MAP_RENDER_LEFT;
    int right = MAP_RENDER_RIGHT;
    int x, row = 0;
    int rowOf[TOPIC_MAX];

    if (g_presentLayout == PRESENT_LAYOUT_WIDE_MAP)
        right += 2 * WIDE_EXTRA_WIDTH;
    else if (g_presentLayout == PRESENT_LAYOUT_CENTERED)
        left += WIDE_EXTRA_WIDTH, right += WIDE_EXTRA_WIDTH;

    int inner = left + MARGIN + PAD_X;
    int limit = right - MARGIN - PAD_X;
    x = inner;
    for (int i = 0; i < s_shownCount; i++)
    {
        int w = (int)strlen(s_shown[i].text) * LETTER + 2 * PAD_X;
        if (x > inner && x + w > limit)
        {
            row++;
            x = inner;
        }
        s_buttons[i] = (Box){x, 0, w, BUTTON_H};
        rowOf[i] = row;
        x += w + GAP;
    }

    // Heading, then the rows, at the bottom of the map.
    int height = PAD_Y + LETTER + PAD_Y + (row + 1) * (BUTTON_H + GAP) - GAP + PAD_Y + 2;
    s_panel = (Box){left + MARGIN, MAP_RENDER_BOTTOM - MARGIN - height, right - left - 2 * MARGIN, height};
    for (int i = 0; i < s_shownCount; i++)
        s_buttons[i].y = s_panel.y + PAD_Y + LETTER + PAD_Y + rowOf[i] * (BUTTON_H + GAP);
}

static int ButtonAt(float windowX, float windowY)
{
    int x, y;

    if (!s_showing || !GRAP_SDL_WindowToRender(windowX, windowY, &x, &y))
        return -1;
    Layout();
    for (int i = 0; i < s_shownCount; i++)
    {
        Box* b = &s_buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
            return i;
    }
    return -1;
}

bool TOPICS_IsShowing(void)
{
    return s_showing;
}

void TOPICS_MouseMoved(float x, float y)
{
    int hovered = ButtonAt(x, y);
    if (hovered != s_hovered)
    {
        s_hovered = hovered;
        Redraw();
    }
}

bool TOPICS_MouseDown(float x, float y)
{
    SDL_KeyboardEvent ev;

    if ((s_clicked = ButtonAt(x, y)) < 0)
        return false;

    // Enter asks it; TOPICS_GetString puts the topic on the line first.
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.down = true;
    ev.key = SDLK_RETURN;
    KEY_SDL_ProcessKeyDown(ev);
    return true;
}

// Text in the game's font (charset 0, 8x8 with the top bit on the left).
static void DrawText(SDL_Renderer* renderer, const char* text, float x, float y, float pixelW, float pixelH)
{
    const u8* font = D_539c[0];
    SDL_FRect pixels[64];

    if (!font)
        return;

    for (; *text; text++, x += 8 * pixelW)
    {
        const u8* glyph = &font[((u8)*text & 0x7f) * 8];
        int count = 0;

        for (int row = 0; row < 8; row++)
        {
            for (int col = 0; col < 8; col++)
            {
                if (glyph[row] & (0x80 >> col))
                    pixels[count++] = (SDL_FRect){x + col * pixelW, y + row * pixelH, pixelW, pixelH};
            }
        }
        SDL_RenderFillRects(renderer, pixels, count);
    }
}

void TOPICS_Draw(SDL_Renderer* renderer)
{
    if (!s_showing)
        return;

    float scaleX = g_presentRect.w / g_presentWidth;
    float scaleY = g_presentRect.h / hiresHeight;
#define WX(v) (g_presentRect.x + (v) * scaleX)
#define WY(v) (g_presentRect.y + (v) * scaleY)

    Layout();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_FRect panel = {WX(s_panel.x), WY(s_panel.y), s_panel.w * scaleX, s_panel.h * scaleY};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 85, 85, 255, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
    DrawText(renderer, HEADING, WX(s_panel.x + PAD_X), WY(s_panel.y + PAD_Y),
             FONT_PIXEL * scaleX, FONT_PIXEL * scaleY);

    for (int i = 0; i < s_shownCount; i++)
    {
        Box* b = &s_buttons[i];
        SDL_FRect box = {WX(b->x), WY(b->y), b->w * scaleX, b->h * scaleY};
        bool lit = i == s_selected || i == s_hovered;

        // Picked or under the mouse: filled. Asked already: grey.
        if (lit)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 170, 255);
            SDL_RenderFillRect(renderer, &box);
        }
        if (lit)
            SDL_SetRenderDrawColor(renderer, 255, 255, 85, 255);
        else if (s_shown[i].asked)
            SDL_SetRenderDrawColor(renderer, 85, 85, 85, 255);
        else
            SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
        SDL_RenderRect(renderer, &box);

        if (lit)
            SDL_SetRenderDrawColor(renderer, 255, 255, 85, 255);
        else if (s_shown[i].asked)
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        else
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawText(renderer, s_shown[i].text, WX(b->x + PAD_X), WY(b->y + PAD_Y),
                 FONT_PIXEL * scaleX, FONT_PIXEL * scaleY);
    }
#undef WX
#undef WY
}

#endif
