/**** BEGIN LICENSE BLOCK ****

BSD 3-Clause License

Copyright (c) 2023, the wind.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

**** END LICENCE BLOCK ****/

#include "ffd_parser.h"

FFD_NAMESPACE

#define FFD_ENSURE_LFFD(C,M) { \
    if (! (C)) { Dbg << "Error around line " << (*this).Line () \
        << ", column: " << (*this).Column () \
        << "; code:" << __FILE__ << ":" << __LINE__ << ": " << M <<  EOL; \
        OS::Exit (1); }}

bool FFDParser::IsWhitespace() { return _buf[_i] <= 32; }
static bool is_line_whitespace(byte b) { return 32 == b || 9 == b; }
bool FFDParser::IsLineWhitespace() { return 32 == _buf[_i] || 9 == _buf[_i]; }
bool FFDParser::IsComment()
{
    return '/' == _buf[_i] && _i < _len-1
        && ('/' == _buf[_i+1] || '*' == _buf[_i+1]);
}
bool FFDParser::IsEol()
{
    if ('\r' == _buf[_i] && _i < _len-1) return '\n' == _buf[_i+1];
    return '\n' == _buf[_i];
}
void FFDParser::SkipEol()
{
    if ('\r' == _buf[_i] && _i < _len-1 && '\n' == _buf[_i+1])
        _i+=2, _line++, _column = 1;
    else if ('\n' == _buf[_i]) _i++, _line++, _column = 1;
}

// afterwards:
// i points: \n\n
//            i
//           \r\n\r\n
//              i
void FFDParser::SkipUntilDoubleEol()
{
    ReadWhile ("later   ", [&](){
        if ('\n' != _buf[_i]) return true;
        if (_i < _len-1 && '\n' == _buf[_i+1]) return false; // LFLF
        if ((_i > 0 && '\r' == _buf[_len-1])
            && (_i < _len-2 && '\r' == _buf[_i+1] && '\n' == _buf[_i+2]))
            return false; // CRLFCRLF
        return true;
    });
}

void FFDParser::ReadNonWhiteSpace()
{
    ReadWhile ("nw space", [&](){ return _buf[_i] > 32 && _buf[_i] <= 126; });
}

void FFDParser::SkipWhitespace()
{
    ReadWhile ("w  space", [&](){ return _buf[_i] <= 32; });
}

void FFDParser::SkipLineWhitespace()
{
    // A perfect line.
    ReadWhile ("lwhitesp", [&](){ return IsLineWhitespace (); });
}

// ensure is_comment() returns true prior calling this one
void FFDParser::SkipComment()
{
    if ('/' == _buf[_i+1])
        ReadWhile ("comment1", [&](){ return ! IsEol (); });
    else if ('*' == _buf[_i+1]) {
        // Yes, that would be slow if I had to parse 60 files per second ...
        ReadWhile ("commentn", [&](){
            return ! ('*' == _buf[_i] && _i < _len-1 && '/' == _buf[_i+1]); });
        _i += 2; // it ends at '*', so
    }
}

bool FFDParser::SymbolValid1st() { return SymbolValid1st (_buf[_i]); }
/*static*/ bool FFDParser::SymbolValid1st(byte b)
{
    // [A-Za-z_]
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || '_' == b;
}
/*static*/ bool FFDParser::SymbolValidNth(byte b)
{
    // [0-9A-Za-z_]
    return (b >= '0' && b <= '9') || SymbolValid1st (b);
}

// Handles variadic list symbols: foo.bar:value-list.
// char stop_at = '\0', bool allow_dot = false
// Handles expression symbols, like "foo)".
String FFDParser::ReadSymbol(char stop_at, bool allow_dot)
{
    FFD_ENSURE_LFFD(SymbolValid1st (), "Wrong symbol name")
    int j = _i;
    ReadWhile ("symbol  ", [&](){
        return _buf[_i] != stop_at && _buf[_i] > 32 && _buf[_i] <= 126; });
    FFD_ENSURE_LFFD((_i - j) <= FFD_SYMBOL_MAX_LEN, "Symbol too long")
    for (int k = j; k < _i; k++) // repeating, but simplifies the code
        FFD_ENSURE_LFFD(SymbolValidNth (_buf[k])
            || (allow_dot && '.' == _buf[k]), "Wrong symbol name")
    return static_cast<String &&>(String {_buf+j, _i-j});
}

// Handle symbols like: token<token,token>
List<String> FFDParser::TokenizeUntilWhiteSpace(const char * d)
{
    List<String> r {};
    int j = _i;
    auto a_delimiter = [](const char * l, const byte & c) {
        while (*l) if (*l++ == c) return true; return false;
    }; // "gnu c", pay attention
    while (_i < _len && _buf[_i] > 32 && _buf[_i] <= 126)
        if (a_delimiter (d, _buf[_i++]))
            r.Put (static_cast<String &&>(String {_buf+j, _i-1-j})), j = _i;
    if (_i > j) r.Put (String {_buf+j, _i-j});
    return r;
}

