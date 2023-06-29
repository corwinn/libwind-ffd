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

/*
     How to (so far)
    .................

  1. #define FFD_LIST_IMPL       at "ffd_model_list.h"
  2. #define FFD_STRING_IMPL     at "ffd_model_string.h"
  3. #define FFD_BYTE_ARRAY_IMPL at "ffd_model_byte_array.h"
  4. the test suite shall pass
  5. Extend "Stream"

*/

#ifndef _FFD_MODEL_H_
#define _FFD_MODEL_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using byte = unsigned char;

#ifndef EOL
# ifdef _WIN32
#  define EOL "\r\n"
# else
#  define EOL "\n"
# endif
#endif

#define FFD_NS FFD
#define FFD_NAMESPACE namespace FFD {
#define NAMESPACE_FFD }
#define FFD_EXPORT __attribute__((visibility("default")))

#define FFD_ENSURE(C,M) if (! (C)) { \
    printf ("Assertion Failed: %s:%d: " M EOL, __FILE__, __LINE__); \
    ::FFD_NS::OS::Exit (1); \
    }

// #include < new >
#define FFD_CREATE_OBJECT(P,T) ::FFD_NS::OS::Alloc (P), new (P) T
#define FFD_DESTROY_OBJECT(P,T) \
    { if (nullptr != P) { P->~T (); ::FFD_NS::OS::Free (P); } }
// Nothing is simple, nor unified, with these people.
// N - nested type; T - nested type; :) Say: you have foo { bar {}}
// How to call foo::bar::~bar()? Read the question.
#define FFD_DESTROY_NESTED_OBJECT(P,N,T) \
    { if (nullptr != P) { P->N::~T (); ::FFD_NS::OS::Free (P); } }

// Defines FFD_LIST_IMPL
#include "ffd_model_list.h"
// Defines FFD_STRING_IMPL
#include "ffd_model_string.h"
// Defines FFD_BYTE_ARRAY_IMPL
#include "ffd_model_byte_array.h"

FFD_NAMESPACE

namespace OS {

auto Exit = [](int status) __attribute__((__noreturn__)) { exit (status); };

auto Strncmp = [](const char * a, const char * b, size_t n)
{
    return strncmp (a, b, n);
};

template <typename T> void Alloc(T *& p, size_t n = 1)
{
    FFD_ENSURE(n > 0, "n < 1")
    p = reinterpret_cast<T *>(calloc (n, sizeof(T)));
    if (! p)
        Exit (2);
}
template <typename T> void Free(T * & p) { if (p) free (p), p = nullptr; }

}

// How to? Define FFD_LIST_IMPL.
// This is intended to contain things that have destructors; so ensure
// FFD_LIST_IMPL has ~FFD_LIST_IMPL().
template <typename T> class List
{
    public: List() {}
    public: List(List<T> && v)
    {
        _.operator= (static_cast<FFD_LIST_IMPL &&>(v._));
    }
    public: List(const List<T> & v) { _.operator= (v._); }
    public: const T * begin() const { return _.begin (); }
    public: const T * end  () const { return _.end (); }
    public: int Count() const { return _.Count (); }
    public: inline bool Empty() const { return Count () <= 0; }
    public: T & Add(const T & v) { return _.Add (v); }
    public: List<T> & Put(T && v) // same as add, just T&&
    {
        return _.Put (static_cast<T &&>(v)), *this;
    }
    public: T & operator[](int i) { return _[i]; }
    public: const T & operator[](int i) const { return _[i]; }
    public: List<T> & operator=(List<T> && v)
    {
        if (&v == this) return *this;
        return  _.operator= (static_cast<FFD_LIST_IMPL &&>(v._)), *this;
    }
    public: List<T> & operator=(const List<T> & v)
    {
        if (&v == this) return *this;
        return  _.operator= (v._), *this;
    }
    public: template <typename Fd> T * Find(Fd on_itm)
    {
        return _.Find (on_itm);
    }
    private: FFD_LIST_IMPL _ {};
};// class List

class String
{
    public: String() : _ {} {}
    public: explicit String(const byte * b, int l) : _ {b, l} {}
    public: String(const char * c) : _ {c} {}
    public: String(const String & s) { operator= (s); }
    public: String(String && s) { operator= (static_cast<String &&>(s)); }
    public: String & operator=(const String & v)
    {
        return _.operator= (v._), *this;
    }
    public: String & operator=(String && v)
    {
        return _.operator= (static_cast<FFD_STRING_IMPL(List<String>) &&>(v._)),
            *this;
    }
    public: bool Empty() const { return _.Empty (); }
    public: bool operator==(const String & v) const
    {
        return _.operator== (v._);
    }
    public: bool operator!=(const String & v) const
    {
        return _.operator!= (v._);
    }
    public: bool operator==(const char * v) const { return _.operator== (v); }
    public: const char * AsZStr() const { return _.AsZStr (); }
    // Return 1 token when the delimiter isn't found.
    public: List<String> Split(char d) { return _.Split (d); }
    private: FFD_STRING_IMPL(List<String>) _;
}; // String

inline bool operator==(const char * c, const String & s)
{
    return s.operator== (c);
}

class ByteArray// = Buffer
{
    public: int Length() const { return _.Length (); }
    public: operator byte * () const { return _.operator byte * (); }
    public: byte & operator[](int i) { return _[i]; }
    public: void Resize(int b) { _.Resize (b); }
    private: FFD_BYTE_ARRAY_IMPL _;
};

// Unlike the 3 above, this is something you pass; so extend it as you see fit.
// All sizes and offsets are in [bytes].
// Failure is handled by the actual IO - this one is expected to return on
// success only.
class Stream
{
    public: virtual Stream & Read(void *, size_t = 1) { return *this; }
    public: virtual off_t Tell() const { return 0; }
    public: virtual off_t Size() const { return 0; }
    public: virtual Stream & Seek(off_t) { return *this; } // relative - always
    // you've just been constructed
    public: virtual Stream & Reset() { return *this; }
    public: Stream() {}
    public: virtual ~Stream() {}
};

NAMESPACE_FFD

#endif