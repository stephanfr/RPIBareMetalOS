// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/regex.h"

namespace
{
// ============================================================================
//  Literal matching
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexLiteralTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexLiteralTests, UnanchoredMatchReturnsIndex)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("cat", "cat", &len));
        CHECK_EQUAL(1, re_match("cat", "xcat", &len));
        CHECK_EQUAL(4, re_match("dog", "the dog", &len));
    }

    TEST(RegexLiteralTests, NoMatchReturnsNegativeOne)
    {
        int len = 0;

        CHECK_EQUAL(-1, re_match("cat", "dogs", &len));
        CHECK_EQUAL(-1, re_match("xyz", "abc", &len));
        CHECK_EQUAL(-1, re_match("cat", "", &len));
    }

    TEST(RegexLiteralTests, MatchLengthReflectsConsumedCharacters)
    {
        int len = 0;

        re_match("cat", "cat", &len);
        CHECK_EQUAL(3, len);

        re_match("dog", "the dog", &len);
        CHECK_EQUAL(3, len);
    }

// ============================================================================
//  Dot wildcard
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexDotTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexDotTests, DotMatchesAnySingleCharacter)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match(".at", "cat", &len));
        CHECK_EQUAL(0, re_match(".at", "bat", &len));
        CHECK_EQUAL(0, re_match(".at", "zat", &len));
        CHECK_EQUAL(3, len);
    }

    TEST(RegexDotTests, SingleDotMatchesFirstCharacter)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match(".", "hello", &len));
        CHECK_EQUAL(1, len);
    }

// ============================================================================
//  Anchors
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexAnchorTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexAnchorTests, BeginAnchorRequiresMatchAtStart)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("^cat", "cat", &len));
        CHECK_EQUAL(-1, re_match("^cat", "xcat", &len));
    }

    TEST(RegexAnchorTests, EndAnchorRequiresMatchAtEnd)
    {
        int len = 0;

        CHECK_EQUAL(2, re_match("cd$", "abcd", &len));
        CHECK_EQUAL(-1, re_match("cd$", "abcde", &len));
    }

    TEST(RegexAnchorTests, BothAnchorsRequireFullStringMatch)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("^cat$", "cat", &len));
        CHECK_EQUAL(-1, re_match("^cat$", "cats", &len));
        CHECK_EQUAL(-1, re_match("^cat$", "scat", &len));
    }

// ============================================================================
//  Quantifiers
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexQuantifierTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexQuantifierTests, StarMatchesZeroOrMore)
    {
        int len = 0;

        //  Zero occurrences still satisfies the pattern (empty match at start)
        CHECK_EQUAL(0, re_match("a*", "banana", &len));
        CHECK_EQUAL(0, len);

        //  Greedy: consumes the entire run between the surrounding literals
        CHECK_EQUAL(0, re_match("b[a]*b", "baaab", &len));
        CHECK_EQUAL(5, len);
    }

    TEST(RegexQuantifierTests, PlusMatchesOneOrMore)
    {
        int len = 0;

        //  Requires at least one occurrence
        CHECK_EQUAL(-1, re_match("a+", "bcd", &len));

        //  Finds the run and reports its length
        CHECK_EQUAL(1, re_match("a+", "baaac", &len));
        CHECK_EQUAL(3, len);
    }

    TEST(RegexQuantifierTests, QuestionMarkMakesPrecedingOptional)
    {
        int len = 0;

        //  colou?r matches both spellings
        CHECK_EQUAL(0, re_match("colou?r", "color", &len));
        CHECK_EQUAL(5, len);
        CHECK_EQUAL(0, re_match("colou?r", "colour", &len));
        CHECK_EQUAL(6, len);
    }

// ============================================================================
//  Character classes
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexCharClassTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexCharClassTests, CharacterClassMatchesAnyListedChar)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("[abc]", "ax", &len));
        CHECK_EQUAL(1, re_match("[abc]", "xb", &len));
        CHECK_EQUAL(2, re_match("[abc]", "xxc", &len));
        CHECK_EQUAL(-1, re_match("[abc]", "xyz", &len));
    }

    TEST(RegexCharClassTests, CharacterRangeMatchesWithinBounds)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("[a-z]", "m", &len));
        CHECK_EQUAL(-1, re_match("[a-z]", "M", &len));
        CHECK_EQUAL(0, re_match("[A-F]", "C", &len));
        CHECK_EQUAL(-1, re_match("[A-F]", "G", &len));
        CHECK_EQUAL(0, re_match("[0-9]", "7", &len));
        CHECK_EQUAL(-1, re_match("[0-9]", "x", &len));
    }

    TEST(RegexCharClassTests, MultipleRangesCombine)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("[a-zA-Z0-9]", "Q", &len));
        CHECK_EQUAL(0, re_match("[a-zA-Z0-9]", "k", &len));
        CHECK_EQUAL(0, re_match("[a-zA-Z0-9]", "9", &len));
        CHECK_EQUAL(-1, re_match("[a-zA-Z0-9]", "_", &len));
    }

