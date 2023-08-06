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

// Don't learn from here.

//c clang++ -std=c++14 -Istl -O0 -g -fsanitize=address,undefined,integer,leak -I. test.cpp -L. -lwind-ffd -Wl,-rpath="${PWD}" -o test

//TODO find out why ("zstd", "squashfs", 47Gb, "parsed: 305466 (todo: 5058)"):
//       * Q=1 MODEL=qt5 | real    251m
//       * Q=1 MODEL=stl | real    450m
//TODO ("xz", "squashfs", 39Gb) Q=1 MODEL=qt5

static_assert(4 == sizeof(int), "I need 32-bit \"int\"");

#include "ffd_model.h"
#include "ffd_dbg.h"
#include "ffd.h"
#include "ffd_node.h"
#include <zlib.h>
#include <new>

#if FFD_TEST_N_FILE_STREAM
#include "n_file_stream.h"
#define FFD_STREAM NFileStream
#else
#define FFD_STREAM FFD_NS::TestStream
#endif

static void test_the_list();
static void test_the_string();
static void test_the_byte_arr();

FFD_NAMESPACE
#ifndef FFD_TEST_N_FILE_STREAM
class TestStream final : public Stream
{
    public: Stream & Read(void * v, size_t b) override
    {
        // printf ("read(%lu, _rb_ptr:%d, _rb_size:%d\n", b, _rb_ptr, _rb_size);
        byte * d = reinterpret_cast<byte *>(v);
        while (b) {
            if (_rb_ptr == _rb_size) {
                _rb_size = fread (_rbuf, 1, _RBUF_SIZE, _f);
                // printf ("%d = fread(%d)\n", _rb_size, _RBUF_SIZE);
                FFD_ENSURE(_rb_size > 0, "fread() failed")
                _rb_ptr = 0;
            }
            auto a = _rb_size - _rb_ptr;
            // printf ("a = %d\n", a);
            auto n = static_cast<int>(b) <= a ? static_cast<int>(b) : a;
            // printf ("n = %d\n", n);
            OS::Memcpy (d, _rbuf + _rb_ptr, n);
            _rb_ptr += n;
            d += n;
            b -= n;
            // printf ("_rb_ptr: %d, b: %lu\n", _rb_ptr, b);
        }
        return *this;
    }
    public: off_t Tell() const override
    {
        return ftell (_f) - (_rb_size - _rb_ptr);
    }
    public: off_t Size() const override { return _s; }
    public: Stream & Seek(off_t o) override
    {
        // printf ("Seek(%ld\n", o);
        auto t = _rb_ptr + o;
        if (t >= 0 && t < _rb_size) {
            _rb_ptr = t;
            return *this;
        }
        else { // slow and simple
            if (t < 0) o = ftell (_f) - _rb_size + t;
            else o = ftell (_f) + (t - _rb_size);
            // printf ("o: %ld\n", o);
            _rb_ptr = _rb_size = 0; // invalidate the read buffer
            return fseek (_f, o, SEEK_SET), *this;
        }
    }
    public: Stream & Reset() override { return Seek (-Tell ()); }
    public: TestStream(const char * fn) : Stream {}, _f{fopen (fn, "rb")}
    {
        OS::Alloc (_rbuf, _RBUF_SIZE);
        if (_f)
            fseek (_f, 0, SEEK_END), _s = ftell (_f), fseek (_f, 0, SEEK_SET);
    }
    public: ~TestStream() override
    {
        if (_f) fclose (_f), _f = nullptr;
        if (_rbuf) OS::Free (_rbuf);
    }
    public: operator bool() const { return nullptr != _f; }
    private: FILE * _f{};
    private: off_t _s{};
    private: byte * _rbuf{};
    private: int const _RBUF_SIZE{1<<17};
    private: int _rb_size{};
    private: int _rb_ptr{};
};
#endif
class TestZipInflateStream final : public Stream
{
    private: z_stream _zs {};
    private: int _zr {~Z_OK}, _size, _usize;
    private: off_t _pos {}; // how many bytes were decoded so far
    private: static uInt constexpr _IN_BUF {1<<12}; // zlib: uInt
    private: byte _buf[_IN_BUF] {};
    private: Stream * _s;
    private: off_t _s0;
    // The h3map one differs in init. No need to create another class.
    public: TestZipInflateStream(Stream * s, int size, int usize,
        bool h3map = false)
        : Stream {}, _size{size}, _usize{usize}, _s{s}, _s0 {s->Tell ()}
    {
        if (h3map) _zr = inflateInit2 (&_zs, 31);
        else _zr = inflateInit (&_zs);
        FFD_ENSURE(Z_OK == _zr, "inflateInit() error")
    }
    public: ~TestZipInflateStream() override { inflateEnd (&_zs); }
    public: inline operator bool() { return Z_OK == _zr; }
    // You can use these for progress: 1.0 * Tell() / Size() * 100
    public: off_t Tell() const override { return _pos; }; // uncompressed
    public: off_t Size() const override { return _usize; }; // uncompressed
    public: Stream & Read(void * buf, size_t bytes = 1) override
    {
        // Dbg << "ZRead(" << buf << ", " << bytes << ")" << EOL;
        FFD_ENSURE(bytes > 0, "bytes can't be <= 0")
        FFD_ENSURE(nullptr != buf, "buf can't be null")
        // The sheer elegance of z_stream is impressive.
        _zs.avail_out = static_cast<uInt>(bytes); // zlib: uInt
        _zs.next_out = static_cast<z_const Bytef *>(buf);
        while (_zs.avail_out > 0) {
            if (_zs.avail_in <= 0) {
                _zs.avail_in = static_cast<uInt>(_size - (_s->Tell () - _s0));
                FFD_ENSURE(_zs.avail_in > 0, "TestZIStream::Read no more input")
                if (_zs.avail_in > _IN_BUF) _zs.avail_in = _IN_BUF;
                _s->Read (_buf, _zs.avail_in);
                _zs.next_in = static_cast<z_const Bytef *>(_buf);
            }
            auto sentinel1 = _zs.avail_in;
            auto sentinel2 = _zs.avail_out;
            _zr = inflate (&_zs, Z_SYNC_FLUSH);
            if (Z_STREAM_END == _zr) _zr = Z_OK;
            FFD_ENSURE(Z_OK == _zr, "TestZIStream::Read error")
            FFD_ENSURE(sentinel1 != _zs.avail_in || sentinel2 != _zs.avail_out,
                "TestZIStream::Read infinite loop case")
        }
        return _pos += bytes, *this;
    } // Read()
};// TestZipInflateStream
NAMESPACE_FFD

