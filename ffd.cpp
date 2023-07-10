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

// All a have is a war I can not win, but I can never stop fighting.

#include "ffd.h"
#include "ffd_node.h"

#include <new>

FFD_NAMESPACE

// 1000 classes? No thanks.
template <typename F> struct KwParser final // Keyword Parser
{
    const int KwLen; // Keyword Length - no need to compute it
    const char * Kw; // Keyword
    F Parse;         // parse function
};

bool FFD::SNode::Parse(FFDParser & parser)
{
    static int const KW_LEN {9};
    static KwParser<bool (FFD::SNode::*)(FFDParser &)>
        const KW[KW_LEN] {
            {4, "type", &FFD::SNode::ParseMachType},
            {3, "???", &FFD::SNode::ParseLater},
            {1, "[", &FFD::SNode::ParseAttribute},
            {4, "list", &FFD::SNode::ParseLater},
            {5, "table", &FFD::SNode::ParseLater},
            {6, "struct", &FFD::SNode::ParseStruct},
            {6, "format", &FFD::SNode::ParseStruct},
            {5, "const", &FFD::SNode::ParseConst},
            {4, "enum", &FFD::SNode::ParseEnum}};

    int j = parser.Tell ();
    // Attribute "correction"
    // Dbg << "column: " << parser.Column () << ", buf[j]:"
    //     << Dbg.Fmt (%2X,*(parser.BufAt (j))) << EOL;
    while (1 == parser.Column () && parser.AtAttributeStart ())
        return ParseAttribute (parser);
    //
    parser.ReadNonWhiteSpace ();
    int kw_len = parser.Tell () - j;
    for (auto & kwp : KW)
        if (kwp.KwLen == kw_len && ! OS::Strncmp (
            kwp.Kw, parser.BufAt (j), kw_len)) {
            FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete node") // .*EOF
            FFD_ENSURE_FFD(! parser.IsEol (), "Incomplete node") // .*EOL
            if ((this->*kwp.Parse) (parser)) {
                if (6 == kwp.KwLen && ! OS::Strncmp (kwp.Kw, "format", 6))
                    Type = FFD::SType::Format;
                return true;
            }
            else
                return false;
        }
    return false;
}// FFD::SNode::Parse()

// type {symbol} {literal}|{symbol} [{expr}]
//     i
// Single line. An EOL completes it.
bool FFD::SNode::ParseMachType(FFDParser & parser)
{
    Type = FFD::SType::MachType;

    parser.SkipLineWhitespace ();
    // name
    Name = static_cast<String &&>(parser.ReadSymbol ());
    Dbg << "MachType: Symbol: " << Name << EOL;
    parser.SkipLineWhitespace ();
    // size or alias
    if (parser.SymbolValid1st ()) { // alias
        String alias = static_cast<String &&>(parser.ReadSymbol ());
        Dbg << "MachType: Alias: " << alias << EOL;
        auto an = NodeByName (alias);
        FFD_ENSURE_FFD(an != nullptr, "The alias must exist prior whats "
            "referencing it. I know you want infinite loops; plenty elsewhere.")
        Size = an->Size;
        Signed = an->Signed;
    }
    else {// size
        if (parser.AtFp ())
            Fp = true, parser.SkipOneByte ();
        Size = parser.ParseIntLiteral ();
        if (Size < 0) { Signed = true; Size = -Size; }
        Dbg << "MachType: " << Size << " bytes, " << (Fp ? "floating-point"
            : (Signed ? "signed" : "unsigned")) << EOL;
    }
    if (parser.IsEol ()) return true; // completed
    // There could be an expression
    parser.SkipLineWhitespace ();
    if (parser.AtExprStart ())
        Expr = static_cast<List<FFDParser::ExprToken> &&>(
            parser.TokenizeExpression ());
    // There could be a comment. But it is handled elsewhere for now.
    return true;
}// FFD::SNode::ParseMachType()

// Be careful: leaves i pointing to '\n', or '\r' in the '\r\n' case.
// {symbol} .*EOLEOL
//         i
bool FFD::SNode::ParseLater(FFDParser & parser)
{
    parser.SkipUntilDoubleEol ();
    return true;
}

// ^[.*]
//  i
// Single line. An EOL completes it.
bool FFD::SNode::ParseAttribute(FFDParser & parser)
{
    Type = FFD::SType::Attribute;

//np ReadExpression (open: '[', close: ']');
    Attribute = static_cast<String &&>(parser.ReadExpression ('[', ']'));
    parser.SkipCommentWhitespaceSequence ();
    Dbg << "Attribute: " << Attribute << EOL;
    return true;
}

