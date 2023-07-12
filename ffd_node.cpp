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

#include "ffd_node.h"

#include <new>

FFD_NAMESPACE

FFDNode::~FFDNode()
{
    for (int i = 0; i < _fields.Count (); i++)
        FFD_DESTROY_OBJECT(_fields[i], FFDNode)
}

FFDNode::FFDNode(FFD::SNode * n, Stream * br, FFDNode * base,
    FFD::SNode * field_node)
    : _s{br}, _n{n}, _f{field_node}, _base{base}
{
    if (base) _level = base->_level + 1;

    if (n->IsField ()) FromField ();
    else if (n->IsStruct ()) FromStruct ();
    else
        Dbg << "Can't handle " << n->TypeToString () << EOL;
}

// All expressions are encolsed in ().
// example 1: open, symbol, op, symbol, close
// example 2: open, symbol, op, open, symbol, op, symbol, close, close
// example 3: open, symbol, close
// ( ( {T} == 1 ) || ( {T} == 3   ) ||  ( {T} ==  4  ) )
// 0 1  2  3  4 5  6 7  8  9  10 11 12 13  14 15 16 17 18
template <typename F> bool eval_expr(
    const List<FFDParser::ExprToken> & e, F resolve_symbols, int & id)
{
    FFD_ENSURE(id >= 0 && id < e.Count (), "Wrong expr.")
    FFDNode::ExprCtx ctx {};
    Dbg << "  Expr: ";
    for (; id < e.Count (); id++) { // find "l op r", or "op l"
        switch (e[id].Type) {
            case FFDParser::ExprTokenType::Open: {
                Dbg << "( ";
                FFD_ENSURE(ctx.i < 2, "opn: Wrong number of arguments")
                ctx.v[ctx.i++] = eval_expr (e, resolve_symbols, ++id);
            } break;
            case FFDParser::ExprTokenType::Close: {
                Dbg << ") " << EOL;
                return ctx.Compute ();
            }
            case FFDParser::ExprTokenType::Symbol: {
                Dbg << "{" << e[id].Symbol << "} ";
                FFD_ENSURE(ctx.i < 2, "sym: Wrong number of arguments")
                if (0 == ctx.i) ctx.LSymbol = e[id].Symbol;
                else if (1 == ctx.i) ctx.RSymbol = e[id].Symbol;
                ctx.i++;
                resolve_symbols (ctx);
            } break;
            case FFDParser::ExprTokenType::Number: {
                Dbg << e[id].Value << " ";
                FFD_ENSURE(ctx.i < 2, "num: Wrong number of arguments")
                ctx.v[ctx.i++] = e[id].Value;
            } break;
            default: {
                if (FFDParser::ExprTokenType::opN == e[id].Type)
                    ctx.n[ctx.i] = true;
                else {
                    ctx.op = e[id].Type; ctx.DbgPrint ();
                    if (2 == ctx.i) { // LR binary eval: a>b < c
                        Dbg << "Ready to compute at id " << id;
                        ctx.v[0] = ctx.Compute ();
                        Dbg << ", evaluted to " << ctx.v[0] << EOL;
                        ctx.i = 1;
                        ctx.op = e[id].Type;
                        ctx.n[0] = ctx.n[1] = false;
                    }
                }
                break;
            }
        }
    }
    Dbg << EOL;
    FFD_ENSURE(1 == ctx.i, "Evaluation failed")
    return ctx.v[0];
}// eval_expr