namespace __pointless_verbosity
{
    template <typename T> struct __try_finally_free_the_object
    {
        T * _p;
        __try_finally_free_the_object(T * p) : _p{p} {}
        ~__try_finally_free_the_object() { if (_p) FFD_DESTROY_OBJECT(_p, T) }
    };
}

static void parse_h3m(FFD_NS::FFD &, const char *);
static void parse_nif(FFD_NS::FFD &, const char *);
static void parse_directory(FFD_NS::FFD &, const char *, const char *,
    void (*)(FFD_NS::FFD &, const char *), bool = false);

// usage: test ffd dir ext_list(a,b,c,...)
// what does it do: are_equal(data, Tree2File (File2Tree (ffd, data))
int main(int argc, char ** argv)
{
    Dbg.Enabled = false;
        test_the_list ();
        test_the_string ();
        test_the_byte_arr ();
        if (4 != argc)
            return Dbg << "usage: test ffd dir ext_list(a,b,c,...)" << EOL, 0;
    Dbg.Enabled = true;
    {
        FFD_NS::ByteArray ffd_buf {};
        FFD_STREAM ffd_stream {argv[1]};
        Dbg << "Using \"" << argv[1] << "\" (" << ffd_stream.Size ()
            << " bytes) FFD to parse \"" << argv[2] << "\": ";
        FFD_ENSURE(ffd_stream.Size () > 0 && ffd_stream.Size () < 1<<20,
            "Suspicious FFD size")
        ffd_buf.Resize (ffd_stream.Size ());
        ffd_stream.Read (ffd_buf.operator byte * (), ffd_stream.Size ());
#ifdef FFD_QTEST
        Dbg.Enabled = false;
#endif
            FFD_NS::FFD ffd {ffd_buf.operator byte * (), ffd_buf.Length ()};
            if (ffd.GetAttr ("[Stream(type: zlibMapStream)]"))
                return parse_directory (ffd, argv[2], argv[3], parse_h3m), 0;
            else if (FFD_NS::OS::IsDirectory (argv[2]))
                return parse_directory (ffd, argv[2], argv[3], parse_nif), 0;
    }
    return 0;
}// main()