// ============================================================================
//  Escaped character classes
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexEscapeClassTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexEscapeClassTests, DigitClassMatchesDigitsOnly)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\d", "1", &len));
        CHECK_EQUAL(-1, re_match("\\d", "a", &len));
        CHECK_EQUAL(1, re_match("\\d", "a1", &len));
    }

    TEST(RegexEscapeClassTests, NonDigitClassMatchesNonDigits)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\D", "a", &len));
        CHECK_EQUAL(-1, re_match("\\D", "5", &len));
        CHECK_EQUAL(0, re_match("\\D+", "ab12", &len));
        CHECK_EQUAL(2, len);
    }

    TEST(RegexEscapeClassTests, WordClassMatchesAlphanumericAndUnderscore)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\w", "_", &len));
        CHECK_EQUAL(0, re_match("\\w", "K", &len));
        CHECK_EQUAL(0, re_match("\\w", "7", &len));
        CHECK_EQUAL(-1, re_match("\\w", ".", &len));
        CHECK_EQUAL(0, re_match("\\w+", "user_name", &len));
        CHECK_EQUAL(9, len);
    }

    TEST(RegexEscapeClassTests, NonWordClassMatchesPunctuation)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\W", "-", &len));
        CHECK_EQUAL(-1, re_match("\\W", "a", &len));
        CHECK_EQUAL(-1, re_match("\\W", "_", &len));
    }

    TEST(RegexEscapeClassTests, WhitespaceClassMatchesWhitespace)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\s", " ", &len));
        CHECK_EQUAL(0, re_match("\\s", "\t", &len));
        CHECK_EQUAL(0, re_match("\\s", "\n", &len));
        CHECK_EQUAL(-1, re_match("\\s", "a", &len));
    }

    TEST(RegexEscapeClassTests, NonWhitespaceClassMatchesNonWhitespace)
    {
        int len = 0;

        CHECK_EQUAL(0, re_match("\\S", "a", &len));
        CHECK_EQUAL(-1, re_match("\\S", " ", &len));
        CHECK_EQUAL(0, re_match("\\S+", "ab cd", &len));
        CHECK_EQUAL(2, len);
    }

    TEST(RegexEscapeClassTests, EscapedMetacharMatchesLiterally)
    {
        int len = 0;

        //  \. matches a literal dot rather than acting as a wildcard
        CHECK_EQUAL(1, re_match("\\.", "a.b", &len));
        CHECK_EQUAL(1, len);
        CHECK_EQUAL(-1, re_match("\\.", "aXb", &len));
    }

// ============================================================================
//  Compilation and reuse
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(RegexCompileMatchTests)
    {};
#pragma GCC diagnostic pop

    TEST(RegexCompileMatchTests, CompileReturnsNonNullForValidPatterns)
    {
        CHECK_TRUE(re_compile("abc") != nullptr);
        CHECK_TRUE(re_compile("[a-z]+") != nullptr);
        CHECK_TRUE(re_compile("^colou?r$") != nullptr);
    }

    TEST(RegexCompileMatchTests, CompileReturnsNullForInvalidPatterns)
    {
        //  Unterminated character class
        CHECK_TRUE(re_compile("[abc") == nullptr);
        //  Negated class with no members
        CHECK_TRUE(re_compile("[^") == nullptr);
    }

    TEST(RegexCompileMatchTests, CompiledPatternMatchesMultipleTexts)
    {
        re_t pattern = re_compile("[0-9]+");
        int len = 0;

        CHECK_EQUAL(0, re_matchp(pattern, "7", &len));
        CHECK_EQUAL(1, len);
        CHECK_EQUAL(2, re_matchp(pattern, "id42x", &len));
        CHECK_EQUAL(2, len);
        CHECK_EQUAL(-1, re_matchp(pattern, "abc", &len));
    }
}
