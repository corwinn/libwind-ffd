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

//TODO sanity check: circular dependency
//TODO sanity check: unresolvable DType (Expr.Count () <= 0)

#ifndef _FFD_H_
#define _FFD_H_

#include "ffd_model.h"
#include "ffd_parser.h"
#include "ffd_dbg.h"

FFD_NAMESPACE

class FFDNode;

// File Format Description.
// Wraps a ffd (a simple text file written using a simple grammar) that can be
// used to parse different file formats.
// A.k.a. transforms binary files into trees.
//   data = FFD::File2Tree ("description", "file").
class FFD_EXPORT FFD
{
    public: FFD(const byte * buf, int len);
    public: ~FFD();

    // TODO FFD::Load ("description").Parse ("foo");

    // The type of text at the description.
    //
    // Attribute are distinct nodes for now; when processing, they're the nth
    // previous nodes. Its possible that there will be global attributes.
    //
    public: enum class SType {Comment, MachType, TxtList, TxtTable, Unhandled,
        Struct, Field, Enum, Const, Format, Attribute};
    public: enum class SConstType {None, Int, Text};
    public: class EnumItem final
    {
        public: EnumItem() {}
        public: EnumItem(int v) : Value{v} {}
        public: String Name {};
        public: int Value {};
        public: List<FFDParser::ExprToken> Expr {};
        public: bool Enabled {};
    };
    public: class ArrDimItem final
    {
        public: String Name {}; // Empty when Value is used
        public: int Value {}; // 0 when Name is used
        public: inline bool None() const { return Name.Empty () && 0 == Value; }
        public: inline void DbgPrint()
        {
            if (None ()) return;
            if (Name.Empty ()) Dbg << "intlit: " << Value;
                          else Dbg << "symbol: " << Name;
        }
    };
    // Syntax node - these are created as a result of parsing the description.
    // Its concatenation of SType. It could become class hierarchy.
    public: class SNode final
    {
        // Allow copying for now
        public: SNode() {}
        public: ~SNode();

        // a temporary not OO LL <=> until the libwind situation gets resolved
        public: SNode * Prev {};
        public: SNode * Next {};
        private: template <typename F> void WalkBackwards(F on_node)
        {
            if (! on_node (this)) return;
            if (nullptr == Prev) return;
            Prev->WalkBackwards (on_node);
        }

        // Returns Usable() only! Means the SNode is enabled, and doesn't need
        // evaluation.
        public: SNode * NodeByName(const String &);
        // Returns all that match the name, regardless of flags.
        public: List<SNode *> NodesByName(const String &);

        // Call after all nodes are parsed. Allows for dependency-independent
        // order of things at the description.
        public: void ResolveTypes();
        public: template <typename F> void WalkForward(F on_node)
        {
            if (! on_node (this)) return;
            if (nullptr == Next) return;
            Next->WalkForward (on_node);
        }

        public: String Attribute {}; // [.*] prior it
        public: SType Type {SType::Unhandled};
        public: SNode * Base {};  // Base->Type == SType::Struct
        public: String Name {};
        public: SNode * DType {}; // Data/dynamic type (Expr: resolved on parse)
        public: String DTypeName {}; // Prior resolve
        public: List<SNode *> Fields {};
        public: List<FFDParser::ExprToken> Expr {};
        public: String Comment {};

        public: bool HashKey {};
        public: String HashType {};

        public: bool Array {};     // Is it an array
        public: ArrDimItem Arr[FFD_MAX_ARR_DIMS] {};
        public: inline int ArrDims() const
        {
            for (int i = 0; i < FFD_MAX_ARR_DIMS; i++)
                if (Arr[i].None ()) return i;
            return FFD_MAX_ARR_DIMS;
        }

        public: bool Variadic {};  // "..." Type == SType::Field
        public: bool VListItem {}; // Struct foo:value-list ; "foo" is at "Name"
        // Variadic: value list :n, m, p-q, ...
        public: List<FFDParser::VLItem> ValueList {};
        public: inline void PrintValueList() const
        {
            for (auto & itm : ValueList)
                Dbg << " itm[" << itm.A << ";" << itm.B << "]";
            Dbg << EOL;
        }
        public: inline bool InValueList(int value) const
        {
            for (auto & itm : ValueList) {
                if (itm.Contains (value)) return true;
            }
            return false;
        }
        public: inline SNode * FindVListItem(const String & dname, int value)
        {//TODO this is the same as NodeByName, NodesByName, etc. unify them
         //TODO more than one is an error: report it
            FFD::SNode * result = {};
            WalkBackwards([&](FFD::SNode * node) {
                if (node->VListItem && node->Usable () && node->Name == dname
                    && node->InValueList (value)) {
                    result = node;
                    return false;
                }
                return true;
            });
            if (nullptr == result && Next)
                Next->WalkForward([&](FFD::SNode * node) {
                    if (node->VListItem && node->Usable ()
                        && node->Name == dname && node->InValueList (value)) {
                        result = node;
                        return false;
                    }
                    return true;
                });
            return result;
        }

        public: bool Composite {}; // replace it with FindDType (Name)

        public: SConstType Const {};
        public: String StringLiteral {};
        public: int IntLiteral {};

        public: bool Enabled {}; // true when Expr has evaluated to it
        public: bool Resolved {}; // true when Expr has been evaluated
        public: bool inline Usable() const
        {//TODO report ! Resolved
            return Expr.Count () <= 0 || (Resolved && Enabled);
        }

        // Type == SType::MachType
        public: bool Signed {};
        public: int Size {};
        public: bool Fp {}; // floating point
        // public SNode * Alias {};  // This could become useful later

        public: List<EnumItem> EnumItems {};
        public: EnumItem * FindEnumItem(const String &);

        public: bool Parse(FFDParser &);
        public: bool ParseMachType(FFDParser &);
        public: bool ParseLater(FFDParser &);
        public: bool ParseAttribute(FFDParser &);
        public: bool ParseStruct(FFDParser &);
        private: bool ParseField(FFDParser &);
        public: bool ParseConst(FFDParser &);
        public: bool ParseEnum(FFDParser &);

        private: bool ParseCompositeField(FFDParser &, int);

        public: inline bool IsRoot() const { return FFD::SType::Format == Type; }
        // DType not possible: a.k.a. some nodes do not require/have dtype.
        public: inline bool NoDType()
        {
            switch (Type) {
                case (SType::Field): case (SType::Enum): return false;
                default: return true;
            };
        }
        public: inline bool IsAttribute() const
        {
            return SType::Attribute == Type;
        }
        public: inline bool IsStruct() const
        {
            return SType::Struct == Type || SType::Format == Type;
        }
        public: inline bool IsMachType() const
        {
            return SType::MachType == Type;
        }
        public: inline bool IsEnum() const { return SType::Enum == Type; }
        public: inline bool IsField() const { return SType::Field == Type; }
        public: inline bool IsIntConst() const
        {
            return SType::Const == Type && SConstType::Int == Const;
        }
        public: inline bool IsConst() const { return SType::Const == Type; }
        public: inline bool IsValidArrDim() const
        {
            return IsMachType () || IsEnum ();
        }
        // 32 bit int, mind you
        public: inline bool IsIntType() const
        {
            return (IsMachType () || IsEnum () || IsIntConst ())
                && (Size >= 1 && Size <= 4) && ! Fp;
        }
        public: inline bool IsComment() const { return SType::Comment == Type; }
        // By value. Allow many attributes for custom extensions.
        public: SNode * GetAttr(const String & query)
        {
            auto n = Prev;
            while (n && n->IsAttribute ()) {
                if (query == n->Attribute) return n;
                n = n->Prev;
            }
            return nullptr;
        }
        public: inline String TypeToString() const
        {
            switch (Type) {
                case FFD::SType::MachType: return "MachType";
                case FFD::SType::Struct: return "Struct";
                case FFD::SType::Field: return "Field";
                case FFD::SType::Enum: return "Enum";
                case FFD::SType::Const: return "Const";
                case FFD::SType::Format: return "Format";
                case FFD::SType::Attribute: return "Attribute";
                default: return "Unhandled";
            };
        }
        public: inline bool HasExpr() const { return Expr.Count () > 0; }
        public: inline void DbgPrint()
        {
            Dbg << "+(" << Dbg.Fmt ("%p", this) << ")" << TypeToString ()
                << ": ";
            if (HashKey) Dbg << "[hk;HashType:" << HashType << "]";
            if (Array) Dbg << "[arr]";
            if (Variadic) Dbg << "[var]";
            if (VListItem) Dbg << "[vli]";
            if (Composite) Dbg << "[comp]";
            if (Signed) Dbg << "[signed]";
            if (IsAttribute ()) Dbg << " Value: \"" << Attribute << "\"";
            else {
                Dbg << " Name: \"" << Name;
                    if (IsStruct ()) DbgPrintPS (); Dbg << "\"";
            }
            Dbg << ", DType: \"";
            if (nullptr == DType) {
                if (! NoDType ())
                    Dbg << "unresolved:" << DTypeName;
            }
            else Dbg << DType->Name;
            if (IsField ()) DbgPrintPS ();
            Dbg << "\"" << EOL;
        }
        public: inline void DbgPrintPS() // parametrized struct details
        {
            if (Parametrized ()) {
                Dbg << "<";
                Dbg << PS[0].Name;
                    for (int i = 1; i < PS.Count (); i++)
                        Dbg << ", " << PS[i].Name;
                Dbg << ">";
            }
        }
        // where there are no dynamic arrays and expressions
        public: int PrecomputeSize()
        {
            int result {};
            for (auto f : Fields) {
                if (! f->DType) return 0;
                if (! f->Expr.Empty ()) return 0;
                if (f->DType->IsStruct ()) return 0;
                if (f->Array) {
                    int arr_result = 1, i {};
                    for (; i < 3 && ! f->Arr[i].None (); i++) {
                        if (! f->Arr[i].Name.Empty ()) {
                            auto n = NodeByName (f->Arr[i].Name);
                            if (n && ! n->IsIntConst ()) return 0;
                            if (n) arr_result *= n->IntLiteral;
                        }
                        else
                            arr_result *= f->Arr[i].Value;
                    }
                    FFD_ENSURE(i > 0, "array node w/o dimensions?")
                    result += arr_result * f->DType->Size;
                }
                else
                    result += f->DType->Size;
            }
            return result;
        }// PrecomputeSize()
        public: enum class PSType {Type, Field, IntLiteral};
        public: struct PSParam final
        {
            PSParam(String && name, FFD::SNode * field = nullptr,
                String && b = "")
                : Name {name}, Bind {b}
            {
                Dbg << "SNode::PSParam: " << name;
                if (nullptr == field) { Dbg << EOL; return; }
                FFD_ENSURE(field->IsField (), "Only fields can set that")
                if (FFDParser::IsIntLiteral (Name)) {
                    Type = PSType::IntLiteral;
                    Value = FFDParser::ToInt (Name);
                    Dbg << " - intlit: " << Value << EOL;
                    return;
                }
                FFD::SNode * f{};
                field->WalkBackwards ([&](SNode * n) {
                    auto keep_walking = Name != n->Name;
                    if (! keep_walking) f = n;
                    return keep_walking;
                });
                if (f) {
                    Type = FFD::SNode::PSType::Field;
                    // Value set at the instance node at its PS != this PS
                    Dbg << " - instance field: " << Name << " bound to " << Bind
                        << EOL;
                    return;
                }
                Type = FFD::SNode::PSType::Type;
                Dbg << " - type: " << Name << EOL;
            }
            bool inline IsField() { return FFD::SNode::PSType::Field == Type; }
            bool inline IsType() { return FFD::SNode::PSType::Type == Type; }
            PSType Type;
            String Name;
            String Bind; // param name bound to field name (at Name)
            int Value; // if Name contains int. literal
            inline void DbgPrint()
            {
                Dbg << "Name: " << Name << ", Type: ";
                switch (Type) {
                    case FFD::SNode::PSType::Type: Dbg << "type"; break;
                    case FFD::SNode::PSType::Field: Dbg << "field"; break;
                    case FFD::SNode::PSType::IntLiteral: Dbg << "ilit"; break;
                    default: Dbg << "undefined";
                }
                Dbg << ", Bound to: \"" << Bind << "\"";
            }
        };// PSParam
        public: List<PSParam> PS{}; // parametrized struct
        public: inline void PSDbgPrint()
        {
            for (int i = 0; i < PS.Count (); i++)
                Dbg << "PS[" << i << "]: ", PS[i].DbgPrint (), Dbg << EOL;
        }
        public: inline bool Parametrized() const { return ! PS.Empty (); }
        public: inline PSParam * PSParamByName(const String & name)
        {
            for (int i = 0; i < PS.Count (); i++)
                if (name == PS[i].Name) return &(PS[i]);
            return nullptr;
        }
        public: inline PSParam * PSParamByBind(const String & name)
        {
            for (int i = 0; i < PS.Count (); i++)
                if (name == PS[i].Bind) return &(PS[i]);
            return nullptr;
        }
        public: template <typename F> inline PSParam * PSParamBy(F f)
        {
            for (int i = 0; i < PS.Count (); i++)
                if (f (PS[i])) return &(PS[i]);
            return nullptr;
        }
        // Switching between input file version requires some invalidation.
        // example: bool machine type differs in size based on input;
        //          while caching it is ok, because bool is used at many places
        //          within one input, caching across multiple inputs - isn't
        //TODONT remove when the monolithic one is off
        public: inline void Reset()
        {
            WalkForward ([](auto sn) { return sn->Invalidate (), true; });
            WalkBackwards ([](auto sn) { return sn->Invalidate (), true; });
        }
        public: inline void Invalidate()
        {
            if (IsConst () || IsMachType () || IsEnum ()) {
                Dbg << "Invalidate: " << Name << EOL;
                Resolved = Enabled = false;
            }
            else if (IsStruct ()) {
                // reset fields dtype - where said dtype is a machine type
                // depending on an expression
                for (auto f : Fields)
                    f->DType = f->DType && f->DType->IsMachType ()
                        && f->DType->HasExpr () ? nullptr : f->DType;
            }
        }
        // Simplify Helpers
        private: int _uc{};
        public: inline void UseOnce()
        {
            if (_uc < 0x7fffffff) _uc++;
            // all unconditional fields become used
            if (this->IsStruct ())
                for (auto n : this->Fields)
                    if (n && ! n->HasExpr ()) n->UseOnce ();
        }
        // Parser gen. part one
        private: inline void PrintExpr(const List<FFDParser::ExprToken> & list)
        {//TODO static, elsewhere
            //TODO the original expression
            using ETT = FFDParser::ExprTokenType;
            if (list.Count () > 0) Dbg << " ";
            for (auto e : list)
                switch (e.Type) {
                    case ETT::Open: Dbg << "("; break;
                    case ETT::Close: Dbg << ")"; break;
                    case ETT::Symbol: Dbg << e.Symbol; break;
                    case ETT::Number: Dbg.Fmt ("0x%000000008X", e.Value); break;
                    case ETT::opN: Dbg << " ! "; break;
                    case ETT::opNE: Dbg << " != "; break;
                    case ETT::opE: Dbg << " == "; break;
                    case ETT::opG: Dbg << " > "; break;
                    case ETT::opL: Dbg << " < "; break;
                    case ETT::opGE: Dbg << " >= "; break;
                    case ETT::opLE: Dbg << " <= "; break;
                    case ETT::opOr: Dbg << " || "; break;
                    case ETT::opAnd: Dbg << " && "; break;
                    case ETT::opBWAnd: Dbg << " & "; break;
                    case ETT::None: break;
                    default: Dbg << "Unknown expr. token";
                }
        }// PrintExpr()
        private: template <typename F> void PrintPS(F print)
        {
            if (PS.Count () > 0) {
                Dbg << "<";
                print (PS[0]);
                for (int i = 1; i < this->PS.Count (); i++)
                    Dbg << ",", print (PS[i]);
                Dbg << ">";
            }
        }
        public: inline void PrintIfUsed()
        {//LATER to functions with a test-unit: parsed == generated
            if (_uc <= 0) return;
            switch (Type) {
                case FFD::SType::MachType:
                    Dbg << "type " << this->Name << " "; //TODO alias info
                    if (this->Fp) Dbg << ".";
                    if (this->Signed) Dbg << "-";
                    Dbg << Size;
                    break;
                case FFD::SType::Struct:
                    Dbg << "struct " << this->Name;
                    // PSDbgPrint ();
                    PrintPS ([](PSParam & p) {Dbg << p.Name;});
                    Dbg << EOL;
                    for (auto n : this->Fields) if (n) n->PrintIfUsed ();
                    break;
                case FFD::SType::Field:
                    FFD_ENSURE(this->Base != nullptr, "Field with no Base")
                    Dbg << "    ";
                    if (Composite) Dbg << this->DTypeName;
                    else if (Variadic) Dbg << "..."; // TODO key(s)
                    else if (DType) {
                        bool ps_type{};
                        if (this->Base->Parametrized ())
                            for (int i = 0; i < this->Base->PS.Count (); i++)
                                if (this->Base->PS[i].Name == this->DTypeName) {
                                    ps_type = true;
                                    Dbg << this->DTypeName;
                                }
                        if (! ps_type) Dbg << this->DType->Name;
                        PrintPS ([](PSParam & p) {Dbg << p.Name;});
                    }
                    else Dbg << "TODO: " << this->DTypeName;
                    if (! Composite) Dbg << " " << this->Name;
                    if (this->Array) for (auto d : this->Arr) if (! d.None ()) {
                        Dbg << "[";
                        if (! d.Name.Empty ()) Dbg << d.Name;
                        else Dbg << d.Value;
                        Dbg << "]";
                    }
                    break;
                case FFD::SType::Enum:
                    Dbg << "enum " << this->Name << " " << this->DType->Name;
                    this->PrintExpr (this->Expr);
                    Dbg << EOL;
                    for (auto i : this->EnumItems) { //TODO implicit numbering?!
                        Dbg << "    " << i.Name << " ";
                        if (this->DType->Signed) Dbg << i.Value;
                        else Dbg << static_cast<unsigned int>(i.Value);
                        Dbg << EOL;
                        this->PrintExpr (i.Expr);
                    }
                    break;
                case FFD::SType::Const:
                    Dbg << this->Name << " ";
                    if (FFD::SConstType::Text == this->Const)
                        Dbg << "\"" << this->StringLiteral << "\"";
                    else if (FFD::SConstType::Int == this->Const)
                        Dbg << this->IntLiteral;
                    else Dbg << "Unknown Const";
                    break;
                case FFD::SType::Format:
                    Dbg << "format " << this->Name << EOL;
                    for (auto n : this->Fields) if (n) n->PrintIfUsed ();
                    break;
                case FFD::SType::Attribute:
                    if (this->Base) Dbg << "    "; // field attr align
                    Dbg << this->Attribute;
                    break;
                default: Dbg << "Unhandled";
            };
            this->PrintExpr (this->Expr);
            Dbg << EOL;
        }// PrintIfUsed()
    };// SNode

    private: SNode * _root {};
    // An LL is preferable to a list, because each node should be able to look
    // at its neighbors w/o accessing third party objects.
    private: FFD::SNode * _tail {}, * _head {}; // DLL<FFD::SNode>

    public: FFDNode * File2Tree(Stream &);
    // free the memory used by the parameter
    public: static void FreeNode(FFDNode *);
    // get root-level attribute (temporary - until attributes get assigned to
    // their respective nodes)
    public: inline SNode * GetAttr(const String & query) const
    {
        return _root->GetAttr (query);
    }
    public: inline void Invalidate() { _root->Reset ();}
    public: inline SNode * Head() const { return _head; }
};// FFD

NAMESPACE_FFD

#endif