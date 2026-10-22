#ifndef _JOURNAL_H
#define _JOURNAL_H

#if defined(ENABLE_JOURNAL)

typedef struct JournalEntry
{
    char keyword[48]; // what the player asked, e.g. "JOB" or "JOB/WORK"
    char* text;       // the reply; '\n' separates the player's answers
} JournalEntry;

typedef struct JournalPerson
{
    int mapId;
    int npc;        // talk script number within the map's .TLK file
    char name[24];
    char desc[64];  // "a ragged beggar"
    int entryCount;
    JournalEntry* entries;
} JournalPerson;

// Recording, called from the conversation code (talk.c).
void JOURNAL_BeginConversation(int npc); // the talk script is loaded in D_b21e
void JOURNAL_BeginTopic(const char* keyword);
void JOURNAL_DiscardTopic(void);
void JOURNAL_EndTopic(void);
void JOURNAL_CaptureChar(int c);
void JOURNAL_CaptureText(const char* text); // part of a reply printed directly
void JOURNAL_AddAnswer(const char* answer);

// Forget everything; a new character starts a new journal.
void JOURNAL_Clear(void);

// Reading, for the journal screen. People are sorted by map.
int JOURNAL_PersonCount(void);
JournalPerson* JOURNAL_GetPerson(int index);
// The name once the player has asked for it, the description until then.
const char* JOURNAL_PersonTitle(JournalPerson* person);
// True if any of the person's text contains filter (case-insensitive).
bool JOURNAL_PersonMatches(JournalPerson* person, const char* filter);

#endif

#endif