//TODO diagram: this looks at siblings of sn->Base
FFD::SNode * FFDNode::ResolveSNode(const String & n, int & value,
    FFD::SNode * sn, bool resolve_only)
{//TODO cache me
    Dbg << "  ResolveSNode: requested symbol: " << n << EOL;
    FFD_ENSURE(sn->IsField (), "Field SNodes only!")
    static String sym_name {};
    Dbg << "  ResolveSNode: sn->Base: " << sn->Base->Name << EOL;
    for (auto sym : sn->Base->NodesByName (n)) {
        Dbg << "  ResolveSNode: symbol: " << sym->Name << EOL;
        if (sym->IsConst () || sym->IsMachType () || sym->IsEnum ()) {
            FFD_ENSURE(sym_name != sym->Name, "Don't do that")
            if (! sym->Resolved) {
                Dbg << "  ResolveSNode: resolving ..." << EOL;
                sym->Resolved = true;
                if (sym->Expr.Count () > 0) {
                    Dbg << "  ResolveSNode: has an expr. evaluating ..." << EOL;
                    sym_name = sym->Name;
                    int ptr {};
                    sym->Enabled = eval_expr (sym->Expr,
                        [&](ExprCtx & ctx) {
                            ResolveSymbols (ctx, sn, this);
                        }, ptr);
                    sym_name = String {};
                }
                else
                    sym->Enabled = true;
                Dbg << "  ResolveSNode: enabled: " << sym->Enabled << EOL;
            }
            else
                Dbg << "  ResolveSNode: resolved already" << EOL;
            if (resolve_only) {//LATER evaluate all and report ambiguities
                if (! sym->Enabled) continue;
                else return sym; // return the 1st enabled one
            }
            if (sym->Enabled) {//LATER detect conflicts (more than 1 enabled)
                if (sym->IsIntConst ())
                    return value = sym->IntLiteral, sym;
                else { // implicit symbol - like (bool); TODO complicates store
                    FFD_ENSURE(sym->Size >= 1 && sym->Size <= 4,
                        "Can't handle that size")
                    int avalue {};
                    Dbg << "  ResolveSNode: implicit symbol, reading "
                        << sym->Size << " byte" << (sym->Size > 1 ? "s" : "")
                        << EOL;
                    _s->Read (&avalue, sym->Size);//TODO create FFDNode for it
                    return value = avalue, sym;
                }
            }
        }// sym->IsConst () || sym->IsMachType () || sym->IsEnum ()
    }// for (auto sym : sn->Base->NodesByName (n))
    Dbg << "  not found at _n->Base" << EOL;
    return nullptr;
}// FFDNode::ResolveSNode()