FFD::SNode::~SNode()
{
    for (int i = 0; i < Fields.Count (); i++)
        FFD_DESTROY_NESTED_OBJECT(Fields[i], FFD::SNode, SNode)
}

// struct {symbol}|{variadic-list-item:{value-list}}
//       i
// Multi-line. An EOLEOL or EOF, after at least 1 field, completes it.
// No expression.
bool FFD::SNode::ParseStruct(FFDParser & parser)
{
    Type = FFD::SType::Struct;
    parser.SkipLineWhitespace ();
    auto p = parser.Tell ();
    auto s = parser.TokenizeUntilWhiteSpace ("<>");
    FFD_ENSURE(s.Count () > 0, "TokenizeUntilWhiteSpace returned 0 tokens")
    Dbg << "Struct: " << s[0] << ": ";
    if (s.Count () > 1) {
        parser.SetCurrent (p); //TODO come on
        s = parser.TokenizeUntilWhiteSpace ("<>,");
        Name = s[0];
        Dbg << "parametrized: \"";
            PS.Add (PSParam {static_cast<String &&>(s[1])}); Dbg <<  "\"";
        for (int i = 2; i < s.Count (); i++)
            Dbg << ", \"", PS.Add (PSParam {static_cast<String &&>(s[i])}),
                Dbg << "\"";
    } else {
        parser.SetCurrent (p); //TODO come on
//np ReadSymbol (stop_at: ':', allow_dot: true);
        Name = static_cast<String &&>(parser.ReadSymbol (':', true));
        if (parser.AtVListSep ()) {
            Dbg << "variadic list item" << EOL;
            VListItem = true;
            parser.SkipOneByte ();
            FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete value-list")
            ValueList =
                static_cast<List<FFDParser::VLItem > &&>(parser.ReadValueList ());
            // Dbg << "Struct: ValueList: " << ValueList << EOL;
        }
    }
    Dbg << EOL;
    // skip remaining white-space(s) and comment(s)
    FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete struct")// struct .*EOF
    parser.SkipCommentWhitespaceSequence ();
    FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete struct")// struct .*EOLEOF
    FFD_ENSURE_FFD(! parser.IsEol (), "Empty struct") // struct foo.*EOLEOL
    // fields
    for (int chk = 0; ; chk++) {
        FFD_ENSURE_FFD(chk < FFD_MAX_FIELDS, "Refine your design")
        SNode * node;
        FFD_CREATE_OBJECT(node, FFD::SNode) {};
        node->Base = this;
        bool b = node->ParseField (parser), a = b && (! node->IsComment ());
        if (a) {
            Fields.Add (node);
            if (Fields.Count () > 1) {
                Fields[Fields.Count ()-2]->Next = Fields[Fields.Count ()-1];
                Fields[Fields.Count ()-1]->Prev = Fields[Fields.Count ()-2];
            }
        }
        else {
            FFD_DESTROY_NESTED_OBJECT(node, FFD::SNode, SNode)
            if (! b) return false;
        }
        // if (parser.HasMoreData ())
        //    Dbg << "done reading a field. " << parser.Tell () << ":"
        //        << Dbg.Fmt (%002X, *(parser.BufAt (parser.Tell ()))) << EOL;
        if (! parser.HasMoreData ()) return true; // fieldEOF
        if (parser.IsEol ()) {
            parser.SkipEol ();
            if (! parser.HasMoreData ()) return true; // fieldEOLEOF
            return true;
        }
    }
    return false;
}// FFD::SNode::ParseStruct()

// {whitespace} {symbol} [{expr}]
// "j" - base (where it starts)
bool FFD::SNode::ParseCompositeField(FFDParser & parser, int j)
{
    int t = parser.Tell ();
    parser.SetCurrent (j);
    if (parser.AtArrStart ()) // field attribute
        return FFD::SNode::ParseAttribute (parser);
    else parser.SetCurrent (t);

    Composite = true;
    DTypeName = static_cast<String &&>(parser.StringAt (j, parser.Tell ()-j));
    Name = "{composite}";
    Dbg << "Field: composite. DTypeName: " << DTypeName << EOL;
    if (parser.IsEol ()) return true;
    // if there is no expr - restore it, so FFD::SNode::ParseField() can handle
    // SkipLineWhitespace(); use case: {Composite} {Comment}
    int p = parser.Tell ();
    parser.SkipLineWhitespace ();
    if (parser.AtExprStart ())
        Expr = static_cast<List<FFDParser::ExprToken> &&>(
            parser.TokenizeExpression ());
    else parser.SetCurrent (p);
    // comment(s) and whitespace are handled by FFD::SNode::ParseField()
    return true;
}