// you manage the returned pointer
static char * CatPath(const char * a, const char * b)
{
    char * r{};
    auto l1 = FFD_NS::OS::Strlen (a);
    auto l2 = 1;
    auto l3 = FFD_NS::OS::Strlen (b);
    FFD_NS::OS::Alloc (r, l1+l2+l3+1);
    FFD_NS::OS::Memcpy (r, a, l1);
    r[l1] = FFD_PATH_SEPARATOR;
    FFD_NS::OS::Memcpy (r+l1+l2, b, l3);
    return r;
}
// naive find the text after the last '.' in "n" - in "m"; m - list: a,b,c,...
static bool MaskMatch(const char * n, const char * m)
{
    auto ln = FFD_NS::OS::Strlen (n);
    for (int i = ln-1 ; i >= 0; i--)
        if ('.' == n[i] && i < static_cast<int>(ln-1))
            if (strcasestr (m, n+i+1)) return true;
    return false;
}
// a recursive one
template <typename T> void enum_files(T t, const char * d, const char * m)
{
    FFD_NS::OS::EnumFiles (d, [&](const char * n, bool directory)
        {
            auto fp = CatPath (d, n);
            FFD_NS::OS::__pointless_verbosity::__try_finally_free<char> _ {fp};
            if (directory) enum_files (t, fp, m);
            else if (MaskMatch (n, m)) t (fp);
            return true;
        });
}

// the files that can not be parsed yet are renamed to .nop
//TODO the misaligned blocks are a primary issue - how to specify the parser
//     to use nif.BlockSize?!
// broken files are renamed to .bro
void parse_nif(FFD_NS::FFD & ffd, const char * n)
{
    FFD_STREAM data_stream {n};
    FFD_NS::FFDNode * tree = ffd.File2Tree (data_stream);
    FFD_ENSURE(nullptr != tree, "parse_nif(): File2Tree() returned null?!")
    // tree->PrintTree ();
    ffd.FreeNode (tree);
}

void parse_directory(FFD_NS::FFD & ffd, const char * r, const char * m,
    void (*pp)(FFD_NS::FFD &, const char *), bool no)
{
    Dbg.Enabled = true;
    Dbg << "parse_directory \"" << r << "\", mask: \"" << m << "\"" << EOL;
    Dbg << "Enumerating, please wait ... ";
    int files{}, todo{}, tfiles{};
    enum_files ([&](const char *) { tfiles++; }, r, m); Dbg << "done" << EOL;
    enum_files ([&](const char * n)
        {
            Dbg << Dbg.Fmt ("[%6d", ++files) << Dbg.Fmt ("/%6d]: ", tfiles)
                << n << EOL;
#ifdef FFD_QTEST
            Dbg.Enabled = no ? Dbg.Enabled : false;
#endif
            ffd.Invalidate ();
            pp (ffd, n);
#ifdef FFD_QTEST
            Dbg.Enabled = no ? Dbg.Enabled : true;
#endif
            if (FFD_NS::FFDNode::SkipAnnoyngFile) {
                FFD_NS::FFDNode::SkipAnnoyngFile = false;
                printf ("Unsupported Version\n");
                todo++;
            }
        }, r, m);
    Dbg << "parsed: " << files; if (todo) Dbg << " (todo: " << todo << ")";
    Dbg << EOL;
}

