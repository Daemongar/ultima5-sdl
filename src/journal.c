#include "common/common.h"
#include "funcs.h"
#include "vars.h"

#include "journal.h"

#if defined(ENABLE_JOURNAL)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/file.h"

#if defined(OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

// The journal (F4): everything people have said in answer to the player's
// questions, by person and keyword, so clues needn't be written down.
//
// talk.c reports each conversation and question as it happens. The reply is
// whatever the person says until the player is next asked "Your interest?",
// including any questions they ask and the player's answers. It is kept in
// SAVEGAME/JOURNAL.TXT, which save slots don't touch: like a paper notebook,
// it still holds what the player heard after they load an older game.

#define JOURNAL_FILE "SAVEGAME" PATH_SEP "JOURNAL.TXT"
#define TOPIC_MAX 4000

// A hard line break, kept through the whitespace clean-up in EndTopic.
#define BREAK_MARK '\x01'

static bool s_loaded;
static int s_personCount;
static JournalPerson* s_persons;

// The conversation and question being recorded.
static int s_mapId = -1;
static int s_npc;
static char s_name[24];
static char s_desc[64];
static bool s_topicOpen;
static char s_keyword[24];
static char s_topic[TOPIC_MAX];
static int s_topicLength;

static void* CheckedRealloc(void* block, size_t size)
{
    void* result = realloc(block, size);
    if (!result)
        abort();
    return result;
}

static char* CopyString(const char* text)
{
    char* copy = CheckedRealloc(NULL, strlen(text) + 1);
    strcpy(copy, text);
    return copy;
}

static void CopyField(char* out, int size, const char* text)
{
    strncpy(out, text, size - 1);
    out[size - 1] = '\0';
}

static int PersonOrder(int mapId, int npc, JournalPerson* person)
{
    if (mapId != person->mapId)
        return mapId - person->mapId;
    return npc - person->npc;
}

// The person's record, added in map order if new.
static JournalPerson* FindPerson(int mapId, int npc, bool add)
{
    int i;

    for (i = 0; i < s_personCount; i++)
    {
        int order = PersonOrder(mapId, npc, &s_persons[i]);
        if (order == 0)
            return &s_persons[i];
        if (order < 0)
            break;
    }

    if (!add)
        return NULL;

    s_persons = CheckedRealloc(s_persons, (s_personCount + 1) * sizeof(JournalPerson));
    memmove(&s_persons[i + 1], &s_persons[i], (s_personCount - i) * sizeof(JournalPerson));
    s_personCount++;

    memset(&s_persons[i], 0, sizeof(JournalPerson));
    s_persons[i].mapId = mapId;
    s_persons[i].npc = npc;
    return &s_persons[i];
}

static JournalEntry* AddEntry(JournalPerson* person, const char* keyword, const char* text)
{
    JournalEntry* entry;

    person->entries = CheckedRealloc(person->entries, (person->entryCount + 1) * sizeof(JournalEntry));
    entry = &person->entries[person->entryCount++];
    CopyField(entry->keyword, sizeof(entry->keyword), keyword);
    entry->text = CopyString(text);
    return entry;
}

static bool KeywordListHas(const char* list, const char* keyword)
{
    size_t length = strlen(keyword);
    const char* p = list;

    while ((p = strstr(p, keyword)) != NULL)
    {
        if ((p == list || p[-1] == '/') && (p[length] == '\0' || p[length] == '/'))
            return true;
        p += length;
    }
    return false;
}

// Remembers what the person said about keyword: replaces an older answer to
// the same question, or joins another question that got the same answer
// (JOB and WORK usually do).
static void Remember(JournalPerson* person, const char* keyword, const char* text)
{
    int i;

    for (i = 0; i < person->entryCount; i++)
    {
        JournalEntry* entry = &person->entries[i];

        if (KeywordListHas(entry->keyword, keyword))
        {
            free(entry->text);
            entry->text = CopyString(text);
            return;
        }
    }

    for (i = 0; i < person->entryCount; i++)
    {
        JournalEntry* entry = &person->entries[i];

        if (strcmp(entry->text, text) == 0)
        {
            if (strlen(entry->keyword) + 1 + strlen(keyword) < sizeof(entry->keyword))
            {
                strcat(entry->keyword, "/");
                strcat(entry->keyword, keyword);
            }
            return;
        }
    }

    AddEntry(person, keyword, text);
}