//LATER string literal const
void FFDNode::ResolveSymbols(ExprCtx & ctx, FFD::SNode * sn, FFDNode * base)
{
    //TODO this needs serious re-factoring
    //TODO match the formal spec. to the letter
    Dbg << "   FFDNode::ResolveSymbols: ";
    if (1 == ctx.i) Dbg << ctx.LSymbol; else Dbg << ctx.RSymbol;
    Dbg << " for " << sn->Name << EOL;
    if (sn->Base) { // SNode
        int value {};
        bool found {};
        if (! ctx.LSymbol.Empty ())
            if (ResolveSNode (ctx.LSymbol, value, sn)) {
                ctx.v[0] = value;
                found = true;
            }
        if (! ctx.RSymbol.Empty ())
            if (ResolveSNode (ctx.RSymbol, value, sn)) {
                ctx.v[1] = value;
                found = true;
            }
        if (found) {
            Dbg << "   SNode found: L: " << ctx.v[0] << ", R: " << ctx.v[1]
                << EOL;
            return;
        }
       //TODO already set? (on duplicate symbol name for example)
    }
    if (base) { // FFDNode
        Dbg << "   FFDNode::ResolveSymbols: looking at "
            << base->FieldNode ()->Name << EOL;
        // handle multi-depth PS field params
        bool ps_handled{}; auto ps_node = base->_base;
        while (ps_node && ! ps_handled) {
            auto snode = base->_base->FieldNode (); // synatx node
            if (snode->Parametrized ()) {
                snode->PSDbgPrint ();
                for (int i = 0; i < snode->PS.Count (); i++)
                    if (snode->PS[i].IsField ()) {
                        if (ctx.LSymbol == snode->PS[i].Bind)
                            ctx.LSymbol = snode->PS[i].Name, ps_handled = true;
                        if (ctx.RSymbol == snode->PS[i].Bind)
                            ctx.RSymbol = snode->PS[i].Name, ps_handled = true;
                    }
            }
            ps_node = ps_node->_base;
        }
        FFDNode * lsym {}, * rsym {};
        if (! ctx.LSymbol.Empty ()) {
            auto arr = static_cast<List<String> &&>(ctx.LSymbol.Split ('.'));
            lsym = base;
            for (int i = 0; i < arr.Count (); i++) {
                Dbg << "NodeByName() Looking for " << arr[i] << EOL;
                lsym = lsym->NodeByName (arr[i]);
                if (lsym)
                    Dbg << "NodeByName() found " << arr[i] << EOL;
                else break;
            }
        }
        if (! ctx.RSymbol.Empty ())//TODO do the same as for the lsym above
            rsym = base->NodeByName (ctx.RSymbol);
        if (! ctx.LSymbol.Empty () && ! lsym) // not found
            ctx.NoSymbol = true; // evaluate to false ; (nf != 1) evals to true
        if (! ctx.RSymbol.Empty () && ! rsym) // not found
            ctx.NoSymbol = true; // evaluate to false
        if (lsym && rsym) {
            Dbg << "    lsym && rsym " << EOL;
            ctx.v[0] = lsym->AsInt ();
            ctx.v[1] = rsym->AsInt ();
            return;
        }
        else if (! lsym && ! rsym) { // they could be not found
            Dbg << "    ! lsym && ! rsym " << EOL;
            return;
        }
        else if (2 == ctx.i) { // requested both; handle enum|const op symbol
            if (lsym && ! rsym) {
                Dbg << "    lsym && ! rsym "; lsym->_n->DbgPrint ();
                // check against the type set
                if (lsym->_n->DType->IsEnum ()) {
                    // Dbg << "   rsym.enum: find " << ctx.RSymbol << EOL;
                    auto enum_entry =
                        lsym->_n->DType->FindEnumItem (ctx.RSymbol);
                    if (enum_entry) {//TODO handle the not found part
                    ctx.v[0] = lsym->AsInt (); // already set at (1 == ctx.i)
                    ctx.v[1] = enum_entry->Value;
                    ctx.NoSymbol = false;
                    Dbg << "   L: " << ctx.v[0] << ", R: " << ctx.v[1] << EOL;
                    return;
                    }
                    else
                        FFD_ENSURE(0, "l: Enum symbol not found")
                }
                ctx.v[0] = lsym->AsInt (); ctx.NoSymbol = false;
                return;
            }//TODO handle the reverse: if (! lsym && rsym)
            else {//LATER swapping requires index swapping at ctx.v as well
                Dbg << "    ! lsym && rsym "; rsym->_n->DbgPrint ();
                // check against the type set
                if (rsym->_n->DType->IsEnum ()) {
                    // Dbg << "   rsym.enum: find " << ctx.RSymbol << EOL;
                    auto enum_entry =
                        rsym->_n->DType->FindEnumItem (ctx.RSymbol);
                    if (enum_entry) {//TODO handle the not found part
                    ctx.v[0] = enum_entry->Value;
                    ctx.v[1] = rsym->AsInt (); // already set at (1 == ctx.i)
                    ctx.NoSymbol = false;
                    Dbg << "   L: " << ctx.v[0] << ", R: " << ctx.v[1] << EOL;
                    return;
                    }
                    else
                        FFD_ENSURE(0, "r: Enum symbol not found")
                }
                ctx.v[1] = rsym->AsInt (); ctx.NoSymbol = false;
                return;
            }
        }
        else if (1 == ctx.i) {
            Dbg << "    lookup for 1 symbol: ";
            if (lsym) {
                Dbg << " left: ";
                ctx.v[0] = lsym->AsInt ();
                Dbg << ctx.v[0] << EOL;
                ctx.NoSymbol = false;
                return;
            }
            if (rsym) {
                Dbg << " right: ";
                ctx.v[1] = rsym->AsInt ();
                Dbg << ctx.v[1] << EOL;
                ctx.NoSymbol = false;
                return;
            }
            FFD_ENSURE(0, "1 == ctx.i && ! l && ! r ?!")
        }
        Dbg << "  not found at _base" << EOL;
    }// if (_base)
}// FFDNode::ResolveSymbol

