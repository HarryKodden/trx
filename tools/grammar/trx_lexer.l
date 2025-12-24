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
    YYLLOC.last_column = static_cast<int>(YYLLOC.first_column + yyleng); \
    YYLLOC.first_line = YYLLOC.last_line;                                \
    YYLLOC.last_line = YYLLOC.first_line;
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
{NEWLINE}       { ++YYLLOC.first_line; YYLLOC.last_line = YYLLOC.first_line; YYLLOC.first_column = 1; YYLLOC.last_column = 1; }

[Ee][Xx][Ee][Cc][ \t]+[Ss][Qq][Ll]  {
                    BEGIN(SQLMODE);
                    return EXEC_SQL;
                }

PROCEDURE       { return PROCEDURE; }
[Ll][Ii][Ss][Tt] { return LIST; }
[Ii][Nn][Cc][Ll][Uu][Dd][Ee]  { return INCLUDE; }
[Cc][Oo][Nn][Ss][Tt][Aa][Nn][Tt] { return CONSTANT; }
[Ff][Uu][Nn][Cc][Tt][Ii][Oo][Nn] { return FUNCTION; }
[Tt][Yy][Pp][Ee] { return TYPE; }
[Ff][Rr][Oo][Mm] { return FROM; }
[Tt][Aa][Bb][Ll][Ee] { return TABLE; }
[Nn][Uu][Ll][Ll] { return NULL_K; }
[Ii][Ff] { return IF; }
[Ee][Ll][Ss][Ee] { return ELSE; }
[Ww][Hh][Ii][Ll][Ee] { return WHILE; }
[Ff][Oo][Rr] { return FOR; }
[Ii][Nn] { return IN; }
[Ss][Ww][Ii][Tt][Cc][Hh] { return SWITCH; }
[Cc][Aa][Ss][Ee] { return CASE; }
[Dd][Ee][Ff][Aa][Uu][Ll][Tt] { return DEFAULT; }
[Tt][Rr][Yy] { return TRY; }
[Vv][Aa][Rr] { return VAR; }
[Cc][Aa][Tt][Cc][Hh] { return CATCH; }
[Tt][Hh][Rr][Oo][Ww] { return THROW; }
[Rr][Ee][Tt][Uu][Rr][Nn] { return RETURN; }
[Tt][Rr][Uu][Ee] { return TRUE; }
[Ff][Aa][Ll][Ss][Ee] { return FALSE; }
[Aa][Nn][Dd] { return AND; }
[Oo][Rr] { return OR; }
[Nn][Oo][Tt] { return NOT; }
[Jj][Ss][Oo][Nn] { return JSON; }
[Cc][Hh][Aa][Rr] { return _CHAR; }
[Ii][Nn][Tt][Ee][Gg][Ee][Rr] { return _INTEGER; }
[Ss][Mm][Aa][Ll][Ll][Ii][Nn][Tt] { return _SMALLINT; }
[Dd][Ee][Cc][Ii][Mm][Aa][Ll] { return _DECIMAL; }
[Bb][Oo][Oo][Ll][Ee][Aa][Nn] { return _BOOLEAN; }
[Ff][Ii][Ll][Ee] { return _FILE; }
[Bb][Ll][Oo][Bb] { return _BLOB; }
[Dd][Aa][Tt][Ee] { return DATE; }
[Tt][Ii][Mm][Ee] { return TIME; }
[Ww][Ee][Ee][Kk] { return WEEK; }
[Tt][Ii][Mm][Ee][Ss][Tt][Aa][Mm][Pp] { return TIMESTAMP; }
[Ww][Ee][Ee][Kk][Dd][Aa][Yy] { return WEEKDAY; }
[Ss][Qq][Ll][Cc][Oo][Dd][Ee] { return SQLCODE; }
[Gg][Ee][Tt] { return GET; }
[Pp][Oo][Ss][Tt] { return POST; }
[Pp][Uu][Tt] { return PUT; }
[Dd][Ee][Ll][Ee][Tt][Ee] { return DELETE; }
[Pp][Aa][Tt][Cc][Hh] { return PATCH; }
[Hh][Ee][Aa][Dd] { return HEAD; }
[Oo][Pp][Tt][Ii][Oo][Nn][Ss] { return OPTIONS; }
[Hh][Ee][Aa][Dd][Ee][Rr][Ss] { return HEADERS; }
[Bb][Oo][Dd][Yy] { return BODY; }
[Mm][Ee][Tt][Hh][Oo][Dd] { return METHOD; }
[Tt][Ii][Mm][Ee][Oo][Uu][Tt] { return TIMEOUT; }

"{"             { return LBRACE; }
"}"             { return RBRACE; }
"["             { return LBRACKET; }
"]"             { return RBRACKET; }
"("             { return LPAREN; }
")"             { return RPAREN; }
","             { return COMMA; }
";"             { return SEMICOLON; }
":="            { return ASSIGN; }
"<="            { return LE; }
">="            { return GE; }
"<>"            { return NEQ; }
"><"            { return NEQ2; }
"."             { return DOT; }

<SQLMODE>[ \t\r]+ {
                    YYLVAL.text = duplicate_text(yytext, yyleng);
                    return SQL_TEXT;
                }
<SQLMODE>\n+     {
                    YYLLOC.first_line += yyleng;
                    YYLLOC.last_line = YYLLOC.first_line;
                    YYLLOC.first_column = 1;
                    YYLLOC.last_column = 1;
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
