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

// while(theres_data).read(how_many).put_it(where)

#ifndef _FFD_NODE_H_
#define _FFD_NODE_H_

#include "ffd_model.h"
#include "ffd.h"

FFD_NAMESPACE

// File Format Description.
// This is the tree that your data gets transformed to, by the description.
// FFDNode = f (SNode, Stream)
class FFD_EXPORT FFDNode
{
    private: ByteArray _data {}; // empty for _array == true; _fields has them
    private: Stream * _s {}; // reference
    private: FFD::SNode * _n {}; // reference ; node
    private: FFD::SNode * _f {}; // reference ; field node (Foo _f[])
    private: bool _signed {}; // signed machine types
    private: bool _array {}; // array of struct at _fields
    private: int _array_item_size {}; // array of struct at _data
    private: bool _hk {}; // hash key
    private: FFD::SNode * _arr_dim[3] {}; // references; store the dimensions
    // No point making it an LL:
    //  - twice the number of objects created
    //  - considerable slow down of the Hash() access below
    //  - doesn't resolve the recursive situation at FromStruct()
    //  - doesn't resolve the odd (for me) mem. leaks; one thing is sure: it
    //    ain't caused by the List<T>
    private: List<FFDNode *> _fields {};
    private: int _level {};
    private: FFDNode * _base {};
    private: FFDNode * _ht {}; // hash table - referred by a hash key node
    // node, stream, base_node, field_node (has DType and Array: responsible for
    // "node" processing)
    public: FFDNode(FFD::SNode *, Stream *, FFDNode * base = nullptr,
        FFD::SNode * = nullptr);
    private: void FromStruct(FFD::SNode * = nullptr);
    private: void FromField();
    public: ~FFDNode();