static void FreeAll(void)
{
    for (int i = 0; i < s_personCount; i++)
    {
        for (int j = 0; j < s_persons[i].entryCount; j++)
            free(s_persons[i].entries[j].text);
        free(s_persons[i].entries);
    }
    free(s_persons);
    s_persons = NULL;
    s_personCount = 0;
}

// One line per entry, tab-separated:
// map  npc  name  description  keyword  text (with '|' for line breaks)
static void Save(void)
{
    FILE* stream;

    u5_mkdir("SAVEGAME");
    stream = FILE_Open(JOURNAL_FILE, "wb");
    if (!stream)
        return;

    for (int i = 0; i < s_personCount; i++)
    {
        JournalPerson* person = &s_persons[i];

        for (int j = 0; j < person->entryCount; j++)
        {
            fprintf(stream, "%d\t%d\t%s\t%s\t%s\t", person->mapId, person->npc,
                person->name, person->desc, person->entries[j].keyword);
            for (char* p = person->entries[j].text; *p; p++)
                fputc(*p == '\n' ? '|' : *p, stream);
            fputc('\n', stream);
        }
    }

    fclose(stream);
}

// Splits line at tabs into fields; returns how many.
static int SplitFields(char* line, char** fields, int max)
{
    int count = 0;

    fields[count++] = line;
    for (char* p = line; *p && count < max; p++)
    {
        if (*p == '\t')
        {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    return count;
}

static void Load(void)
{
    static char line[TOPIC_MAX + 256];
    char* fields[6];
    FILE* stream;

    s_loaded = true;

    stream = FILE_Open(JOURNAL_FILE, "rb");
    if (!stream)
        return;

    while (fgets(line, sizeof(line), stream))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (SplitFields(line, fields, 6) != 6)
            continue;

        JournalPerson* person = FindPerson(atoi(fields[0]), atoi(fields[1]), true);
        CopyField(person->name, sizeof(person->name), fields[2]);
        CopyField(person->desc, sizeof(person->desc), fields[3]);

        for (char* p = fields[5]; *p; p++)
        {
            if (*p == '|')
                *p = '\n';
        }
        AddEntry(person, fields[4], fields[5]);
    }

    fclose(stream);
}

static void EnsureLoaded(void)
{
    if (!s_loaded)
        Load();
}

// Plain text of a talk script string, as TALK_0f32_ProcessChar would print
// it: letters have the high bit set, bytes below 0x81 are words from D_24ea,
// and the other codes are commands, which print nothing here.
static void DecodeTalkString(const u8* p, char* out, int size)
{
    int length = 0;
    bool afterWord = false;

    for (; *p != 0; p++)
    {
        const char* word = NULL;
        char letter[2] = {0, 0};

        if (*p < 0x81)
            word = D_24ea[*p - 1];
        else if (*p >= 0xa0 && *p != 0xff)
            letter[0] = (char)(*p & 0x7f);
        else if (*p == 0x8d)
            letter[0] = ' ';

        if (word && *word)
        {
            if (length > 0 && length < size - 1)
                out[length++] = ' ';
            for (; *word && length < size - 1; word++)
                out[length++] = *word;
            afterWord = true;
        }
        else if (letter[0])
        {
            if (afterWord && length < size - 1)
                out[length++] = ' ';
            if (length < size - 1)
                out[length++] = letter[0];
            afterWord = false;
        }
    }

    // Trim and collapse spaces.
    out[length] = '\0';
    int read = 0, write = 0;
    while (out[read] == ' ')
        read++;
    for (; out[read]; read++)
    {
        if (out[read] == ' ' && (out[read + 1] == ' ' || out[read + 1] == '\0'))
            continue;
        out[write++] = out[read];
    }
    out[write] = '\0';
}

void JOURNAL_BeginConversation(int npc)
{
    const u8* p = D_b21e;

    EnsureLoaded();

    s_mapId = D_5893_map_id;
    s_npc = npc;
    s_topicOpen = false;

    // String 0 is the person's name, string 1 their description.
    DecodeTalkString(p, s_name, sizeof(s_name));
    while (*p != 0)
        p++;
    DecodeTalkString(p + 1, s_desc, sizeof(s_desc));

    // "a noble fighting bard." follows "You see"; as a title it needs no period.
    size_t length = strlen(s_desc);
    if (length > 0 && s_desc[length - 1] == '.')
        s_desc[length - 1] = '\0';
}

void JOURNAL_BeginTopic(const char* keyword)
{
    int length = 0;

    if (s_mapId < 0)
        return;

    // Uppercase and trimmed, as the keyword is shown in the journal.
    while (*keyword == ' ')
        keyword++;
    for (; *keyword && length < (int)sizeof(s_keyword) - 1; keyword++)
        s_keyword[length++] = (char)ULTIMA_2032_ToUpper((u8)*keyword);
    while (length > 0 && s_keyword[length - 1] == ' ')
        length--;
    s_keyword[length] = '\0';

    s_topicOpen = length > 0;
    s_topicLength = 0;
}

void JOURNAL_DiscardTopic(void)
{
    s_topicOpen = false;
}

void JOURNAL_CaptureChar(int c)
{
    if (s_topicOpen && s_topicLength < TOPIC_MAX - 1)
        s_topic[s_topicLength++] = (char)c;
}

void JOURNAL_CaptureText(const char* text)
{
    for (; *text; text++)
        JOURNAL_CaptureChar(*text);
}

void JOURNAL_AddAnswer(const char* answer)
{
    if (!s_topicOpen)
        return;

    JOURNAL_CaptureChar(BREAK_MARK);
    JOURNAL_CaptureChar('>');
    JOURNAL_CaptureChar(' ');
    for (; *answer; answer++)
        JOURNAL_CaptureChar(ULTIMA_2032_ToUpper((u8)*answer));
    JOURNAL_CaptureChar(BREAK_MARK);
}

// Stores the reply. The script breaks lines to fit the game's narrow text
// window, so runs of spaces and line breaks become one space; a blank line
// or one of the player's answers becomes a line break.
void JOURNAL_EndTopic(void)
{
    char text[TOPIC_MAX];
    int length = 0;
    int i = 0;

    if (!s_topicOpen)
        return;
    s_topicOpen = false;

    while (i < s_topicLength)
    {
        char c = s_topic[i];

        if (c == ' ' || c == '\n' || c == BREAK_MARK)
        {
            int newlines = 0;
            bool hard = false;

            for (; i < s_topicLength; i++)
            {
                c = s_topic[i];
                if (c == '\n')
                    newlines++;
                else if (c == BREAK_MARK)
                    hard = true;
                else if (c != ' ')
                    break;
            }

            if (length > 0 && i < s_topicLength)
                text[length++] = (hard || newlines >= 2) ? '\n' : ' ';
            continue;
        }

        if ((u8)c >= 0x20 && c != '|' && c != '\t')
            text[length++] = c;
        i++;
    }
    text[length] = '\0';

    // "I cannot help thee with that." leaves only its closing quote mark.
    if (strcspn(text, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") == (size_t)length)
        return;

    JournalPerson* person = FindPerson(s_mapId, s_npc, true);
    CopyField(person->name, sizeof(person->name), s_name);
    CopyField(person->desc, sizeof(person->desc), s_desc);
    Remember(person, s_keyword, text);
    Save();
}

void JOURNAL_Clear(void)
{
    s_loaded = true;
    s_topicOpen = false;
    FreeAll();
    Save();
}

int JOURNAL_PersonCount(void)
{
    EnsureLoaded();
    return s_personCount;
}

JournalPerson* JOURNAL_GetPerson(int index)
{
    return &s_persons[index];
}

const char* JOURNAL_PersonTitle(JournalPerson* person)
{
    for (int i = 0; i < person->entryCount; i++)
    {
        if (KeywordListHas(person->entries[i].keyword, "NAME") && person->name[0])
            return person->name;
    }
    return person->desc[0] ? person->desc : person->name;
}

static bool Contains(const char* text, const char* filter)
{
    size_t length = strlen(filter);

    for (; *text; text++)
    {
        size_t i;
        for (i = 0; i < length; i++)
        {
            if (ULTIMA_2032_ToUpper((u8)text[i]) != ULTIMA_2032_ToUpper((u8)filter[i]))
                break;
        }
        if (i == length)
            return true;
    }
    return length == 0;
}

bool JOURNAL_PersonMatches(JournalPerson* person, const char* filter)
{
    if (Contains(JOURNAL_PersonTitle(person), filter))
        return true;

    for (int i = 0; i < person->entryCount; i++)
    {
        if (Contains(person->entries[i].keyword, filter) || Contains(person->entries[i].text, filter))
            return true;
    }
    return false;
}

#endif
