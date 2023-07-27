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

#ifdef _WIN32
# define FFD_PATH_SEPARATOR '\\'
#else
# define FFD_PATH_SEPARATOR '/'
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
# include <sys/types.h>
# include <dirent.h>
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
#define FFD_CREATE_OBJECT(P,T) \
    ::FFD_NS::OS::Alloc (reinterpret_cast<T *&>(P)), new (P) T
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

auto Strlen = [](const char * str) { return strlen (str); };

auto Memcpy = [](void * dest, const void * src, size_t n)
{
    memcpy (dest, src, n);
};

auto Memmove = [](void * dest, const void * src, size_t n)
{
    memmove (dest, src, n);
};

template <typename T> void Alloc(T *& p, size_t n = 1)
{
    FFD_ENSURE(n > 0, "n < 1")
    p = reinterpret_cast<T *>(calloc (n, sizeof(T)));
    if (! p)
        Exit (2);
}
template <typename T> void Free(T * & p) { if (p) free (p), p = nullptr; }

namespace __pointless_verbosity
{
    struct try_finally_closedir
    {// instead of "try { ... } finally { ... }"
        DIR * d;
        try_finally_closedir(DIR * v) : d(v) {}
        ~try_finally_closedir() { if (d) closedir (d), d = 0; }
    };
    template <typename T> struct __try_finally_free
    {
        T * _state;
        __try_finally_free(T * state) : _state {state} {}
        ~__try_finally_free() { OS::Free (_state); }
        inline void Realloc(int s)
        {
            if (_state) OS::Free (_state); OS::Alloc (_state, s);
        }
    };
}

#ifdef _WIN32
#error implement me
#else
inline bool IsDirectory(const char * f)
{
    printf ("IsDirectory: %s" EOL, f);
    struct stat t{};
    FFD_ENSURE(0 == stat (f, &t), "stat failed")
    return S_ISDIR(t.st_mode);
}

template <typename T> bool EnumFiles(const char * dn, T on_file)
{
    FFD_ENSURE(nullptr != on_file, "on_file can't be null")
    FFD_ENSURE(nullptr != dn, "path name can't be null")
    auto stat_dlen = strlen (dn);
    FFD_ENSURE(stat_dlen > 0, "path name length can't be <= 0")
    if (FFD_PATH_SEPARATOR == dn[stat_dlen-1]) stat_dlen--;

    DIR * ds = opendir (dn);
    FFD_ENSURE(nullptr != ds, "can't open directory")
    __pointless_verbosity::try_finally_closedir ____ {ds};
    char * stat_name;
    int stat_name_max_size {4096};
    OS::Alloc (stat_name, stat_name_max_size);
    __pointless_verbosity::__try_finally_free<char> ___ {stat_name};
    for (dirent * de = nullptr;;) {
        de = readdir (ds);
        FFD_ENSURE(! errno, "can't readdir")
        if (! de) break;
        if (! de->d_name[0]) {
            printf ("Warning: readdir(): empty d_name" EOL);
            continue;
        }
        auto len = strlen (de->d_name);
        if (1 == len && '.' == de->d_name[0]) continue;
        if (2 == len && '.' == de->d_name[0]
                     && '.' == de->d_name[1]) continue;

        int stat_clen = stat_dlen + 1 + len + 1; // +1 -> PS, +1; -> '\0'
        if (stat_clen > stat_name_max_size)
            ___.Realloc (stat_name_max_size = stat_clen);
        OS::Memmove (stat_name, dn, stat_dlen);
        stat_name[stat_dlen] = FFD_PATH_SEPARATOR;
        Memmove (stat_name + stat_dlen + 1, de->d_name, len); // +1 -> PS
        stat_name[stat_clen-1] = '\0'; // +1 computed above

        struct stat finfo{};
        FFD_ENSURE(0 == stat (stat_name, &finfo), "stat failed")
        bool dir = S_ISDIR(finfo.st_mode);
        if (! dir && ! S_ISREG(finfo.st_mode)) {
            printf ("Warning: readdir(): skipped ! file && ! directory: "
                "%s%c%s" EOL, dn, FFD_PATH_SEPARATOR, de->d_name);
            continue;
        }
        if (! on_file (de->d_name, dir)) break;
    } // for (;;)
    return true;
} // EnumFiles
#endif

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
    public: int Length() const { return _.Length (); } // [bytes]
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