bool FFDNode::EvalBoolExpr(FFD::SNode * sn, FFDNode * base)
{
    Dbg << "FFDNode::EvalBoolExpr(" << sn->Name << ", "
        << base->FieldNode ()->Name << ")" << EOL;
    int ptr {};
    auto result = eval_expr (sn->Expr, [&](ExprCtx & ctx) {
        ResolveSymbols (ctx, sn, base);
    }, ptr);
    Dbg << "   FFD::Node::EvalBoolExpr: " << result << EOL;
    return result;
}

String FFDNode::AsString()
{
    return static_cast<String &&>(String {_data, _data.Length ()});
}

void FFDNode::EvalArray()
{
    _array = true;
    // given "Foo bar[]", _f is "bar" and _n is "Foo"; (_n = _f->DType)
    auto n = nullptr != _f ? _f : _n;
    Dbg << " +field, array of " << n->DType->Name << EOL;
    // array size
    int arr_size {}, final_size {1};
    bool ja {false};
    for (int i = 0; i < FFD_MAX_ARR_DIMS; i++) {
        if (n->Arr[i].None ()) break;
        FFD_ENSURE(! ja, "implement me: jagged array of jagged arrays")
        Dbg << " ++dim type: "; n->Arr[i].DbgPrint (); Dbg << EOL;
        // Is it an implicit machine type?
        if (! n->Arr[i].Name.Empty ()) { // [{symbol}]
            // Look at root; because there are no root arrays - the array is
            // a field in a struct (n->Base (and n->Base is a node in a DLL)).
            // Dbg << " ++dim Looking for " << n->Arr[i].Name << EOL;
            auto m = n->Base->NodeByName (n->Arr[i].Name);
            // Dbg << " ++dim still Looking for " << n->Arr[i].Name << EOL;
            if (! m) {
                int value {};
                // Look for conditional things at root (evaluate whatever could
                // be evaluated; these are: "type", "enum" or "const" with bool.
                // expressions - depending on runtime data values)
                m = ResolveSNode (n->Arr[i].Name, value, n);
                // Dbg << " ++dim still2 Looking for " << n->Arr[i].Name << EOL;
            }
            if (m && m->IsIntConst ()) { // [FOO_CONST]
                _arr_dim[i] = m;
                Dbg << " ++dim value (intconst): " << m->IntLiteral << " items"
                    << EOL;
                arr_size = m->IntLiteral;
            }
            else if (m && ! m->IsMachType () && ! m->IsEnum ()) {
                Dbg << " implement me: root SNode array dim; jagged arr for "
                    "example" << EOL;
                return;
            }
            else if (m) {// a "type" found at root; [int] or [byte] ...
                _arr_dim[i] = m;
                Dbg << " ++dim size (implicit): " << m->Size << " bytes" << EOL;
                FFD_ENSURE(m->Size >= 0 && m->Size <= 4, "array dim overflow")
                _s->Read (&arr_size, m->Size);
                Dbg << " ++dim value (implicit): " << arr_size << " items"
                    << EOL;
            }
            else { // not a root SNode; no point searching at Fields
                auto node = NodeByName (n->Arr[i].Name);
                //TODO unite with "handle multi-depth PS field params" above
                if (nullptr == node && AtPSStruct ()) {
                    auto p = _base->FieldNode ()->PSParamByName (n->Arr[i].Name);
                    if (p) {
                        Dbg << "PS: array dim is an instance field" << EOL;
                        auto tmp = _base->_base;
                        while (tmp) {
                            auto b = tmp->FieldNode ()->PSParamByBind (p->Name);
                            if (b) {
                                Dbg << "Found \"" << p->Name << "\" bound to \""
                                    << b->Name << "\"" EOL;
                                node = NodeByName (b->Name);//TODO repl. Arr[i]?
                                break;
                            }
                            tmp = tmp->_base;
                        }
                    }
                }
                FFD_ENSURE(nullptr != node, "Arr. dim. not found")
                if (node->_array) {
                    ja = true;
                    Dbg << "[j] item size: " << node->_array_item_size << EOL;
                    arr_size = 1, final_size = node->IntArrElementSum ();
                    Dbg << "[j] total items: " << arr_size << EOL;
                }
                else {
                FFD_ENSURE(node->_n->IsField (), "Unsupported arr. dim.")
                m = node->_n->DType;
                FFD_ENSURE(m->IsValidArrDim (), "Unsupported arr. dim.")
                FFD_ENSURE(m->Size >= 0 && m->Size <= 4, "Arr. dim. overflow")
                arr_size = node->AsInt ();
                Dbg << " ++dim size (ffdnode): " << arr_size << " items" << EOL;
                }
            }
        }// ! n->Arr[i].Name.Empty ()
        else if (n->Arr[i].Value < 0) { // "-key" - read until "key"
            FFD_ENSURE(0 == i, "multi-dim read-until arrays not supported yet")
            FFD_ENSURE(n->DType->IsMachType () || n->DType->IsEnum (),
                "negative array size")
            FFD_ENSURE(1 == n->DType->Size || 2 == n->DType->Size
                || 4 == n->DType->Size, "read-until: unsupported item size")
            int key = -n->Arr[i].Value;
            Dbg << " ++dim read until \"" << key << "\"" << EOL;
            auto sa = _s->Size (); // cached on purpose; - just in case
            for (int b = key; _s->Tell () < sa;) { //TODO optimize me
                _s->Read (&b, n->DType->Size);
                if (b == key) break;
                _data.Resize (_data.Length () + n->DType->Size);
                OS::Memcpy(_data.operator byte * () + _data.Length () -
                    n->DType->Size, &b, n->DType->Size);
            }
            Dbg << " ++dim read until len: " << _data.Length () << EOL;
            String preview {_data.operator byte * (), _data.Length ()};
            Dbg << " ++dim read until as text: " << preview << EOL;
            //TODO [][-key], [-key][], [-key][-key]
        }
        else {
            Dbg << " ++dim value (intlit): " << n->Arr[i].Value << " items"
                << EOL;
            _arr_dim[i] = n;
            arr_size = n->Arr[i].Value;
        }
        final_size *= arr_size;
    }
    Dbg << " ++array size: " << final_size << EOL;
    if (0 == final_size) { // An actual use-case: "Atlantis_1029662174.h3m".
        if (0 == _data.Length ())
            Dbg << "Warning: array final_size of 0: nothing to read" << EOL;
        return;
    }
    FFD_ENSURE(final_size >= 0 && final_size <= 1<<21, // H3M_MAX_FILE_SIZE
        "suspicious array size")
    // item size
    _array_item_size = 0;
    if (n->DType->IsMachType () || n->DType->IsEnum ()) {
        Dbg << " ++item size: " << n->DType->Size << " bytes" << EOL;
        final_size *= (_array_item_size = n->DType->Size);
        FFD_ENSURE(final_size >= 0 && final_size <= 1<<21, // H3M_MAX_FILE_SIZE
            "suspicious array size")
        _data.Resize (final_size);
        _s->Read (_data.operator byte * (), final_size);
        Dbg << " ++data: "; PrintByteSequence ();
        //TODO HasAttribute() while n->Base->Prev && n->Base->Prev->IsAttribute()
        if (n->Base->Prev && n->Base->Prev->IsAttribute () &&
            n->Base->Prev->Attribute == "[Text]")
            Dbg << " ++text: " << AsString () << EOL;
    }
    else {// array item
        int psize = n->DType->PrecomputeSize ();
        if (psize > 0) { // 41472 TTile for example
            Dbg << " ++item pre-computed size: " << psize << " bytes" << EOL;
            final_size *= (_array_item_size = psize);
            FFD_ENSURE(final_size >= 0 && final_size <= 1<<21,
                "suspicious array size")
            // read once
            _data.Resize (final_size);
            _s->Read (_data.operator byte * (), final_size);
            //LATER accessing those is complicated:
            //       - I have to provide n-dim access
            //       - it has to know it is at _data, not at _fields
            //       - it has to keep psize and all arr. dim. sizes; that would
            //         be simple, save for jagged arrays
        }
        else {// array struct item
            for (int i = 0 ; i < final_size; i++) {
                Dbg << " +++item [" << i << "] (dynamic)" << EOL;
                // These are unconditional because there is no per-array item,
                // boolean evaluation. A.k.a. - the entire array is present.
                FFDNode * f {};
                Dbg << "ArrayField of " << _n->Name
                    << " named " << _f->Name << EOL;
                FFD_CREATE_OBJECT(f, FFDNode) {_n, _s, this};
                _fields.Add (f);
            }
        }
    }
}// FFDNode::EvalArray()

