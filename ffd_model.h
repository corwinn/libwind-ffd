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

#define FFD_ENSURE(C,M) if (! (C)) { \
    printf ("Assertion Failed: %s:%d: " M EOL, __FILE__, __LINE__); \
    OS::Exit (1); \
    }

// #include < new >
#define FFD_CREATE_OBJECT(P,T) OS::Alloc (P), new (P) T
#define FFD_DESTROY_OBJECT(P,T) \
    { if (nullptr != P) { P->~T (); OS::Free (P); } }
// Nothing is simple, nor unified, with these people.
// N - nested type; T - nested type; :) Say: you have foo { bar {}}
// How to call foo::bar::~bar()? Read the question.
#define FFD_DESTROY_NESTED_OBJECT(P,N,T) \
    { if (nullptr != P) { P->N::~T (); OS::Free (P); } }

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

template <typename T> class List
{
    public: List();
    public: List(List<T> &&);
    public: const T * begin() const;
    public: const T * end  () const;
    public: int Count() const;
    public: inline bool Empty() const { return Count () <= 0; }
    public: T & Add(const T &);
    public: List<T> & Put(T &&); // same as add, just T&&
    public: T & operator[](int);
    public: const T & operator[](int) const;
    public: List<T> & operator=(List<T> &&);
    public: template <typename Fd> T * Find(Fd on_itm);
};

class String
{
    public: String();
    public: explicit String(const byte *, int);
    public: String(const char *);
    public: String(const String &);
    public: String(String &&);
    public: String & operator=(const String &);
    public: String & operator=(String &&);
    public: bool Empty() const;
    public: bool operator==(const String &) const;
    public: bool operator!=(const String &) const;
    public: bool operator==(const char *) const;
    public: const char * AsZStr() const;
    public: List<String> Split(char);
};

inline bool operator==(const char * c, const String & s)
{
    return s.operator== (c);
}

class ByteArray// = Buffer
{
    public: int Length() const;
    public: operator byte * () const;
    public: byte & operator[](int);
    public: void Resize(int);
};

class Stream
{
    public: Stream & Read(void *, size_t bytes = 1);
    public: off_t Tell() const;
    public: off_t Size() const;
};

NAMESPACE_FFD

#endif