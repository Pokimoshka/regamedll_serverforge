#include "precompiled.h"
#include "cppunitlite/TestHarness.h"

// Regression test for the SharedParse() global-buffer overflow.
// s_shared_token is char[1500]; SharedParse() must never emit a token that
// does not fit, regardless of the size of a single unquoted/quoted token in
// the input. On the unpatched code the oversized-token cases below smash the
// global buffer (crash / memory corruption) before the CHECK is even reached.
const int SHARED_TOKEN_SIZE = 1500; // must match char s_shared_token[1500]

TEST(RegularWordOverflow, SharedParse, 10)
{
	// One unquoted "word" of 5000 non-space characters (no quotes/specials).
	static char input[5001];
	Q_memset(input, 'A', sizeof(input) - 1);
	input[sizeof(input) - 1] = '\0';

	SharedParse(input);
	CHECK("oversized word must be truncated", Q_strlen(SharedGetToken()) < SHARED_TOKEN_SIZE);
}

TEST(QuotedStringOverflow, SharedParse, 10)
{
	// One quoted string of 5000 characters between the quotes.
	static char input[5003];
	input[0] = '\"';
	Q_memset(input + 1, 'B', 5000);
	input[5001] = '\"';
	input[5002] = '\0';

	SharedParse(input);
	CHECK("oversized quoted string must be truncated", Q_strlen(SharedGetToken()) < SHARED_TOKEN_SIZE);
}

TEST(NormalTokensStillWork, SharedParse, 10)
{
	// The fix must not change behavior for well-formed input.
	char input[] = "  primaryWeapon   \"hello world\"  , ";
	char *p = SharedParse(input);
	CHECK("plain word token", Q_strcmp(SharedGetToken(), "primaryWeapon") == 0);

	p = SharedParse(p);
	CHECK("quoted string token", Q_strcmp(SharedGetToken(), "hello world") == 0);

	p = SharedParse(p);
	CHECK("comma token", Q_strcmp(SharedGetToken(), ",") == 0);
}

// Regression tests for BufPrintf()/BufWPrintf() remaining-length accounting.
// The buggy code did `len -= Q_strlen(buf)` / `len -= wcslen(buf)`, which
// subtracts the TOTAL length of the buffer (including previously written
// content) instead of only the characters written by this call. On repeated
// appends the remaining-space counter went negative and callers could write
// out of bounds.
TEST(BufPrintfAppend, SharedUtil, 10)
{
	char buf[64] = { 0 };
	int remainder = sizeof(buf);

	char *tmp = BufPrintf(buf, remainder, "head:");
	CHECK("first write content", Q_strcmp(buf, "head:") == 0);
	CHECK("first remainder", remainder == (int)(sizeof(buf) - 5));
	CHECK("first pointer at end", tmp == buf + 5);

	tmp = BufPrintf(tmp, remainder, " %d", 42);
	CHECK("append content", Q_strcmp(buf, "head: 42") == 0);
	CHECK("second remainder", remainder == (int)(sizeof(buf) - 8));
	CHECK("second pointer at end", tmp == buf + 8);
}

TEST(BufPrintfNoSpace, SharedUtil, 10)
{
	char buf[16] = { 0 };
	int remainder = 0;

	char *tmp = BufPrintf(buf, remainder, "x");
	CHECK("returns nullptr when no space", tmp == nullptr);
}

TEST(BufPrintfTruncation, SharedUtil, 10)
{
	char buf[16] = { 0 };
	int remainder = 4; // only room for 3 chars + NUL

	char *tmp = BufPrintf(buf, remainder, "abcdefghij");
	CHECK("truncated to available space", Q_strlen(buf) <= 3);
	CHECK("remainder stays non-negative", remainder >= 0);
	CHECK("returns valid pointer", tmp != nullptr);
}

TEST(BufWPrintfAppend, SharedUtil, 10)
{
	wchar_t buf[64] = { 0 };
	int remainder = ARRAYSIZE(buf);

	wchar_t *tmp = BufWPrintf(buf, remainder, L"head:");
	CHECK("wide first write content", wcscmp(buf, L"head:") == 0);
	CHECK("wide first remainder", remainder == (int)(ARRAYSIZE(buf) - 5));

	tmp = BufWPrintf(tmp, remainder, L" %d", 42);
	CHECK("wide append content", wcscmp(buf, L"head: 42") == 0);
	CHECK("wide second remainder", remainder == (int)(ARRAYSIZE(buf) - 8));
}