void FFDNode::FromField()
{
    Dbg << " field " << _n->Name << EOL;
    if (! _n->DType) {
        int unused {};
        bool resolve_only {true};
        Dbg << " Resolving " << _n->DTypeName << EOL;
        _n->DType = ResolveSNode (_n->DTypeName, unused, _n, resolve_only);
    }
    FFD_ENSURE(nullptr != _n->DType, "field->DType can't be null")

    /*_enabled = _n->HasExpr () ? EvalBoolExpr () : true;
    if (! _enabled) return;*/

    auto data_type = _n->DType;
    FFD_ENSURE(data_type->IsMachType () || data_type->IsEnum (),
        "bug: FFDNode::FromStruct() passed a field with unhandled DType")
    if (_n->Array)
        EvalArray ();
    else {
        Dbg << " field, data size: " << data_type->Size << " bytes" << EOL;
        FFD_ENSURE(data_type->Size >= 0
            && data_type->Size <= FFD_MAX_MACHTYPE_SIZE, "data_type->Size")
        _data.Resize (data_type->Size);
        _signed = data_type->Signed;
        _s->Read (_data.operator byte * (), data_type->Size);
        Dbg << " field, data: "; PrintByteSequence ();
        // HashKey
        if (_n->HashKey) {
            _hk = true;
            Dbg << " field, hk; looking for ttype: " << _n->HashType << EOL;
            _ht = FindHashTable (_n->HashType);
            FFD_ENSURE(nullptr != _ht, "Hash table not found")
            auto ht_fn = _ht->FieldNode ();
            auto ht_base_fn = _ht->_base->FieldNode ();
            Dbg << " field, hk, table: " << ht_base_fn->Name << "."
                << ht_fn->Name << EOL;
        }
    }
}// FFDNode::FromField()

