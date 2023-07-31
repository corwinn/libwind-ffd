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

#ifndef _FFD_DBG_H_
#define _FFD_DBG_H_

#include "ffd_model.h"

FFD_NAMESPACE

// Convenience.
struct UnqueuedThreadSafeDebugLog final
{
    using L = UnqueuedThreadSafeDebugLog;
    template <typename T> inline L & Fmt(const char * f, T & v)
    {
        if (Enabled) printf ("%s: ", Channel), printf (f, v);
        return *this;
    }
    template <typename T> inline L & Fmt(const char * f, T && v)
    {
        if (Enabled) printf ("%s: ", Channel), printf (f, v);
        return *this;
    }
    inline L & operator<<(const char * v) { return Fmt ("%s", v); }
    inline L & operator<<(long v) { return Fmt ("%ld", v); }
    inline L & operator<<(unsigned long v) { return Fmt ("%lu", v); }
    inline L & operator<<(int v) { return Fmt ("%d", v); }
    inline L & operator<<(unsigned int v) { return Fmt ("%u", v); }
    inline L & operator<<(short v) { return Fmt ("%d", v); }
    inline L & operator<<(byte v) { return Fmt ("%d", v); }
    inline L & operator<<(const String & v)
    {
        return Fmt ("%s", v.AsZStr ());
    }
    inline L & operator<<(void * & v) { return Fmt ("%p", v); }
    inline L & operator<<(L & l) { return l; }

    bool Enabled{true};
    const char * const Channel;
    FFD_EXPORT static UnqueuedThreadSafeDebugLog & D();
    UnqueuedThreadSafeDebugLog(const char * c = nullptr, bool e = true)
        : Enabled{e}, Channel{c ? strdup (c) : strdup ("")}
    {
    }
};

NAMESPACE_FFD

#define Dbg (::FFD_NS::UnqueuedThreadSafeDebugLog::D ())

// Not quite ..., but should do.
// You can either ".Enabled = false" or "DBG_CHANNEL(,,false)"
#define DBG_CHANNEL(V,N,E) auto V ::FFD_NS::UnqueuedThreadSafeDebugLog {N, E};

#endif