    // FFDNode Converter - used by the Get() method.
    template <typename T> struct NodeCon final { NodeCon() = delete; };
    template <> struct NodeCon<int> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator int() { return Node->AsInt (); }
    };
    template <> struct NodeCon<bool> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator bool() { return Node->AsInt (); }
    };
    template <> struct NodeCon<String> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator String()
        {
            return static_cast<String &&>(Node->AsString ());
        }
    };
    template <> struct NodeCon<byte> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator byte() { return Node->AsByte (); }
    };
    template <> struct NodeCon<short> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator short() { return Node->AsShort (); }
    };
    template <> struct NodeCon<FFDNode *> final
    {
        FFDNode * Node;
        NodeCon(FFDNode * node) : Node {node} {}
        inline operator FFDNode *() { return Node; }
    };

    // Returns "dt" if "nn" ain't present.
    public: template <typename T> T Get(const String & nn, T dt = T {})
    {
        auto node = NodeByName (nn);
        //TODO there is a problem here: how to differ bug from use-case?
        if (nullptr == node) return dt;
        return NodeCon<T> {node}.operator T ();
    }

    public: inline bool IsEnum() const
    {
        auto sn = FieldNode ();
        FFD_ENSURE(nullptr != sn, "No syntax node")
        //TODO what instance nodes are allowed to have no type info?
        FFD_ENSURE(nullptr != sn->DType, "No type info")
        return sn->DType->IsEnum ();
    }
    public: inline const String & GetEnumName() const
    {
        int v = AsInt ();
        auto itm = FieldNode ()->DType->EnumItems.Find (
            [&v](const auto & itm) { return itm.Value == v; });
        FFD_ENSURE(nullptr != itm, "Unknown enum value")
        return itm->Name;
    }

    //PERHAPS all of these As.* must handle the _hk flag
    public: String AsString();
    private: inline FFDNode * Hash(const FFDNode * key) const
    {
        FFD_ENSURE(nullptr != key, "Hash(): key can't be null")
        // auto sn = key->FieldNode ();
        if (_data.Length () > 0) {
            //LATER either construct a new FFDNode to just use its AsInt() -
            //      doesn't sound too bright to me; or use distinct functions:
            //      IntHash for example
            /*auto ofs = sn->DType->Size * key->AsInt ();
            auto buf = _data.operator byte * () + ofs;
            switch (sn->DType->Size) {
                case 1: return static_cast<byte>(*buf);
                case 2: return static_cast<short>(*buf);
                case 4: return static_cast<int>(*buf);
                default: FFD_ENSURE(0, "Unknown hash key size")
            }*/
            FFD_ENSURE(0, "Implement me: int hash(key)")
        }
        else if (_fields.Count () > 0) {
            Dbg << "key->AsInt: " << key->AsInt (key->_ht) << "/"
                << _fields.Count () << EOL;
            return _fields[key->AsInt (key->_ht)];
        }
        else
            FFD_ENSURE(0, "Empty HashTable")
    }
    public: inline byte AsByte() const { return _data[0]; }
    public: inline short AsShort() const
    {
        switch (_data.Length ())
        {
            case 1:
                return static_cast<short>(*(_data.operator byte * ()));
            case 2: return
                *(reinterpret_cast<short *>(_data.operator byte * ()));
            default: FFD_ENSURE(0, "Don't request that AsShort")
        }
    }
    public: template <typename T> inline T As() const
    {
        return *(reinterpret_cast<T *>(_data.operator byte * ()));
    }
    public: inline int AsInt(FFDNode * ht = nullptr) const
    {
        int result {};
        switch (_data.Length ())
        {
            case 1: result = static_cast<int>(As<byte> ()); break;
            case 2: result = static_cast<int>(
                _signed ? As<short> () : As<unsigned short> ()); break;
            case 4: result = As<int> (); break;
            default: FFD_ENSURE(0, "Don't request that AsInt")
        }
        if ((_hk && ! ht) || (ht && ht != _ht)) {//TODO test-me
            FFD_ENSURE(nullptr != _ht, "HashKey without a HashTable")
            auto sn = _ht->FieldNode ();
            FFD_ENSURE(sn->DType->IsIntType (), "HashTable<not int>")
            result = _ht->Hash (this)->AsInt ();
        }
        return result;
    }
    public: template <typename T> inline const T * AsArr() const
    {
        return reinterpret_cast<T *>(_data.operator byte * ());
    }

    struct ExprCtx final // required to evaluate enum elements in expression.
    {
        int v[2] {}; // l r
        int i {};    // 0 1
        bool n[2] {}; // negate for l r
        String LSymbol {}; // Version | RoE
        String RSymbol {}; // RoE     | Version
        FFDParser::ExprTokenType op {FFDParser::ExprTokenType::None}; // Binary
        bool NoSymbol {};
        public: inline int Compute()
        {
            if (NoSymbol) return 0;
            if (n[0]) v[0] = ! v[0];
            if (n[1]) v[1] = ! v[1];
            // Dbg << "Compute " << i << ", l: " << v[0] << ", r: " << v[1]
            //     << EOL;
            switch (op) {
                case FFDParser::ExprTokenType::None: return v[0];
                case FFDParser::ExprTokenType::opN: return ! v[0];
                case FFDParser::ExprTokenType::opNE: return v[0] != v[1];
                case FFDParser::ExprTokenType::opE: return v[0] == v[1];
                case FFDParser::ExprTokenType::opG: return v[0] > v[1];
                case FFDParser::ExprTokenType::opL: return v[0] < v[1];
                case FFDParser::ExprTokenType::opGE: return v[0] >= v[1];
                case FFDParser::ExprTokenType::opLE: return v[0] <= v[1];
                case FFDParser::ExprTokenType::opOr: return v[0] || v[1];
                case FFDParser::ExprTokenType::opAnd: return v[0] && v[1];
                case FFDParser::ExprTokenType::opBWAnd: return v[0] & v[1];
                default: FFD_ENSURE(0, "Unknown op")
            }
        }
        public: inline void DbgPrint()
        {
            switch (op) {
                case FFDParser::ExprTokenType::None: Dbg << " "; break;
                case FFDParser::ExprTokenType::opN: Dbg << "! "; break;
                case FFDParser::ExprTokenType::opNE: Dbg << "!= "; break;
                case FFDParser::ExprTokenType::opE: Dbg << "== "; break;
                case FFDParser::ExprTokenType::opG: Dbg << "> "; break;
                case FFDParser::ExprTokenType::opL: Dbg << "< "; break;
                case FFDParser::ExprTokenType::opGE: Dbg << ">= "; break;
                case FFDParser::ExprTokenType::opLE: Dbg << "<= "; break;
                case FFDParser::ExprTokenType::opOr: Dbg << "|| "; break;
                case FFDParser::ExprTokenType::opAnd: Dbg << "&& "; break;
                case FFDParser::ExprTokenType::opBWAnd: Dbg << "& "; break;
                default: Dbg << "?? "; break;
            }
        }
    };// ExprCtx
    // Evaluate machtype|enum Size, or const IntLiteral, based on their Expr.
    // Cache their Enabled state, based on the evaluated Expr.
    // Returns the SNode of the symbol that was found.
    // Use when machtype, enum, or const have Expression on them.
    // "resolve_only" is true when you don't need a value from the stream.
    // "resolve_only" is false for implicit "bool" for example: "(bool)", where
    // bool is a machine type, and true for some machine type whose size depends
    // on runtime bool evaluation.
    private: FFD::SNode * ResolveSNode(const String &, int & value,
        FFD::SNode * sn, bool resolve_only = false);
    private: void ResolveSymbols(ExprCtx &, FFD::SNode * sn, FFDNode * base);
    // sn - expression node, base - current struct node
    private: bool EvalBoolExpr(FFD::SNode * sn, FFDNode * base);
    private: void EvalArray();

    // [dbg]
    private: inline void PrintByteSequence()
    {
        if (_data.Length () <= 0) return;
        Dbg << Dbg.Fmt ("[%002X", _data[0]);
        // ellipsis
        constexpr int ellipsis_len = 512;
        auto const len =
            _data.Length () <= ellipsis_len ? _data.Length () : ellipsis_len;
        for (int i = 1; i < len; i++)
            Dbg << Dbg.Fmt (" %002X", _data[i]);
        Dbg << (_data.Length () <= ellipsis_len ? "" : " ...") << "]" << EOL;
    }
    public: inline FFD::SNode * FieldNode() const { return _f ? _f : _n; }
    public: inline FFDNode * NodeByName(const String & name)
    {
        //LATER
        // This lookup is not quite ok. Duplicate symbol names might surprise
        // one. I better think of some way to explicitly mark "public" symbols.
        if (_array) { // no point looking in it
            if (_base) return _base->NodeByName (name);
            return nullptr;
        }

        for (auto n : _fields) {
            auto sn = n->FieldNode ();
            if (sn->Name == name) return n;
        }

        if (_base) return _base->NodeByName (name);

        return nullptr;
    }
    public: inline FFDNode * FindHashTable(const String & type_name)
    {
        if (_array) { // no point looking in it
            if (_base) return _base->FindHashTable (type_name);
            return nullptr;
        }
        for (auto n : _fields) {
            auto sn = n->FieldNode ();
            /*Dbg << " field: " << sn->Name << ", type: "
                << (sn->DType ? sn->DType->Name : "none")
                << ", arr. dims: " << sn->ArrDims () << EOL;*/
            if (1 == sn->ArrDims ()
                && sn->DType && sn->DType->Name == type_name) return n;
        }
        if (_base) return _base->FindHashTable (type_name);
        return nullptr;
    }

    public: inline void PrintTree(int f_id = -1)
    {
        for (int i = 0; i < _level; i++) {
            if (_level-1 == i) {
                if (_base && _base->_fields.Count () - 1 == f_id) Dbg << "'-";
                else Dbg << "|-";
            }
            else if (! (i % 2)) Dbg << "| ";
            else Dbg << "  ";
        }
        Dbg << "Node";
        if (f_id >= 0) Dbg << Dbg.Fmt ("[%5d]", f_id);
        Dbg << " level " << _level << ": ";
        auto fn = FieldNode ();
        if (fn) {
            if (fn->Name.Empty ()) Dbg << "{empty name} ???";
            else Dbg << fn->Name;
        }
        Dbg << EOL;
        for (int i = 0; i < _fields.Count (); i++) {
            if (! _fields[i]) Dbg << "{null field " << i << "} ???" << EOL;
            else _fields[i]->PrintTree (i);
        }
    }

    public: inline int TotalNodeCount() const
    {
        int cnt {_fields.Count ()};
        for (auto node : _fields) cnt += node->TotalNodeCount ();
        return cnt;
    }

    public: inline bool ArrayOfFields() const
    {
        return _array && 0 == _array_item_size;
    }
    public: inline int NodeCount() const
    {
        return ArrayOfFields () ? _fields.Count ()
            : _data.Length () / _array_item_size;
    };
    public: inline FFDNode * operator[](int i)
    {
        //LATER create unconditional FFDNode from memory stream;
        //      sync with SNode::PrecomputeSize()
        FFD_ENSURE(ArrayOfFields (), "Pre-computed size, not implemented yet")
        return _fields[i];
    }
    public: inline List<FFDNode *> & Nodes() { return _fields; }

    public: inline const ByteArray * AsByteArray() { return &_data; }

    public: inline int IntArrElementAt(int index)
    {
        auto dt = FieldNode ()->DType;
        FFD_ENSURE(dt != nullptr, "IntArrElementAt: DType can't be null")
        FFD_ENSURE(dt->IsIntType (), "IntArrElementAt: not an int array")
        switch (dt->Size) {//TODO <size: size_t, signed: bool> to Type
            case 1: return AsArr<byte> ()[index];
            case 2: return dt->Signed ? AsArr<short> ()[index]
                : AsArr<unsigned short> ()[index];
            case 4: return dt->Signed ? AsArr<int>()[index]
                : AsArr<unsigned int>()[index];
            default: FFD_ENSURE(0, "IntArrElementAt: unhandled DType->Size")
        }
    }
    //TODO not the best variant, but should do prior the Arr API
    public: template<typename T> inline int AASum(T a, int n)
    {
        int r{};
        for (int i = 0; i < n; r+=static_cast<int>(a[i++]))
            ;
        return r;
    }
    public: inline int IntArrElementSum() //TODO Arr API
    {
        auto dt = FieldNode ()->DType;
        FFD_ENSURE(dt != nullptr, "IntArrElementSum: DType can't be null")
        FFD_ENSURE(dt->IsIntType (), "IntArrElementSum: not an int array")
        switch (dt->Size) {//TODO <size: size_t, signed: bool> to Type
            case 1: return AASum (AsArr<byte> (), NodeCount ());
            case 2: return dt->Signed
                ? AASum (AsArr<short> (), NodeCount ())
                : AASum (AsArr<unsigned short> (), NodeCount ());
            case 4: return dt->Signed
                ? AASum (AsArr<int>(), NodeCount ())
                : AASum (AsArr<unsigned int>(), NodeCount ());
            default: FFD_ENSURE(0, "IntArrElementSum: unhandled DType->Size")
        }
    }

    // State of a variadic field that should iteratively be resolved to a struct
    //LATER support n.Count() > 2
    private: struct VFIterator final
    {
        VFIterator(List<FFDNode *> n)
            : Ht{static_cast<List<FFDNode *> &&>(n)}
        {
            // "hash_table", "keys"
            // "field"
            FFD_ENSURE(Ht.Count () > 0 && Ht.Count () < 3,
                "VFIterator: odd number of nodes")
            Dbg << "VFIterator: Table(s):" << EOL;
            for (int i = 0; i < Ht.Count (); i++) {
                Dbg << "  ht[" << i << "]: "
                    << Ht[i]->FieldNode ()->Base->Name << "."
                    << Ht[i]->FieldNode ()->Name << EOL << "  ";
                    Ht[i]->FieldNode ()->DbgPrint ();
            }
            if (1 == n.Count ()) { // 1 node that directly resolves to string
                //TODO
                //this is the case but only when Ht[0] is resolvable to string:
                //  String Name
                //  ... struct.Name <- is this the correct syntax?
                //  ... Name <- looks more appropriate
                // what happens if it is a non-local array of strings?
                Dbg << "VFIterator: single field" << EOL;
                return;
            }
            // 0 -> ... struct.isnt_an_array (1 == Ht.Count ())
            Count = Ht.Count () > 1 ? Ht[1]->NodeCount () : 0;
            Dbg << "VFIterator: ltop layer keys: " << Count << EOL;
        }
        int Index{}; //
        int Count{}; // Ht[1]->Count
        int Key{};  // Ht[1]->AsArr()[Index++]
        List<FFDNode *> Ht{}; // 1. Ht[0]->_fields[Key]
                              // 2. Ht[0]->AsString()
        inline String ResolveToString(List<FFDNode *> * update = nullptr)
        {
            Dbg << "VFIterator: ResolveToString" <<  Ht.Count () << EOL;
            if (1 == Ht.Count ()) { // this becomes a template?!
                //TODO clarify the ??-iterator situation: names, over what
                //     it is being iterated, are not in a pre-defined array;
                //     a.k.a. the iterator is building the array
                if (update) Ht[0] = update->operator[] (0);
                Dbg << "VFIterator: val: " << Ht[0]->_fields[0]->AsString ()
                    << " at Index: " << Index++ << EOL;
                return Ht[0]->_fields[0]->AsString ();
            }
            else {
                FFD_ENSURE(Index < Count, "VFIterator: overflow")
                int key = Ht[1]->IntArrElementAt (Index++);
                //TODO fix AsString() to use appropriate field based on [Text]
                FFD_ENSURE(Ht.Count () > 0, "VFI: Ht can't be empty")
                FFD_ENSURE(key >= 0 && key < Ht[0]->_fields.Count (),
                    "VFI: key out of range")
                FFD_ENSURE(Ht[0]->_fields[key]->_fields.Count () > 0,
                    "VFI: odd hash item")
                Dbg << "VFIterator: key: " << key << ", " << "val: "
                    << Ht[0]->_fields[key]->_fields[0]->AsString ()
                    << " at Index: " << Index-1 << EOL;
                return Ht[0]->_fields[key]->_fields[0]->AsString ();
            }
        }
    };// VFIterator
    //TODO GetVFIterator(FFDNode * client)
    private: List<VFIterator> _vfi_list {};
    // this node as a field has _base node as a struct
    public: inline bool AtPSStruct()
    {
        return _base && _base->FieldNode ()->Parametrized ();
    }
    public: static __attribute__ ((visibility("default"))) bool SkipAnnoyngFile;
};// FFDNode

NAMESPACE_FFD

#endif