void FFDNode::FromStruct(FFD::SNode * sn)
{
    bool don_use_f = sn != nullptr;
    if (! sn) sn = _n; // temporary: allows for the recursive detour below

    //TODO really, remove that recursive nice-mountain-view
    if (_f) Dbg << " field " << _f->Name << " ";
    Dbg << "struct lvl " << _level << ": "  << sn->Name << EOL;
    if (! don_use_f && _f && _f->Array) {
        // "Foo bar[]" that has already passed the eval below
        EvalArray ();
        return;
    }

    for (auto n : sn->Fields) {
        FFDNode * f {};
        Dbg << "<> " << sn->Name << "." << n->Name
            << Dbg.Fmt (" offset: %000000008X", _s->Tell ()) << EOL;
        if (n->HasExpr () && ! EvalBoolExpr (n, this)) {
            Dbg << " Eval: false: " << n->Name << EOL;
            continue;
        }
        if (! n->DType && FieldNode ()->Parametrized ()) {
            Dbg << "<><> FieldNode: " << FieldNode ()->Name << EOL;
            FieldNode ()->DbgPrint ();
            Dbg << "<><> Resolving parametrized " << n->DTypeName << EOL;
            FieldNode ()->PSDbgPrint ();
            FFD_ENSURE(FieldNode ()->DType->Parametrized (),
                "the PS isn't parametrized?!")
            Dbg << "<><> PS Names: " << EOL;
            int key{-1};
            for (int i = 0; i < FieldNode ()->DType->PS.Count (); i++) {
                Dbg << "  p[0] = " << FieldNode ()->DType->PS[i].Name << EOL;
                if (n->DTypeName == FieldNode ()->DType->PS[i].Name) {
                    key = i; break;
                }
            }
            FFD_ENSURE(key >= 0, "Unknown PS param")
            n->DType = n->Base->NodeByName (FieldNode ()->PS[key].Name);
            FFD_ENSURE(nullptr != n->DType, "Create the parametrized DType")
        }
        if (n->DType && n->DType->IsStruct ()) {
            if (n->Composite) {
                Dbg << "Composite struct: " << n->DType->Name << EOL;
                //TODO do this at the FFDParser, otherwise one and the same
                //     syntax node could get modified more than once - not ok
                FromStruct (n->DType);
                continue;
            }
            else
                FFD_CREATE_OBJECT(f, FFDNode) {n->DType, _s, this, n};
        }
        else {//TODO to functions
            if (n->Variadic) {
                Dbg << "Variadic field; - a dynamic composite field" << EOL;
                Dbg << "  ++var Dynamic Name: " << n->Name << EOL;
                List<String> names =
                    static_cast<List<String> &&> (n->Name.Split ('.'));
                for (int i = 0; i < names.Count (); i++)
                    Dbg << "  ++var: name[" << i << "]: " << names[i] << EOL;
                FFD_ENSURE(names.Count () > 0, "  ++var: key not found.")
                // end of String::Split ('.');
                FFDNode * fn{this}; //TODO this code repeats at Resolve above
                if (FFD_STRUCT_BY_NAME == names[0]) { // ... hash.hkeys
                    List<FFDNode *> ht {}; // TODO allow ht[0] == fn ?
                    for (int i = 1; i < names.Count (); i++) {
                        fn = fn->NodeByName (names[i]);
                        FFD_ENSURE(nullptr != fn, "  ++var: unk. ht.")
                        FFD_ENSURE(fn->_array, "  ++var: non-array ht.")
                        ht.Add (fn);
                    }
                    //TODO what if _base->_base is the array, etc. refactor
                    //     to handle tree iterator
                    FFD_ENSURE(_base->_array, "can't iterate over non-array")
                    if (_base->_vfi_list.Empty ())//TODO see GetVFIterator
                        _base->_vfi_list.Add (VFIterator {ht});
                    auto em_node = FieldNode ()->NodeByName (
                        _base->_vfi_list[0].ResolveToString ());
                    FFD_ENSURE(em_node != nullptr, "  ++var: not found")
                    em_node->DbgPrint ();
                    FromStruct (em_node);
                    continue;
                }// if (FFD_STRUCT_BY_NAME == names[0])
                for (int i = 0; i < names.Count (); i++) { // ... hk.field
                    fn = fn->NodeByName (names[i]);
                    FFD_ENSURE(nullptr != fn, "  ++var: unk. field.")
                    // FFDNode * obj {};
                    if (fn->_hk) {
                        Dbg << "  ++var: Hash(" << fn->AsInt (fn->_ht) << ")"
                            << EOL;
                        fn = fn->_ht->Hash (fn);
                        FFD_ENSURE(nullptr != fn, "  ++var: unk. obj.")
                        FFD_ENSURE(fn->_base->_array, "  ++var: not-arr. obj.")
                        Dbg << "  ++var: obj[" << i << "]: ."
                            << fn->_base->FieldNode ()->Name << EOL;
                    }
                    else
                        Dbg << "  ++var: obj[" << i << "]: ."
                            << fn->FieldNode ()->Name << EOL;
                }
                Dbg << "  ++var: value-list value: " << fn->AsInt () << EOL;
                auto composite = sn->FindVListItem (n->Name, fn->AsInt ());
                // It is allowed to be not found: no more fields.
                if (composite) {
                    Dbg << "composite: " << composite->Name;
                        composite->PrintValueList ();
                    //TODO Emit new Syntax node here: sn->Name + n->Name
                    //     + fn->AsInt (fn->_ht); or sn->Name.Autoinc;
                    //     some distinct name to avoid modifying one and the
                    //     same thing
                    FromStruct (composite);
                }
                continue;
            }// (n->Variadic)
            else
                FFD_CREATE_OBJECT(f, FFDNode) {n, _s, this};
        }// ! (n->DType && n->DType->IsStruct ())
        _fields.Add (f);
    }
}// FFD::Node::FromStruct()

NAMESPACE_FFD
