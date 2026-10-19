// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include <string.h>

#include "cli/command_parser.h"

namespace
{
    //
    //  A minimal in-memory input stream for driving CommandParser.  It serves
    //  a fixed list of pre-prepared lines (without their newlines) one at a
    //  time, matching how the real character_istream feeds getline().
    //
    class MockInputStream : public minstd::istream<char>
    {
    public:
        MockInputStream(const char *const *lines, size_t count)
            : lines_(lines), count_(count)
        {
        }

        minstd::istream<char> &getline(minstd::buffer<char> &buffer, char) override
        {
            if (index_ < count_)
            {
                const char *line = lines_[index_++];
                buffer.append(line, strnlen(line, 65536));
            }
            return *this;
        }

    private:
        const char *const *lines_;
        size_t count_;
        size_t index_ = 0;
    };

    //
    //  Convenience: parse a single line and return its first token.
    //
    const char *ParseFirstToken(const char *line)
    {
        const char *lines[] = {line};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);
        return parser.GetNextLine();
    }

    //
    //  Convenience: parse a single line and collect every token into out[].
    //  Returns the number of tokens collected.
    //
    size_t CollectTokens(const char *line, const char **out, size_t max_tokens)
    {
        const char *lines[] = {line};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);

        size_t count = 0;
        const char *token = parser.GetNextLine();
        while (token != nullptr && count < max_tokens)
        {
            out[count++] = token;
            token = parser.NextToken();
        }
        return count;
    }

// ============================================================================
//  Single-token lines
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserSingleTokenTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserSingleTokenTests, BareWordIsReturnedWhole)
    {
        STRCMP_EQUAL("hello", ParseFirstToken("hello"));
        STRCMP_EQUAL("ls", ParseFirstToken("ls"));
        STRCMP_EQUAL("a", ParseFirstToken("a"));
    }

    TEST(CommandParserSingleTokenTests, LeadingAndTrailingWhitespaceIsTrimmed)
    {
        STRCMP_EQUAL("hello", ParseFirstToken("   hello"));
        STRCMP_EQUAL("hello", ParseFirstToken("hello   "));
        STRCMP_EQUAL("hello", ParseFirstToken("\thello\t"));
    }

    TEST(CommandParserSingleTokenTests, TokenWithNoSpacesIsNotSplit)
    {
        STRCMP_EQUAL("hello-world", ParseFirstToken("hello-world"));
        STRCMP_EQUAL("/usr/bin/ls", ParseFirstToken("/usr/bin/ls"));
    }

    TEST(CommandParserSingleTokenTests, SingleTokenYieldsNullOnNextToken)
    {
        const char *lines[] = {"hello"};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);

        STRCMP_EQUAL("hello", parser.GetNextLine());
        CHECK_TRUE(parser.NextToken() == nullptr);
        CHECK_TRUE(parser.NextToken() == nullptr);
    }

