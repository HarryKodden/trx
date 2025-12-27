%option noyywrap
%option nodefault
%option reentrant
%option bison-bridge
%option bison-locations
%option warn

%x SQLMODE

%{
#define YY_NO_UNPUT 1

#include "trx/parsing/ParserDriver.h"
#include "trx/diagnostics/DiagnosticEngine.h"
#include "trx_parser.hpp"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

#define YY_DECL int yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, yyscan_t yyscanner)
#define YY_EXTRA_TYPE trx::parsing::ParserDriver*
#define YYLVAL (*yylval_param)
#define YYLLOC (*yylloc_param)

namespace {
char *duplicate_text(const char *text, std::size_t length) {
    char *copy = static_cast<char *>(std::malloc(length + 1));
    if (!copy) {
        return nullptr;
    }
    std::memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *duplicate_string_literal(const char *text, std::size_t length) {
    std::string buffer;
    buffer.reserve(length);
    for (std::size_t i = 1; i + 1 < length; ++i) {
        if (text[i] == '\\' && i + 1 < length - 1) {
            ++i;
            switch (text[i]) {
                case 'n': buffer.push_back('\n'); break;
                case 't': buffer.push_back('\t'); break;
                case '\\': buffer.push_back('\\'); break;
                case '"': buffer.push_back('"'); break;
                default: buffer.push_back(text[i]); break;
            }
        } else {
            buffer.push_back(text[i]);
        }
    }
    return duplicate_text(buffer.c_str(), buffer.size());
}
}

#define YY_USER_ACTION                                                   \
    YYLLOC.first_column = YYLLOC.last_column;                            \
    for (int i = 0; i < yyleng; ++i) {                                   \
        if (yytext[i] == '\n') {                                         \
            ++YYLLOC.last_line;                                          \
            YYLLOC.last_column = 1;                                      \
        } else {                                                         \
            ++YYLLOC.last_column;                                        \
        }                                                                \
    }                                                                    \
    YYLLOC.first_line = YYLLOC.last_line;
%}

WHITESPACE   [ \t\r\f]+
NEWLINE      [\n]
IDENT        [A-Za-z_][A-Za-z0-9_]*
PATH         [A-Za-z_][A-Za-z0-9_]*([.][A-Za-z0-9_]+)+
NUMBER       (-?[0-9]+(\.[0-9]+)?)
STRING       \"([^\"\\]|\\.)*\"
SSTRING      \'([^\'\\]|\\.)*\'
SQLIDENT     [A-Za-z_][A-Za-z0-9_]*
SQLPATH      {SQLIDENT}(\.{SQLIDENT})*
SQLTEXT      [^\s:;]+

%%

"//".*          { /* skip single line comment */ }
"/*"([^*]|\*+[^*/])*\*+"/"  { /* skip multi line comment */ }
{WHITESPACE}    { /* skip */ }
{NEWLINE}       { /* location already updated by YY_USER_ACTION */ }

<INITIAL>[Ee][Xx][Ee][Cc][ \t]+[Ss][Qq][Ll]  {
                    BEGIN(SQLMODE);
                    return EXEC_SQL;
                }

<SQLMODE>[ \t\r]+ {
                    YYLVAL.text = duplicate_text(yytext, yyleng);
                    return SQL_TEXT;
                }
<SQLMODE>\n+     {
                    /* location already updated by YY_USER_ACTION */
                    YYLVAL.text = duplicate_text(" ", 1);
                    return SQL_TEXT;
                }
<SQLMODE>":"{SQLPATH} {
                    YYLVAL.text = duplicate_text(yytext + 1, static_cast<std::size_t>(yyleng - 1));
                    return SQL_VARIABLE;
                }
<SQLMODE>";"    {
                    BEGIN(INITIAL);
                    return SEMICOLON;
                }
<SQLMODE>{SQLTEXT} {
                    YYLVAL.text = duplicate_text(yytext, yyleng);
                    return SQL_TEXT;
                }
<SQLMODE>.      {
                    YYLVAL.text = duplicate_text(yytext, 1);
                    return SQL_TEXT;
                }

<INITIAL>EXPORT          { return EXPORT; }
<INITIAL>PROCEDURE       { return PROCEDURE; }
<INITIAL>[Ll][Ii][Ss][Tt] { return LIST; }
<INITIAL>[Ii][Nn][Cc][Ll][Uu][Dd][Ee]  { return INCLUDE; }
<INITIAL>[Cc][Oo][Nn][Ss][Tt][Aa][Nn][Tt] { return CONSTANT; }
<INITIAL>[Ff][Uu][Nn][Cc][Tt][Ii][Oo][Nn] { return FUNCTION; }
<INITIAL>[Tt][Yy][Pp][Ee] { return TYPE; }
<INITIAL>[Ff][Rr][Oo][Mm] { return FROM; }
<INITIAL>[Tt][Aa][Bb][Ll][Ee] { return TABLE; }
<INITIAL>[Nn][Uu][Ll][Ll] { return NULL_K; }
<INITIAL>[Ii][Ff] { return IF; }
<INITIAL>[Ee][Ll][Ss][Ee] { return ELSE; }
<INITIAL>[Ww][Hh][Ii][Ll][Ee] { return WHILE; }
<INITIAL>[Ff][Oo][Rr] { return FOR; }
<INITIAL>[Ii][Nn] { return IN; }
<INITIAL>[Ss][Ww][Ii][Tt][Cc][Hh] { return SWITCH; }
<INITIAL>[Cc][Aa][Ss][Ee] { return CASE; }
<INITIAL>[Dd][Ee][Ff][Aa][Uu][Ll][Tt] { return DEFAULT; }
<INITIAL>[Tt][Rr][Yy] { return TRY; }
<INITIAL>[Vv][Aa][Rr] { return VAR; }
<INITIAL>[Cc][Aa][Tt][Cc][Hh] { return CATCH; }
<INITIAL>[Tt][Hh][Rr][Oo][Ww] { return THROW; }
<INITIAL>[Rr][Ee][Tt][Uu][Rr][Nn] { return RETURN; }
<INITIAL>[Tt][Rr][Uu][Ee] { return TRUE; }
<INITIAL>[Ff][Aa][Ll][Ss][Ee] { return FALSE; }
<INITIAL>[Aa][Nn][Dd] { return AND; }
<INITIAL>[Oo][Rr] { return OR; }
<INITIAL>[Nn][Oo][Tt] { return NOT; }
<INITIAL>[Jj][Ss][Oo][Nn] { return JSON; }
<INITIAL>[Cc][Hh][Aa][Rr] { return _CHAR; }
<INITIAL>[Ii][Nn][Tt][Ee][Gg][Ee][Rr] { return _INTEGER; }
<INITIAL>[Ss][Mm][Aa][Ll][Ll][Ii][Nn][Tt] { return _SMALLINT; }
<INITIAL>[Dd][Ee][Cc][Ii][Mm][Aa][Ll] { return _DECIMAL; }
<INITIAL>[Bb][Oo][Oo][Ll][Ee][Aa][Nn] { return _BOOLEAN; }
<INITIAL>[Ff][Ii][Ll][Ee] { return _FILE; }
<INITIAL>[Bb][Ll][Oo][Bb] { return _BLOB; }
<INITIAL>[Dd][Aa][Tt][Ee] { return DATE; }
<INITIAL>[Tt][Ii][Mm][Ee] { return TIME; }
<INITIAL>[Ww][Ee][Ee][Kk] { return WEEK; }
<INITIAL>[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp] { return TIMESTAMP; }
<INITIAL>[Ww][Ee][Ee][Kk][Dd][Aa][Yy] { return WEEKDAY; }
<INITIAL>[Ss][Qq][Ll][Cc][Oo][Dd][Ee] { return SQLCODE; }
<INITIAL>[Gg][Ee][Tt] { return GET; }
<INITIAL>[Pp][Oo][Ss][Tt] { return POST; }
<INITIAL>[Pp][Uu][Tt] { return PUT; }
<INITIAL>[Dd][Ee][Ll][Ee][Tt][Ee] { return DELETE; }
<INITIAL>[Pp][Aa][Tt][Cc][Hh] { return PATCH; }
<INITIAL>[Hh][Ee][Aa][Dd] { return HEAD; }
<INITIAL>[Oo][Pp][Tt][Ii][Oo][Nn][Ss] { return OPTIONS; }
<INITIAL>[Hh][Ee][Aa][Dd][Ee][Rr][Ss] { return HEADERS; }
<INITIAL>[Bb][Oo][Dd][Yy] { return BODY; }
<INITIAL>[Mm][Ee][Tt][Hh][Oo][Dd] { return METHOD; }
<INITIAL>[Tt][Ii][Mm][Ee][Oo][Uu][Tt] { return TIMEOUT; }

"{"             { return LBRACE; }
"}"             { return RBRACE; }
"["             { return LBRACKET; }
"]"             { return RBRACKET; }
"("             { return LPAREN; }
")"             { return RPAREN; }
","             { return COMMA; }
";"             { return SEMICOLON; }
"/"             { return SLASH; }
":="            { return ASSIGN; }
"<="            { return LE; }
">="            { return GE; }
"<>"            { return NEQ; }
"><"            { return NEQ2; }
"."             { return DOT; }

{STRING}        {
                    YYLVAL.text = duplicate_string_literal(yytext, yyleng);
                    return STRING;
                }
{SSTRING}       {
                    YYLVAL.text = duplicate_string_literal(yytext, yyleng);
                    return STRING;
                }
{PATH}          {
                    YYLVAL.text = duplicate_text(yytext, yyleng);
                    return PATH;
                }
{NUMBER}        {
                    YYLVAL.number = std::strtod(yytext, nullptr);
                    return NUMBER;
                }
{IDENT}         {
                    YYLVAL.text = duplicate_text(yytext, yyleng);
                    return IDENT;
                }
.               {
                    return yytext[0];
                }

%%

#undef YYLVAL
#undef YYLLOC
#undef YY_USER_ACTION