// {whitespace} {symbol} [{symbol}] [{expr}]
// {whitespace} {symbol}<>{symbol}[] {symbol}
// {whitespace} ... {variadic-list-name}
// i
bool FFD::SNode::ParseField(FFDParser & parser)
{
    Type = FFD::SType::Field;

    parser.SkipLineWhitespace ();
    // Either a symbol or a comment
    while (parser.IsComment ()) { // multi-one-line comments
        parser.SkipCommentWhitespaceSequence ();
        // {comment}{EOL}{EOF} || {comment}{EOL}{EOL}
        if (! parser.HasMoreData () || parser.IsEol ()) // dangling comment
            return Type = FFD::SType::Comment, true;
        parser.SkipLineWhitespace ();
    }
    // What is it? "type<>type[] symbol" or "typeEOL" or "type " or "..."
    int j = parser.Tell ();
    for (;; parser.SkipOneByte ()) {
        if (parser.AtHashStart ()) {// hash field
            HashKey = true; // this field is hash key: the actual value is:
                            // HashType[value]
            FFD_ENSURE_FFD(parser.HasMoreData (), "wrong hash field") // foo<EOF
            parser.SkipOneByte ();
            FFD_ENSURE_FFD(parser.HasMoreData () && parser.AtHashEnd (),
                "wrong hash field")
            // resolve: pass 1
            String hash_key_type = static_cast<String &&>(
                parser.StringAt (j, parser.Tell ()-1-j));
            Dbg << "Field: hash. Key type: " << hash_key_type << EOL;
            DType = Base->NodeByName (hash_key_type); // could be conditional
            parser.SkipOneByte (); // move after "<>"
            FFD_ENSURE_FFD(parser.HasMoreData (), "wrong hash field") // .*<>EOF
            // Type - array Type; look for a field of said array type.
            // Right now single key is supported only e.g. the type is
            // implicitly Foo[] so no reason to store and query the [] as well;
            // composite keys might become available much, much, ..., later.
            //LATER why matching by type only?
            HashType = static_cast<String &&>(parser.ReadSymbol ('['));
            FFD_ENSURE_FFD(parser.HasMoreData (), "wrong hash field") // .*[EOF
            parser.SkipOneByte ();
            FFD_ENSURE_FFD(parser.HasMoreData () && parser.AtArrEnd (),
                "wrong hash field")
            parser.SkipOneByte ();
            FFD_ENSURE_FFD(parser.HasMoreData (), "wrong hash field") // .*[]EOF
            Dbg << "Field: hash. HashType: " << HashType << EOL;
            // Name - required; hash fields can't be nameless
            parser.SkipLineWhitespace ();
            // Can't be an array.
            Name = static_cast<String &&>(parser.ReadSymbol ());
            Dbg << "Field: hash. Name: " << Name << EOL;
            break;
        }
        else if (parser.AtVariadicStart ()) { // variadic
            Variadic = true;
            parser.ReadVariadicField (); // "..."
            FFD_ENSURE_FFD(parser.HasMoreData (), "Wrong variadic field") //.EOF
            parser.SkipLineWhitespace ();
            Name = static_cast<String &&>(parser.ReadSymbol ('\0', true));
            Dbg << "Field: variadic. Name: " << Name << EOL;
            break;
        }
        else if (parser.IsEol ()) // compositeEOL
            { ParseCompositeField (parser, j); break; }
        else if (parser.IsLineWhitespace ()) {// type or composite type
            int p = parser.Tell (); // look-ahead
            parser.SkipLineWhitespace ();
            if (parser.IsComment () || parser.AtExprStart ()) // composite
                { ParseCompositeField (parser.SetCurrent (p), j); break; }
            parser.SetCurrent (p);
            DTypeName =
                static_cast<String &&>(parser.StringAt (j, parser.Tell ()-j));
            Dbg << "Field: type: " << DTypeName << EOL;
            DType = Base->NodeByName (DTypeName); // resolve: pass 1
            parser.SkipLineWhitespace ();
            Name = static_cast<String &&>(parser.ReadSymbol ('['));
            if (parser.AtArrStart ()) {
                parser.SkipOneByte ();
                FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete array")// [EOF
                FFD_ENSURE_FFD(! parser.IsEol (), "Incomplete array") // [EOL
                Array = true;
                for (int arr = 0; arr < FFD_MAX_ARR_DIMS; arr++) {
                    if (parser.SymbolValid1st ())
                        Arr[arr].Name =
                            static_cast<String &&>(parser.ReadArrDim ());
                    else { // int literal; allow line whitespace around it
                        if (parser.IsLineWhitespace ()) // trim start
                            parser.SkipLineWhitespace ();
                        Arr[arr].Value = parser.ParseIntLiteral ();
                        if (parser.IsLineWhitespace ()) // trim end
                            parser.SkipLineWhitespace ();
                        FFD_ENSURE(parser.AtArrEnd (), "wrong arr dim")
                        parser.SkipOneByte (); // ']'.Next()
                    }
                    Dbg << "Field: array[" << arr << "]= ";
                    Arr[arr].DbgPrint (); Dbg << EOL;
                    if (! parser.HasMoreData ()) break; // foo[.*]EOF
                    if (! parser.AtArrStart ()) break;
                    else {
                        parser.SkipOneByte ();
                        FFD_ENSURE_FFD(arr != (FFD_MAX_ARR_DIMS-1),
                            "array: too many dimensions")
                    }
                    FFD_ENSURE_FFD(parser.HasMoreData (),
                        "incomplete array") // [EOF
                }
            }// array
            Dbg << "Field: name: " << Name << EOL;
            break;
        }// (parser.IsLineWhitespace ())
    }// (;; i++)
    // Read optional: expression, etc.
    if (! parser.HasMoreData () || IsAttribute ()) return true;
    if (parser.IsEol ()) { parser.SkipEol () ; return true; }
    parser.SkipLineWhitespace ();
    if (parser.AtExprStart ())
        Expr = static_cast<List<FFDParser::ExprToken> &&>(
            parser.TokenizeExpression ());
    parser.SkipCommentWhitespaceSequence ();
    return true;
}// FFD::SNode::ParseField()