void parse_h3m(FFD_NS::FFD & ffd, const char * map)
{
    using SIMPLY_STREAM = FFD_NS::Stream;
    using SIMPLY_ZSTREAM = FFD_NS::TestZipInflateStream;
    FFD_STREAM h3m_stream {map};
    SIMPLY_STREAM * data_stream {&h3m_stream};
    // 6167 maps: the largest: 375560 bytes, uncompressed one: 1342755 bytes
    const int H3M_MAX_FILE_SIZE = 1<<21;
    int h, usize{}, size = static_cast<int>(h3m_stream.Size ());
    printf ("(%d bytes)", size);
    FFD_ENSURE(size > 3 && size < H3M_MAX_FILE_SIZE,
        "Suspicious Map size")
    h3m_stream.Read (&h, 4).Reset ();
    if (0x88b1f == h) { // zlibMapStream
        h3m_stream.Seek (size - 4).Read (&usize, 4).Reset ();
        printf (", USize: %d bytes", usize);
        FFD_ENSURE(usize > size && usize < H3M_MAX_FILE_SIZE,
            "Suspicious Map usize")
        FFD_CREATE_OBJECT(data_stream, SIMPLY_ZSTREAM) {
            &h3m_stream, size, usize, /*h3map:*/true};
    }

    __pointless_verbosity::__try_finally_free_the_object<SIMPLY_STREAM> __ {
        &h3m_stream != data_stream ? data_stream : nullptr};
    auto * tree = ffd.File2Tree (*data_stream);
    FFD_ENSURE(nullptr != tree, "parse_nif(): File2Tree() returned null?!")
    // tree->PrintTree ();
    ffd.FreeNode (tree);
    printf (", unprocessed h3m_stream bytes: %lu" EOL,
        h3m_stream.Size () - h3m_stream.Tell ());
}// parse_h3m()

// __ testworks ________________________________________________________________
static const char * TEST_NAME = "";
#define ARE_EQUAL(A,B,M) FFD_ENSURE((A) == (B), M) \
    Dbg << " OK: " << TEST_NAME << " "#A" == "#B EOL;
#define IS_TRUE(A,M) ARE_EQUAL(true, (A), M)
#define IS_ZERO(A,M) ARE_EQUAL(0, (A), M)
#define IS_FALSE(A,M) ARE_EQUAL(false, (A), M)
#define IS_NOT_NULL(A,M) ARE_EQUAL(true, (nullptr != A), M)
#define IS_NULL(A,M) ARE_EQUAL(true, (nullptr == A), M)