static inline bool is_decimal_number(byte b)
{
    return b >= '0' && b <= '9';
}
static inline bool is_hexadecimal_number(byte b)
{
    return (b >= 'a' && b <= 'f') || (b >= 'A' && b <= 'F')
        || is_decimal_number (b);
}
static inline bool is_upper(byte b)
{
    return (b >= 'A' && b <= 'Z');
}

/*static*/ bool FFDParser::IsIntLiteral(const byte * buf, int len)
{
    int i{};
    while ((len > 0 ? i < len : buf[i]))
        if (is_decimal_number (buf[i])) i++;
        else return false;
    return i > 0;
}

/*static*/ bool FFDParser::IsIntLiteral(const String & s)
{
    return IsIntLiteral (
        reinterpret_cast<const byte *>(s.AsZStr ()), s.Length ());
}

/*static*/ int FFDParser::ToInt(const String & s)
{
    int foo{};
    return FFDParser::ParseIntLiteral (
        reinterpret_cast<const byte *>(s.AsZStr ()), s.Length (), foo);
}

//TODO testme
/*static*/ int FFDParser::ParseIntLiteral(const byte * buf, int len,
    int & reuse)
{
    static int const N[16] {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    static unsigned int const P[10] {1,10,100,1000,10000,100000,1000000,
        10000000,100000000,1000000000};
    int result = 1, i = 0; bool h = false;
    if ('-' == buf[i]) { result = -1; i++; }
    else if ('0' == buf[i] && i+1 < len && 'x' == buf[i+1]) {
        i += 2;
        h = true;
    }
    // 123 = 3*(10^0) + 2*(10^1) + 1*(10^2)
    // 1ff = f*(16^0) + f*(16^1) + 1*(16^2)
    FFD_ENSURE(i < len, "Incomplete integer literal") // -EOF
    // integer: nnnnnnnnnn; hexadecimal int: nnnnnnnn
    int j = i; unsigned int tmp_result = 0;
    if (h) {
        while (i < len && is_hexadecimal_number (buf[i])) i++;
        FFD_ENSURE(i - j <= 8, "Integer literal too long")
        for (int k = i-1, p = 1; k >= j; k--, p *= (k >= j ? 16 : 1)) {
            int number = is_decimal_number (buf[k]) ? buf[k] - '0'
                : 10 + buf[k] - (is_upper (buf[k]) ? 'A' : 'a');
            tmp_result += N[number] * p;
        }
    }
    else {
        while (i < len && is_decimal_number (buf[i])) i++;
        FFD_ENSURE(i - j <= 10, "Integer literal too long")
        for (int k = i-1, p = -1; k >= j; k--)
            tmp_result += N[buf[k] - '0'] * P[++p];
    }
    return reuse = i, result * static_cast<int>(tmp_result);
}// FFDParser::ParseIntLiteral()

//TODO testme
int FFDParser::ParseIntLiteral()
{
#ifndef FFD_PARSERER_INT_LITERAL_DETAILS
    int len{};
    int result = ParseIntLiteral (_buf+_i, _len-_i, len);
    return _i += len, result;
#else
    static int const N[16] {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    static unsigned int const P[10] {1,10,100,1000,10000,100000,1000000,
        10000000,100000000,1000000000};
    int result = 1; bool h = false;
    if ('-' == _buf[_i]) { result = -1; _i++; }
    else if ('0' == _buf[_i] && _i+1 < _len && 'x' == _buf[_i+1]) {
        _i += 2;
        h = true;
    }
    // 123 = 3*(10^0) + 2*(10^1) + 1*(10^2)
    // 1ff = f*(16^0) + f*(16^1) + 1*(16^2)
    FFD_ENSURE_LFFD(_i < _len, "Incomplete integer literal") // -EOF
    // integer: nnnnnnnnnn; hexadecimal int: nnnnnnnn
    int j = _i; unsigned int tmp_result = 0;
    if (h) {
        ReadWhile ("int lit.",
                    [&](){ return is_hexadecimal_number (_buf[_i]); });
        FFD_ENSURE_LFFD(_i - j <= 8, "Integer literal too long")
        for (int k = _i-1, p = 1; k >= j; k--, p *= (k >= j ? 16 : 1)) {
            int number = is_decimal_number (_buf[k]) ? _buf[k] - '0'
                : 10 + _buf[k] - (is_upper (_buf[k]) ? 'A' : 'a');
            tmp_result += N[number] * p;
        }
    }
    else {
        ReadWhile ("int lit.", [&](){ return is_decimal_number (_buf[_i]); });
        FFD_ENSURE_LFFD(_i - j <= 10, "Integer literal too long")
        for (int k = _i-1, p = -1; k >= j; k--)
            tmp_result += N[_buf[k] - '0'] * P[++p];
    }
    return result * static_cast<int>(tmp_result);
#endif
}// FFDParser::ParseIntLiteral()

// Multi-line not handled.
String FFDParser::ReadExpression(char open, char close)
{
    FFD_ENSURE_LFFD(open != '(' && close != ')',
        "These are handled by TokenizeExpression()")
    int b = 0;
    bool e = open ^ close;
    FFD_ENSURE_LFFD(open == _buf[_i], "An expression must start with (")
    int j = _i;
    ReadWhile("expr.   ",
        [&](){
            // Wrong symbols shall be detected at the evaluator - no point to
            // evaluate here.
            if (e) {
                if (open == _buf[_i]) b++;
                else if (close == _buf[_i]) b--;
            } else {
                if (open == _buf[_i] && 0 == b) b++;
                else if (close == _buf[_i]) b--; //TODO and not "\"-ed
            }
            FFD_ENSURE_LFFD(
                b >= 0 && b <= FFD_EXPR_MAX_NESTED_EXPR, "Wrong expr.")
            return b > 0 && _buf[_i] >= 32 && _buf[_i] <= 126;});
    // It shall complete on "close"
    FFD_ENSURE_LFFD(close == _buf[_i], "Incomplete expr.")
    FFD_ENSURE_LFFD(0 == b, "Bug: incomplete expr.")
    _i++;
    FFD_ENSURE_LFFD((_i - j) <= FFD_EXPR_MAX_LEN, "Expr. too long")
    return static_cast<String &&>(String {_buf+j, _i-j});
}

// Skip any sequence of white-space and comments (MAX_SW_ITEMS max), up to and
// including an EOL|EOF (multi-line comment EOL(s) doesn't count).
// Long name on purpose. TODO re-design me for a short name, or modify the
// grammar.
void FFDParser::SkipCommentWhitespaceSequence()
{
    int const MAX_SW_ITEMS {10};
    for (int chk = 0; _i < _len; chk++) {
        FFD_ENSURE_LFFD(chk < MAX_SW_ITEMS,
            "I'm sorry, this ain't a novelette file format")
        if (IsLineWhitespace ()) SkipLineWhitespace ();
        else if (IsComment ()) SkipComment ();
        else if (IsEol ()) {
            SkipEol ();
            break;
        }
        else
            FFD_ENSURE_LFFD(42^42, "Unexpected element")
    }
}

// String FFDParser::ReadValueList()
List<FFDParser::VLItem> FFDParser::ReadValueList()
{
    List<FFDParser::VLItem> result {};
    Dbg << "val-list: ";
    bool a {};
    for (;; a = true) {
        FFDParser::VLItem itm {};
        itm.A = ParseIntLiteral ();
        if (a) Dbg << ", ";
        if ('-' == _buf[_i]) {
            _i++;
            FFD_ENSURE_LFFD(HasMoreData (), "Incomplete val-list") // -EOF
            itm.B = ParseIntLiteral (); Dbg << itm.A << "-" << itm.B;
        }
        else {
            itm.B = itm.A; Dbg << itm.A;
        }
        FFD_ENSURE_LFFD(itm.A <= itm.B, "Wrong val-list: a can't be > b")
        result.Add (itm);
        if (',' != _buf[_i]) break; else {
            _i++;
            FFD_ENSURE_LFFD(HasMoreData (), "Incomplete val-list") // ,EOF
        }
    }
    Dbg << EOL;
    return static_cast<List<FFDParser::VLItem> &&>(result);
}

void FFDParser::ReadVariadicField()
{
    FFD_ENSURE_LFFD(_i < _len-2, "Incomplete variadic field")
    FFD_ENSURE_LFFD('.' == _buf[_i+1], "Incomplete variadic field")
    FFD_ENSURE_LFFD('.' == _buf[_i+2], "Incomplete variadic field")
    _i+=3;
}

String FFDParser::ReadArrDim()
{
    int s = _i;
    ReadWhile ("arr     ", [&](){ return ']' != _buf[_i]; });
    FFD_ENSURE_LFFD(']' == _buf[_i], "Incomplete array")
    FFD_ENSURE_LFFD(_i-s <= FFD_MAX_ARR_EXPR_LEN,
        "Simplify your array expression")
    _i++;
    return static_cast<String &&>(String {_buf+s, _i-1-s});
}

String FFDParser::ReadStringLiteral()
{
    FFD_ENSURE_LFFD(_i < _len-1, "Incomplete string literal") // "EOF
    //LATER fix: yep, it shall read "a"a"a"; also, add \-ing
    return static_cast<String &&>(ReadExpression ('"', '"'));
}

// Handle supported operations: > < == >= <= != || && ! .
// Makes FFDParser::TokenizeExpression wy more read-able.
FFDParser::ExprTokenType FFDParser::TokenizeExpressionOp()
{
    FFD_ENSURE_LFFD(_i < _len-2, "Incomplete expr.")
    switch (_buf[_i]) {
        case '\n': _line++; case '\r': return ++_i, ExprTokenType::None;
        case '!':
            switch (_buf[_i+1]) {
                case '=' :
                    FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
                    Dbg << "!= "; return _i+=2, ExprTokenType::opNE;
                case '(' : Dbg << "! "; return ++_i, ExprTokenType::opN;
                default:
                    FFD_ENSURE_LFFD(SymbolValid1st (_buf[_i+1]), "Wrong Op");
                    Dbg << "! "; return ++_i, ExprTokenType::opN;
            }
        case '<':
            if (is_line_whitespace (_buf[_i+1]))
                return ++_i, Dbg << "< ", ExprTokenType::opL;
            FFD_ENSURE_LFFD('=' == _buf[_i+1], "Wrong Op")
            FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
            Dbg << "<= ";
            return _i+=2, ExprTokenType::opLE;
        case '>':
            if (is_line_whitespace (_buf[_i+1]))
                return ++_i, Dbg << "> ", ExprTokenType::opG;
            FFD_ENSURE_LFFD('=' == _buf[_i+1], "Wrong Op")
            FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
            Dbg << ">= ";
            return _i+=2, ExprTokenType::opGE;
        case '=':
            FFD_ENSURE_LFFD('=' == _buf[_i+1], "Wrong Op")
            FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
            Dbg << "== ";
            return _i+=2, ExprTokenType::opE;
        case '|':
            FFD_ENSURE_LFFD('|' == _buf[_i+1], "Wrong Op")
            FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
            Dbg << "|| ";
            return _i+=2, ExprTokenType::opOr;
        case '&':
            // require ' ' after &
            if (' ' == _buf[_i+1]) return _i+=1, ExprTokenType::opBWAnd;
            FFD_ENSURE_LFFD('&' == _buf[_i+1], "Wrong Op")
            FFD_ENSURE_LFFD(is_line_whitespace (_buf[_i+2]), "Wrong Op")
            Dbg << "&& ";
            return _i+=2, ExprTokenType::opAnd;
        default: Dbg << "\"" << _buf[_i] << "\" <- ";
                 FFD_ENSURE_LFFD(1^1, "Unknown Op")
    }// switch (_buf[_i])
}// FFDParser::TokenizeExpressionOp()

// Handle (.*)
List<FFDParser::ExprToken> FFDParser::TokenizeExpression()
{
    Dbg << "TokenizeExpression: ";
    List<FFDParser::ExprToken> result {}; // (, foo, )
    int depth {};
    int chk {};
    do {
        if ('(' == _buf[_i]) { Dbg << "( ";
            FFD_ENSURE_LFFD(chk++ < FFD_EXPR_MAX_NESTED_EXPR, "Wrong expr.")
            depth++; _i++;
            result.Put (FFDParser::ExprToken {ExprTokenType::Open});
        }
        else if (')' == _buf[_i]) { Dbg << ") ";
            depth--; if (depth) _i++;
            result.Put (FFDParser::ExprToken {ExprTokenType::Close});
        }
        else if (SymbolValid1st (_buf[_i])) {
            FFDParser::ExprToken t {ExprTokenType::Symbol};
            t.Symbol = ReadSymbol (')', true); Dbg << "{" << t.Symbol << "} ";
            result.Put (static_cast<FFDParser::ExprToken &&>(t));
        }
        else if (is_decimal_number (_buf[_i])) {
            FFDParser::ExprToken t {ExprTokenType::Number};
            t.Value = ParseIntLiteral (); Dbg << t.Value << " ";
            result.Put (static_cast<FFDParser::ExprToken &&>(t));
        }
        else if (IsLineWhitespace ()) { Dbg << "{} "; SkipLineWhitespace (); }
        else
            result.Put (FFDParser::ExprToken {TokenizeExpressionOp ()});
    } while (depth && _i < _len);
    FFD_ENSURE_LFFD(')' == _buf[_i], "Incomplete expr.")
    FFD_ENSURE_LFFD(0 == depth, "Bug: incomplete expr.")
    _i++;
    Dbg << EOL;
    return static_cast<List<ExprToken> &&>(result);
}// FFDParser::TokenizeExpression()

#undef FFD_ENSURE_LFFD

NAMESPACE_FFD
