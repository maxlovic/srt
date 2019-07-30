#include <gtest/gtest.h>

#include <array>
#include <string>
#include <cstdint>
#include <vector>


using namespace std;


/// Decode StreamID to key-value pairs
///
/// @return >0 - number of decoded elements (should be even)
/// @return -1 - failure
///
int srt_streamid_decode(const char* streamid, size_t streamid_len, char** keyval_pair, size_t num_elems, size_t elem_size)
{
    static const char stdhdr[] = "#!::";

    const string str(streamid, streamid_len);
    size_t keyval_idx = 0;
    size_t start = 4;
    string query_pair;
    while (keyval_idx + 2 <= num_elems)
    {
        const size_t next_coma = str.find(",", start);
        const size_t next_eq   = str.find("=", start);
        const size_t pair_len = next_coma == string::npos ? (str.size() - start) : (next_coma - start);

        if (next_eq > next_coma)
            return -1;

        const size_t key_len = next_eq - start;
        if (key_len > elem_size)
            return -1;

        memcpy(keyval_pair[keyval_idx], str.c_str() + start, key_len);
        keyval_pair[keyval_idx][key_len] = '\0';
        ++keyval_idx;

        const size_t value_len = (start + pair_len) - next_eq - 1;
        if (value_len > elem_size)
            return -1;

        memcpy(keyval_pair[keyval_idx], str.c_str() + next_eq + 1, value_len);
        keyval_pair[keyval_idx][value_len] = '\0';
        ++keyval_idx;

        if (next_coma == string::npos)
            return (int) keyval_idx;

        start = next_coma + 1;
    }

    return -1;
}


/// Encode key-value pairs to StreamID
///
/// @return >0 - number of decoded elements (should be even)
/// @return -1 - failure
///
int srt_streamid_encode(char* streamid, size_t streamid_len, const char** keyval_pair, size_t num_elems)
{
    memcpy(streamid, "#!::", 4);
    size_t pos = 4;

    for (size_t i = 0; i < num_elems; i += 2)
    {
        if (i != 0)
            streamid[pos++] = ',';

        const size_t keylen = strlen(keyval_pair[i]);

        memcpy(streamid + pos, keyval_pair[i], keylen);
        pos += keylen;
        streamid[pos++] = '=';

        const size_t vallen = strlen(keyval_pair[i + 1]);
        memcpy(streamid + pos, keyval_pair[i + 1], vallen);
        pos += vallen;
    }

    return 0;
}


TEST(StreamID, Decode)
{
    const string streamid = "#!::u=haivision,r=resource name,s=154484316484,t=stream,m=request";

    const size_t elem_size = 64;
    const size_t num_elems = 10;
    char buffer[num_elems * elem_size];
    char* result[num_elems] = {};
    for (size_t i = 0; i < num_elems; ++i)
    {
        result[i] = buffer + i * elem_size;
    }

    const size_t res = srt_streamid_decode(streamid.c_str(), streamid.size(), result, num_elems, elem_size);
    EXPECT_EQ(res, 10);

    EXPECT_EQ(strcmp(result[0], "u"), 0);
    EXPECT_EQ(strcmp(result[1], "haivision"), 0);
    EXPECT_EQ(strcmp(result[2], "r"), 0);
    EXPECT_EQ(strcmp(result[3], "resource name"), 0);
    EXPECT_EQ(strcmp(result[4], "s"), 0);
    EXPECT_EQ(strcmp(result[5], "154484316484"), 0);
    EXPECT_EQ(strcmp(result[6], "t"), 0);
    EXPECT_EQ(strcmp(result[7], "stream"), 0);
    EXPECT_EQ(strcmp(result[8], "m"), 0);
    EXPECT_EQ(strcmp(result[9], "request"), 0);
}



TEST(StreamID, Encode)
{
    const size_t num_elems = 10;
    const char *pairs[num_elems] = {
        "u", "haivision",
        "r", "resource name",
        "s", "154484316484",
        "t", "stream",
        "m", "request",
    };

    const string streamid = "#!::u=haivision,r=resource name,s=154484316484,t=stream,m=request";

    char result[256] = {};
    const size_t res = srt_streamid_encode(result, sizeof result, pairs, num_elems);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(strcmp(result, streamid.c_str()), 0);
}


TEST(StreamID, DecodeTooSmallElemSize)
{
    const string streamid = "#!::u=haivision";

    const size_t elem_size = 8;
    const size_t num_elems = 8;
    char buffer[num_elems * elem_size];
    char* result[num_elems] = {};
    for (size_t i = 0; i < num_elems; ++i)
    {
        result[i] = buffer + i * elem_size;
    }
    const size_t res = srt_streamid_decode(streamid.c_str(), streamid.size(), result, num_elems, elem_size);
    EXPECT_EQ(res, -1);
}


TEST(StreamID, DecodeTooFewElements)
{
    const string streamid = "#!::u=haivision,r=resource name,s=154484316484,t=stream,m=request";

    const size_t elem_size = 64;
    const size_t num_elems = 4;
    char buffer[num_elems * elem_size];
    char* result[num_elems] = {};
    for (size_t i = 0; i < num_elems; ++i)
    {
        result[i] = buffer + i * elem_size;
    }
    const size_t res = srt_streamid_decode(streamid.c_str(), streamid.size(), result, num_elems, elem_size);
    EXPECT_EQ(res, -1);
}


TEST(StreamID, DecodeNoEq)
{
    const string streamid = "#!::uhaivision";

    const size_t elem_size = 64;
    const size_t num_elems = 4;
    char buffer[num_elems * elem_size];
    char* result[num_elems] = {};
    for (size_t i = 0; i < num_elems; ++i)
    {
        result[i] = buffer + i * elem_size;
    }
    const size_t res = srt_streamid_decode(streamid.c_str(), streamid.size(), result, num_elems, elem_size);
    EXPECT_EQ(res, -1);
}


TEST(StreamID, DecodeEqAfterComa)
{
    const string streamid = "#!::uhaivision,r=resource name";

    const size_t elem_size = 64;
    const size_t num_elems = 4;
    char buffer[num_elems * elem_size];
    char* result[num_elems] = {};
    for (size_t i = 0; i < num_elems; ++i)
    {
        result[i] = buffer + i * elem_size;
    }
    const size_t res = srt_streamid_decode(streamid.c_str(), streamid.size(), result, num_elems, elem_size);
    EXPECT_EQ(res, -1);
}