void test_the_list()
{
    TEST_NAME="List.List()";
    FFD_NS::List<int> a;
    ARE_EQUAL(nullptr, a.begin (), "list.begin() != nullptr")
    ARE_EQUAL(nullptr, a.end (), "list.end() != nullptr")
    IS_ZERO(a.Count (), "the list has no zero size")
    IS_TRUE(a.Empty (), "the list is not empty")

    TEST_NAME="List.Add()";
    int m = 4;
    int & n = a.Add (m);
    ARE_EQUAL(4, *a.begin(), "begin() != &[0]")
    ARE_EQUAL(4, *(a.end()-1), "end() != &[0]")
    ARE_EQUAL(1, a.Count (), "list.Count() != 1")
    IS_FALSE(a.Empty (), "the list is empty")
    ARE_EQUAL(n, m, "unexpected reference")
    ARE_EQUAL(4, a[0], "unexpected value")
    n = 5;
    ARE_EQUAL(5, a[0], "unexpected reference")
    int & v = a.Add (m); // n becomes dangling ref
    ARE_EQUAL(5, *a.begin(), "begin() != &[0]")
    ARE_EQUAL(4, *(a.end()-1), "end() != &[1]")
    ARE_EQUAL(2, a.Count (), "list.Count() != 2")
    IS_FALSE(a.Empty (), "the list is empty")
    ARE_EQUAL(5, a[0], "unexpected value")
    ARE_EQUAL(4, a[1], "unexpected value")
    ARE_EQUAL(v, m, "unexpected reference")
    v = 6;
    ARE_EQUAL(5, a[0], "unexpected reference")
    ARE_EQUAL(6, a[1], "unexpected reference")

    TEST_NAME="List.Put()";
    a.Put (1).Put (2).Put (3);
    ARE_EQUAL(1, a[2], "unexpected a[2]")
    ARE_EQUAL(2, a[3], "unexpected a[3]")
    ARE_EQUAL(3, a[4], "unexpected a[4]")

    TEST_NAME="List.List(List &&)";
    FFD_NS::List<int> b = static_cast<FFD_NS::List<int> &&>(a);
    IS_ZERO(a.Count (), "move failed")
    ARE_EQUAL(5, b.Count (), "move failed")
    ARE_EQUAL(1, b[2], "unexpected a[2]")
    ARE_EQUAL(2, b[3], "unexpected a[3]")
    ARE_EQUAL(3, b[4], "unexpected a[4]")

    TEST_NAME="List.operator=(List &&)";
    a = static_cast<FFD_NS::List<int> &&>(b);
    IS_ZERO(b.Count (), "move failed")
    ARE_EQUAL(5, a.Count (), "move failed")
    ARE_EQUAL(1, a[2], "unexpected a[2]")
    ARE_EQUAL(2, a[3], "unexpected a[3]")
    ARE_EQUAL(3, a[4], "unexpected a[4]")
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
    a = a;
#pragma clang diagnostic pop
    ARE_EQUAL(5, a.Count (), "move failed")
    ARE_EQUAL(1, a[2], "unexpected a[2]")
    ARE_EQUAL(2, a[3], "unexpected a[3]")
    ARE_EQUAL(3, a[4], "unexpected a[4]")

    TEST_NAME="List.Find()";
    int * f = a.Find([](const auto & v) { return 2 == v; });
    IS_NOT_NULL(f, "found nothing")
    ARE_EQUAL(2, *f, "returned incorrect item")
    f = a.Find([](const auto & v) { return 22 == v; });
    ARE_EQUAL(nullptr, f, "found non-existent item")
}// test_the_list()