// const {symbol} {literal} [{expr}]
//      i
bool FFD::SNode::ParseConst(FFDParser & parser)
{
    Type = FFD::SType::Const;

    parser.SkipLineWhitespace ();
    Name = static_cast<String &&>(parser.ReadSymbol ());
    Dbg << "Const: name: " << Name << EOL;
    parser.SkipLineWhitespace ();
    // int or string
    if (parser.AtDoubleQuote ()) {
        Const = FFD::SConstType::Text;
        StringLiteral = static_cast<String &&>(parser.ReadStringLiteral ());
        Dbg << "Const: string: " << StringLiteral << EOL;
    }
    else {
        Const = FFD::SConstType::Int;
        IntLiteral = parser.ParseIntLiteral ();
        Size = 4;
        if (IntLiteral < 0) Signed = true;
        Dbg << "Const: integer: " << IntLiteral << EOL;
    }
    if (parser.IsEol ()) { parser.SkipEol (); return true; }
    parser.SkipLineWhitespace ();
    if (parser.AtExprStart ())
        Expr = static_cast<List<FFDParser::ExprToken> &&>(
            parser.TokenizeExpression ());
    else
        parser.SkipCommentWhitespaceSequence ();
    return true;
}// FFD::SNode::ParseConst()

// enum {symbol} {machine type} [{expr}]
//     i
//   {whitespace} {symbol} {int literal} [{expr}]
bool FFD::SNode::ParseEnum(FFDParser & parser)
{
    Type = FFD::SType::Enum;

    parser.SkipLineWhitespace ();
    Name = static_cast<String &&>(parser.ReadSymbol ());
    Dbg << "Enum: name: " << Name << EOL;
    parser.SkipLineWhitespace ();
    DTypeName = static_cast<String &&>(parser.ReadSymbol ());
    Dbg << "Enum: type: " << DTypeName << EOL;
    DType = NodeByName (DTypeName); // resolve: pass 1
    FFD_ENSURE_FFD(nullptr != DType, "Enum shall resolve to a machine type")
    Size = DType->Size;
    Signed = DType->Signed;
    if (parser.IsEol ())
        parser.SkipEol ();
    else {
        if (parser.AtExprStart ())
            Expr = static_cast<List<FFDParser::ExprToken> &&>(
                parser.TokenizeExpression ());
        FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete enum") // foo.*)EOF
        parser.SkipCommentWhitespaceSequence ();
    }
    FFD_ENSURE_FFD(parser.HasMoreData (), "Incomplete enum") // enum foo.*EOLEOF
    FFD_ENSURE_FFD(! parser.IsEol (), "Empty enum") // enum foo.*EOLEOL
    for (int chk = 0, auto_value = 0; ; chk++, auto_value++) {
        FFD_ENSURE_FFD(chk < FFD_MAX_ENUM_ITEMS, "Refine your design")
        // TODO code-gen: auto-sync to formal_description
        // {whitespace} {symbol} [{int literal}] [{expr}] [{comment}]
        // i
        parser.SkipLineWhitespace ();
        if (parser.IsComment ()) // {comment}
            parser.SkipCommentWhitespaceSequence (), chk--, auto_value--;
        else {
            EnumItem itm {auto_value};
            itm.Name = static_cast<String &&>(parser.ReadSymbol ());
            Dbg << "EnumItem: Name: " << itm.Name << EOL;
            if (parser.IsEol ()) parser.SkipEol (); // {name}EOL
            else {
                parser.SkipLineWhitespace ();
                if (parser.IsComment ()) // {name} {comment}
                    parser.SkipCommentWhitespaceSequence ();
                else {
                    auto_value = itm.Value = parser.ParseIntLiteral ();
                    Dbg << "EnumItem: Value: " << itm.Value << EOL;
                    if (parser.IsEol ()) parser.SkipEol (); // {name} {value}
                    else {
                        if (parser.AtExprStart ()) // {name} {value} {expr}
                            itm.Expr = static_cast<List<FFDParser::ExprToken> &&>(
                                parser.TokenizeExpression ());
                        else
                            parser.SkipCommentWhitespaceSequence ();
                    }
                }
            }
            EnumItems.Add (itm);//TODO option: EnumItem duplicate value check
        }
        //
        if (! parser.HasMoreData ()) return true; // itemEOF
        if (parser.IsEol ()) {
            parser.SkipEol ();
            if (! parser.HasMoreData ()) return true; // itemEOLEOF
            return true;
        }
    }
    return false;
}// FFD::SNode::ParseEnum()

