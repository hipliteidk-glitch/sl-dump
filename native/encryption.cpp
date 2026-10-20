#include "encryption.h"

// Scalar port of the SSE2 original. The original SSE2 block filled the table
// with consecutive values (0,1,2,3, 4,5,6,7, ... 252..255), so we build that
// identity table deterministically here instead of relying on uninitialised
// stack memory.
bool DecodeWithKey(DecodeBuffer* decodeBuffer, const vector<uint8_t>& magic_key, char magic_char, const __m128_temp* xmmword)
{
    int32_t* flat = &decodeBuffer->buffer[0].m128i_i32[0];
    for (int i = 0; i < 256; i++)
    {
        flat[i] = i;
    }
    decodeBuffer->i32_1 = 0;
    decodeBuffer->i32_2 = 0;

    size_t magic_key_pos = 0;
    uint8_t magic_char_2 = (uint8_t)magic_char;
    uint8_t magic_char_3 = (uint8_t)magic_char;
    uint8_t v17 = 0;
    bool v15 = true;
    bool rtn = false;

    for (size_t i = 0; i < 256; i++)
    {
        magic_char_3 = (uint8_t)((magic_char_3 << 7) + (magic_char_3 >> 1));
        if (v15)
        {
            magic_char_2 = (uint8_t)(magic_char_2 + ((magic_char_3 & 1) + 2) * magic_key[magic_key_pos]);
        }
        uint8_t temp_char = magic_char_3 ^ magic_key[magic_key_pos];
        if (magic_char_3 == magic_key[magic_key_pos])
        {
            if (v15)
                rtn = magic_char_2 == magic_key[magic_key_pos + 1];
            if (!rtn) cout << (char)magic_char_2 << " " << (char)magic_char_3 << " " << (char)magic_key[magic_key_pos + 1] << endl;
            magic_char_3 = (uint8_t)(((uint8_t)magic_char >> 1) + ((uint8_t)magic_char << 7));
            magic_key_pos = 0;
            v15 = false;
            temp_char = magic_char_3 ^ magic_key[0];
        }
        int32_t v13 = flat[i];
        v17 = (uint8_t)(v17 + (temp_char + v13));
        flat[i] = flat[v17];
        magic_key_pos++;
        flat[v17] = v13;
    }
    return rtn;
}

void FinishDecode(DecodeBuffer* decodeBuffer, vector<uint8_t>& chunk_buffer)
{
    int32_t* flat = &decodeBuffer->buffer[0].m128i_i32[0];
    for (size_t i = 0; i < chunk_buffer.size(); i++)
    {
        decodeBuffer->i32_1 = 0xFF & (decodeBuffer->i32_1 + 1);
        int32_t v7 = flat[decodeBuffer->i32_1];

        decodeBuffer->i32_2 = 0xFF & (decodeBuffer->i32_2 + v7);
        int32_t v9 = flat[decodeBuffer->i32_2];

        flat[decodeBuffer->i32_1] = v9;
        flat[decodeBuffer->i32_2] = v7;

        chunk_buffer[i] ^= decodeBuffer->buffer[0].m128i_u8[4 * (uint8_t)(v7 + v9)];
    }
}

void DecodeChunk(vector<uint8_t>& chunk_buffer, const vector<uint8_t>& magic_key, const __m128_temp* xmmword, char magic_char)
{
    DecodeBuffer decodeBuffer;
    if (DecodeWithKey(&decodeBuffer, magic_key, magic_char, xmmword))
    {
        FinishDecode(&decodeBuffer, chunk_buffer);
    }
}