void test_the_string()
{
    TEST_NAME="String.String()";
    FFD_NS::String a {};
    IS_TRUE(a.Empty (), "the string is not empty")
    IS_NOT_NULL(a.AsZStr (), "the zstring is null?!")
    IS_ZERO(*a.AsZStr (), "the zsrting isn't zero-terminated?!")
    auto t = a.Split ('.');
    IS_TRUE(t.Empty (), "the empty string returned more than 0 tokens?!")

    TEST_NAME="String.String(const byte *, int)";
    auto t1 = "hi";
    auto t2 = reinterpret_cast<const byte *>(t1);
    FFD_NS::String b {t2, 2};
    IS_FALSE(b.Empty (), "the string is empty")
    ARE_EQUAL('h', b.AsZStr()[0], "unexpected [0]")
    ARE_EQUAL('i', b.AsZStr()[1], "unexpected [1]")
    ARE_EQUAL('\0', b.AsZStr()[2], "unexpected [2]")

    TEST_NAME="String.operator==(const char *)";
    IS_TRUE(b == "hi", "c-str comparison not ok")
    IS_TRUE("hi" == b, "c-str comparison not ok")
    IS_FALSE(nullptr == b, "c-str comparison not ok")
    IS_FALSE(b == nullptr, "c-str comparison not ok")
    IS_FALSE("h" == b, "c-str comparison not ok")
    IS_FALSE("hii" == b, "c-str comparison not ok")
    IS_FALSE(b == "h", "c-str comparison not ok")
    IS_FALSE(b == "hii", "c-str comparison not ok")
    IS_FALSE(b == "ih", "c-str comparison not ok")

    TEST_NAME="String.String(const char *)";
    FFD_NS::String c {"hi"};
    IS_TRUE("hi" == c, "content not ok")
    FFD_NS::String d {""};
    IS_TRUE("" == d, "content not ok")

    TEST_NAME="String.String(const String &)";
    FFD_NS::String e {c};
    IS_TRUE("hi" == c , "content not ok")
    IS_TRUE("hi" == e, "content not ok")

    TEST_NAME="String.String(String &&)";
    FFD_NS::String f {static_cast<FFD_NS::String &&>(e)};
    IS_TRUE("hi" == f, "moved content not ok")
    IS_TRUE("" == e, "moved content not ok")

    TEST_NAME="String.operator==(const String &)";
    IS_TRUE(c == f, "not equal?!")
    TEST_NAME="String.operator!=(const String &)";
    IS_FALSE(c == e, "equal?!")

    TEST_NAME="String.Split()";
    FFD_NS::String g {"a.b.c"};
    auto m = g.Split ('.');
    ARE_EQUAL(3, m.Count (), "incorrect token num")
    ARE_EQUAL("a", m[0], "incorrect token 0")
    ARE_EQUAL("b", m[1], "incorrect token 1")
    ARE_EQUAL("c", m[2], "incorrect token 2")
    auto n = g.Split (',');
    ARE_EQUAL(1, n.Count (), "incorrect token num")
    FFD_NS::String g1 {"aa.b.ccc"};
    auto o = g1.Split ('.');
    ARE_EQUAL(3, o.Count (), "incorrect token num")
    ARE_EQUAL("aa", o[0], "incorrect token 0")
    ARE_EQUAL("b", o[1], "incorrect token 1")
    ARE_EQUAL("ccc", o[2], "incorrect token 2")
    FFD_NS::String g2 {"."};
    auto p = g2.Split ('.');
    ARE_EQUAL(2, p.Count (), "incorrect token num")
    ARE_EQUAL("", p[0], "incorrect token 0")
    ARE_EQUAL("", p[1], "incorrect token 1")
    FFD_NS::String g3 {".."};
    p = g3.Split ('.');
    ARE_EQUAL(3, p.Count (), "incorrect token num")
    ARE_EQUAL("", p[0], "incorrect token 0")
    ARE_EQUAL("", p[1], "incorrect token 1")
    ARE_EQUAL("", p[2], "incorrect token 2")
    FFD_NS::String g4 {"..a"};
    p = g4.Split ('.');
    ARE_EQUAL(3, p.Count (), "incorrect token num")
    ARE_EQUAL("", p[0], "incorrect token 0")
    ARE_EQUAL("", p[1], "incorrect token 1")
    ARE_EQUAL("a", p[2], "incorrect token 2")
    FFD_NS::String g5 {"a.."};
    p = g5.Split ('.');
    ARE_EQUAL(3, p.Count (), "incorrect token num")
    ARE_EQUAL("a", p[0], "incorrect token 0")
    ARE_EQUAL("", p[1], "incorrect token 1")
    ARE_EQUAL("", p[2], "incorrect token 2")
    FFD_NS::String g6 {".a."};
    p = g6.Split ('.');
    ARE_EQUAL(3, p.Count (), "incorrect token num")
    ARE_EQUAL("", p[0], "incorrect token 0")
    ARE_EQUAL("a", p[1], "incorrect token 1")
    ARE_EQUAL("", p[2], "incorrect token 2")
}// test_the_string()

void test_the_byte_arr()
{
    TEST_NAME="ByteArray.ByteArray()";
    FFD_NS::ByteArray a {};
    IS_ZERO(a.Length (), "length is not 0")
    IS_NULL(a.operator byte *(), "the byte * is not null")

    TEST_NAME="ByteArray.Resize()";
    a.Resize(1);
    ARE_EQUAL(1, a.Length (), "length is not 1")
    IS_NOT_NULL(a.operator byte *(), "the byte * is null")
    IS_ZERO(a[0], "element[0] is not 0")
    a[0]=1;
    ARE_EQUAL(1, a[0], "unexpected element[0]")
    a.Resize(2);
    ARE_EQUAL(1, a[0], "unexpected element[0]")
    ARE_EQUAL(0, a[1], "unexpected element[1]")
    ARE_EQUAL(2, a.Length (), "length is not 1")
    ARE_EQUAL(1, a.operator byte * ()[0], "unexpected element[0]")
    ARE_EQUAL(0, a.operator byte * ()[1], "unexpected element[1]")
    a.Resize(1);
    ARE_EQUAL(1, a.Length (), "length is not 1")
    ARE_EQUAL(1, a[0], "unexpected element[0]")
}// test_the_byte_arr()