// ============================================================================
//  Multi-token lines
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserMultiTokenTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserMultiTokenTests, SpacesSeparateTokens)
    {
        const char *tokens[8];
        CHECK_EQUAL(3, CollectTokens("hello world foo", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world", tokens[1]);
        STRCMP_EQUAL("foo", tokens[2]);
    }

    TEST(CommandParserMultiTokenTests, TabsSeparateTokens)
    {
        const char *tokens[8];
        CHECK_EQUAL(2, CollectTokens("hello\tworld", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world", tokens[1]);
    }

    TEST(CommandParserMultiTokenTests, MixedSpaceAndTabSeparators)
    {
        const char *tokens[8];
        CHECK_EQUAL(3, CollectTokens("hello \t world\tfoo", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world", tokens[1]);
        STRCMP_EQUAL("foo", tokens[2]);
    }

    TEST(CommandParserMultiTokenTests, RunsOfDelimitersCollapseToOneGap)
    {
        const char *tokens[8];
        CHECK_EQUAL(2, CollectTokens("hello     world", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world", tokens[1]);
    }

    TEST(CommandParserMultiTokenTests, TrailingDelimiterDoesNotCreateEmptyToken)
    {
        const char *tokens[8];
        CHECK_EQUAL(2, CollectTokens("hello world   ", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world", tokens[1]);
    }

// ============================================================================
//  Quoted literals
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserQuotedLiteralTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserQuotedLiteralTests, DoubleQuotedLiteralStripsQuotes)
    {
        STRCMP_EQUAL("hello world", ParseFirstToken("\"hello world\""));
    }

    TEST(CommandParserQuotedLiteralTests, SingleQuotedLiteralStripsQuotes)
    {
        STRCMP_EQUAL("hello world", ParseFirstToken("'hello world'"));
    }

    TEST(CommandParserQuotedLiteralTests, LiteralPreservesInternalSpaces)
    {
        const char *tokens[8];
        CHECK_EQUAL(2, CollectTokens("cmd \"a b c\"", tokens, 8));
        STRCMP_EQUAL("cmd", tokens[0]);
        STRCMP_EQUAL("a b c", tokens[1]);
    }

    TEST(CommandParserQuotedLiteralTests, LiteralAmongOtherTokens)
    {
        const char *tokens[8];
        CHECK_EQUAL(3, CollectTokens("hello \"world foo\" bar", tokens, 8));
        STRCMP_EQUAL("hello", tokens[0]);
        STRCMP_EQUAL("world foo", tokens[1]);
        STRCMP_EQUAL("bar", tokens[2]);
    }

    TEST(CommandParserQuotedLiteralTests, ConsecutiveLiteralsAreSeparateTokens)
    {
        const char *tokens[8];
        CHECK_EQUAL(2, CollectTokens("\"one two\" 'three four'", tokens, 8));
        STRCMP_EQUAL("one two", tokens[0]);
        STRCMP_EQUAL("three four", tokens[1]);
    }

    TEST(CommandParserQuotedLiteralTests, WhitespaceAroundLiteralIsTrimmed)
    {
        STRCMP_EQUAL("hello world", ParseFirstToken("  \"hello world\"  "));
    }

// ============================================================================
//  Empty and whitespace-only input
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserEmptyInputTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserEmptyInputTests, EmptyLineYieldsNullToken)
    {
        CHECK_TRUE(ParseFirstToken("") == nullptr);
    }

    TEST(CommandParserEmptyInputTests, WhitespaceOnlyLineYieldsNullToken)
    {
        CHECK_TRUE(ParseFirstToken("   ") == nullptr);
        CHECK_TRUE(ParseFirstToken("\t\t") == nullptr);
        CHECK_TRUE(ParseFirstToken(" \t ") == nullptr);
    }

    TEST(CommandParserEmptyInputTests, EndOfStreamYieldsNullToken)
    {
        const char *lines[] = {"hello"};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);

        //  First line is consumed
        STRCMP_EQUAL("hello", parser.GetNextLine());

        //  No more lines: getline() appends nothing, input_ is empty
        CHECK_TRUE(parser.GetNextLine() == nullptr);
        CHECK_TRUE(parser.GetNextLine() == nullptr);
    }

// ============================================================================
//  Multi-line input
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserMultiLineTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserMultiLineTests, SuccessiveLinesAreParsedIndependently)
    {
        const char *lines[] = {"first", "second", "third"};
        MockInputStream stream(lines, 3);
        cli::CommandParser parser(stream);

        STRCMP_EQUAL("first", parser.GetNextLine());
        STRCMP_EQUAL("second", parser.GetNextLine());
        STRCMP_EQUAL("third", parser.GetNextLine());
    }

    TEST(CommandParserMultiLineTests, TokenizationResetsBetweenLines)
    {
        const char *lines[] = {"alpha beta", "gamma delta"};
        MockInputStream stream(lines, 2);
        cli::CommandParser parser(stream);

        //  Line 1
        STRCMP_EQUAL("alpha", parser.GetNextLine());
        STRCMP_EQUAL("beta", parser.NextToken());
        CHECK_TRUE(parser.NextToken() == nullptr);

        //  Line 2 starts fresh -- a stale saveptr must not leak across lines
        STRCMP_EQUAL("gamma", parser.GetNextLine());
        STRCMP_EQUAL("delta", parser.NextToken());
        CHECK_TRUE(parser.NextToken() == nullptr);
    }

    TEST(CommandParserMultiLineTests, BlankLineBetweenContentLines)
    {
        const char *lines[] = {"one", "", "two"};
        MockInputStream stream(lines, 3);
        cli::CommandParser parser(stream);

        STRCMP_EQUAL("one", parser.GetNextLine());
        CHECK_TRUE(parser.GetNextLine() == nullptr);
        STRCMP_EQUAL("two", parser.GetNextLine());
    }

// ============================================================================
//  CurrentToken tracking
// ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(CommandParserCurrentTokenTests)
    {};
#pragma GCC diagnostic pop

    TEST(CommandParserCurrentTokenTests, TracksTheMostRecentlyReturnedToken)
    {
        const char *lines[] = {"alpha beta"};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);

        const char *token = parser.GetNextLine();
        POINTERS_EQUAL(token, parser.CurrentToken());
        STRCMP_EQUAL("alpha", parser.CurrentToken());

        token = parser.NextToken();
        POINTERS_EQUAL(token, parser.CurrentToken());
        STRCMP_EQUAL("beta", parser.CurrentToken());
    }

    TEST(CommandParserCurrentTokenTests, ReflectsNullAfterExhaustion)
    {
        const char *lines[] = {"alpha"};
        MockInputStream stream(lines, 1);
        cli::CommandParser parser(stream);

        parser.GetNextLine();
        parser.NextToken();
        CHECK_TRUE(parser.NextToken() == nullptr);
        CHECK_TRUE(parser.CurrentToken() == nullptr);
    }

} // namespace
