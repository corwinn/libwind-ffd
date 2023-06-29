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

//c clang++ -std=c++14 -Istl -O0 -g -fsanitize=address,undefined,integer,leak -I. test.cpp -L. -lwind-ffd -Wl,-rpath="${PWD}" -o test

#include "ffd_model.h"
#include "ffd_dbg.h"
#include "ffd.h"
#include "ffd_node.h"

// compressed streams support
#include <zlib.h>
#include <new>

static void test_the_list();
static void test_the_string();
static void test_the_byte_arr();

FFD_NAMESPACE
// Don't learn from here.
class TestStream final : public Stream
{
    public: Stream & Read(void * v, size_t b) override
    {
        FFD_ENSURE(fread (v, 1, b, _f) == b, "fread() failed")
        return *this;
    }
    public: off_t Tell() const override { return ftell (_f); }
    public: off_t Size() const override { return _s; }
    public: Stream & Seek(off_t o) override
    {
        return fseek (_f, o, SEEK_CUR), *this;
    }
    public: Stream & Reset() override { return Seek (-Tell ()); }
    public: TestStream(const char * fn) : Stream {}, _f{fopen (fn, "rb")}
    {
        if (_f)
            fseek (_f, 0, SEEK_END), _s = ftell (_f), fseek (_f, 0, SEEK_SET);
    }
    public: ~TestStream() override { if (_f) fclose (_f), _f = nullptr; }
    public: operator bool() const { return nullptr != _f; }
    private: FILE * _f {};
    private: off_t _s {};
};
class TestZipInflateStream final : public Stream
{
    private: z_stream _zs {};
    private: int _zr {~Z_OK}, _size, _usize;
    private: off_t _pos {}; // how many bytes were decoded so far
    private: static uInt constexpr _IN_BUF {1<<12}; // zlib: uInt
    private: byte _buf[_IN_BUF] {};
    private: Stream * _s;
    // The h3map one differs in init. No need to create another class.
    public: TestZipInflateStream(Stream * s, int size, int usize,
        bool h3map = false)
        : Stream {}, _size{size}, _usize{usize}, _s{s}
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
                _zs.avail_in = static_cast<uInt>(_size - _s->Tell ());
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

// h3m-specific
using SIMPLY_STREAM=FFD_NS::Stream;
using SIMPLY_ZSTREAM=FFD_NS::TestZipInflateStream;

// usage: test ffd data
// what does it do: are_equal(data, Tree2File (File2Tree (ffd, data))
int main(int argc, char ** argv)
{
    Dbg.Enabled = false;
        test_the_list();
        test_the_string();
        test_the_byte_arr();
        if (3 != argc)
            return Dbg << "usage: test ffd data" << EOL, 0;
        FFD_NS::String bar {"hi"};
        Dbg << bar << "\n";
    Dbg.Enabled = true;
    {
        FFD_NS::ByteArray ffd_buf {};
        FFD_NS::TestStream ffd_stream {argv[1]};
        Dbg << "Using \"" << argv[1] << "\" (" << ffd_stream.Size ()
            << " bytes) FFD to parse \"" << argv[2] << "\": ";
        FFD_ENSURE(ffd_stream.Size () > 0 && ffd_stream.Size () < 1<<20,
            "Suspicious FFD size")
        ffd_buf.Resize (ffd_stream.Size ());
        ffd_stream.Read (ffd_buf.operator byte * (), ffd_stream.Size ());
        Dbg.Enabled = false;
            FFD_NS::FFD ffd {ffd_buf.operator byte * (), ffd_buf.Length ()};
            // h3m-specific processing; - a large test suite
            FFD_NS::TestStream h3m_stream {argv[2]};
            SIMPLY_STREAM * data_stream {&h3m_stream};
            const int H3M_MAX_FILE_SIZE = 1<<21; // 6167 maps: the largest: 375560 bytes
            auto h3map = ffd.GetAttr ("[Stream(type: zlibMapStream)]");
            if (h3map) {
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
                        &h3m_stream, size, usize, nullptr != h3map};
                }
                // printf (EOL);
            }
            auto * tree = ffd.File2Tree (*data_stream);
        Dbg.Enabled = true;
        Dbg << ", unprocessed h3m_stream bytes: "
            << h3m_stream.Size() - h3m_stream.Tell() << EOL;
        FFD_ENSURE(nullptr != tree, "File2Tree() returned null?!")
        // tree->PrintTree ();
        ffd.FreeNode (tree);

        // h3m-specific
        if (&h3m_stream != data_stream)
            FFD_DESTROY_OBJECT(data_stream, SIMPLY_STREAM)
    }
    return 0;
}// main()

// testworks
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