FFD::~FFD()
{
    while (_tail) {
        auto dnode = _tail;
        _tail = _tail->Prev;
        FFD_DESTROY_NESTED_OBJECT(dnode, FFD::SNode, SNode)
    }
    FFD_ENSURE(nullptr == _tail, "bug: something like an LL")
    _head = _tail;
}

//LATER use a hash
FFD::SNode * FFD::SNode::NodeByName(const String & query)
{
    FFD::SNode * result = {};
    WalkBackwards([&](FFD::SNode * node) {
        // Dbg << "ba n: " << node->Name << "vs. " << query << EOL;
        if (node->Name == query && node->Usable ()) {
            result = node;
            return false;
        }
        return true;
    });
    if (nullptr == result && Next)
        Next->WalkForward([&](FFD::SNode * node) {
            // Dbg << "fo n: " << node->Name << "vs. " << query << EOL;
            if (node->Name == query && node->Usable ()) {
                result = node;
                return false;
            }
            return true;
        });
    return result;
}

FFD::EnumItem * FFD::SNode::FindEnumItem(const String & name)
{//TODO enum.Expr > 0
    for (int i = 0; i < EnumItems.Count (); i++)
        if (EnumItems[i].Name == name) return &(EnumItems[i]);
    return nullptr;
}

List<FFD::SNode *> FFD::SNode::NodesByName(const String & query)
{
    Dbg << "FFD::SNode::NodesByName(" << query << ")" << " at " << Name << EOL;
    List<FFD::SNode *> result = {};
    WalkBackwards([&](FFD::SNode * node) {
        // Dbg << "  WalkBackwards: node->Name: " << node->Name << EOL;
        if (node->Name == query) result.Add (node);
        return true;
    });
    if (Next)
        Next->WalkForward([&](FFD::SNode * node) {
            // Dbg << "  WalkForward: node->Name: " << node->Name << EOL;
            if (node->Name == query) result.Add (node);
            return true;
        });
    return static_cast<List<FFD::SNode *> &&>(result);
}

static void print_tree(FFD::SNode * n)
{
    if (nullptr == n) {
        Dbg << "null tree - nothing to print" << EOL;
        return;
    }
    Dbg << "The tree:" << EOL;
    n->WalkForward ([&](FFD::SNode * n) -> bool {
        if (nullptr == n) { Dbg << "+[null]" << EOL; return true; }
        n->DbgPrint ();
        for (auto sn : n->Fields) {
            if (nullptr == sn) { Dbg << "+[null]" << EOL; continue; }
            Dbg << "  "; sn->DbgPrint ();
        }
        return true;
    });
}

void FFD::SNode::ResolveTypes()
{
    for (auto sn : Fields) sn->ResolveTypes ();

    if (DType || NoDType ()) return;
    if (DTypeName.Empty ()) {
        Dbg << "neither dtype nor dtypename: \"" << Name << "\"" << EOL;
        return;
    }
    auto ps =  DTypeName.Split ('<'); // TODO implement the multi-delim. one
    if (ps.Count () > 1) { // parametrized struct TODO 1000 checks
        DTypeName = ps[0]; // point to the parametrized struct
        Dbg << " FieldSNode<>" << Base->Name << "." << Name << ": resolving "
            << DTypeName << EOL;
        ps = ps[1].Split ('>');
        ps = ps[0].Split (',');
        auto foo = Base->NodeByName (DTypeName); // because this is a field?
        //TODO - Root->NodeByName - I'm looking for a type here; and types are
        //       root sub-nodes; or even better: TypeByName ()
        //TODO what happens if !Base, and if DTypeName.Empty()?
        for (int i = 0; i < ps.Count (); i++)
            Dbg << "  - ", PS.Add ({static_cast<String &&>(ps[i]), this,
                static_cast<String &&>(foo ? foo->PS[i].Name : "")});
    }
    DType = Base ? Base->NodeByName (DTypeName)
        : NodeByName (DTypeName);
}
static void resolve_all_types(FFD::SNode * n)
{
    n->WalkForward ([&](FFD::SNode * nn){ nn->ResolveTypes (); return true; });
}

FFD::FFD(const byte * buf, int len)
{
    // pre-validate - simplifies the parser
    for (int i = 0; i < len; i++) {
        FFD_ENSURE(
            '\n' == buf[i] || '\r' == buf[i] || (buf[i] >= 32 && buf[i] <= 126),
            "Wrong chars at description")
    }

    Dbg << "TB LR parsing " << len << " bytes ffd" << EOL;
    FFDParser parser {buf, len};
    // Dbg.Enabled = false;
    for (int chk = 0; parser.HasMoreData (); chk++) {
        if (parser.IsWhitespace ()) parser.SkipWhitespace ();
        // Skipped, for now
        else if (parser.IsComment ()) parser.SkipComment ();
        else {
            FFD::SNode * node;
            FFD_CREATE_OBJECT(node, FFD::SNode) {};
            if (nullptr == _tail)
                _tail = _head = node;
            else {
                node->Prev = _tail;
                node->Next = _tail->Next;
                if (_tail->Next) _tail->Next->Prev = node;
                _tail->Next = node;
                _tail = node;
            }
            node->Parse (parser);
            if (node->IsRoot ()) {
                FFD_ENSURE_FFD(nullptr == _root, "Multiple formats in a "
                    "single description aren't supported yet")
                _root = node;
                // Dbg.Enabled = false;
                Dbg << "Ready to parse: " << node->Name << EOL;
            }
        }
        FFD_ENSURE_FFD(chk < len, "infinite loop")
    }// (int i = 0; i < len;)

    // 2. Fasten DType - only those with "! Expr.Empty ()" shall remain null -
    //    they're being resolved at "runtime".
    resolve_all_types (_head);
    print_tree (_head);
}// FFD::FFD()

FFDNode * FFD::File2Tree(Stream & fh2)
{
    Stream * s {&fh2};
    FFDNode * data_root {};
    FFD_CREATE_OBJECT(data_root, FFDNode) {_root, s};
    Dbg << "uncompressed stream s: " << s->Tell () << "/" << s->Size () << EOL;
    return data_root;
}
void FFD::FreeNode(FFDNode * n) { FFD_DESTROY_OBJECT(n, FFDNode) }
#undef FFD_ENSURE_FFD

NAMESPACE_FFD
