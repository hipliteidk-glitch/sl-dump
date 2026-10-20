#include "encryption.h"
#include <string>
#include <string.h>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include "tinf.h"

#ifdef _WIN32
#define DllExport  __declspec(dllexport)
#else
#define DllExport __attribute__ ((visibility ("default")))
#endif

extern "C" {
	__m128_temp _xmmword;
	vector<uint8_t> _magic_key;

	static void reset_xmmword() {
		_xmmword.m128i_u32[0] = 0x0;
		_xmmword.m128i_u32[1] = 0x1;
		_xmmword.m128i_u32[2] = 0x2;
		_xmmword.m128i_u32[3] = 0x3;
		_magic_key.resize(256);
	}

	static char* scramble(char* buffer_ptr, size_t v33, uint8_t magic_char) {
		uint8_t v35 = magic_char;
		if ((int)(v33 + 1) > 0) {
			uint8_t v34 = magic_char;
			for (size_t i = 0; i <= v33; i++) {
				v34 = (uint8_t)((v34 << 7) + (v34 >> 1));
				*buffer_ptr ^= v34;
				v35 = (uint8_t)(v35 + *buffer_ptr * ((v34 & 1) + 2));
				buffer_ptr++;
			}
		}
		*buffer_ptr = (char)v35;
		return (char*)&_magic_key[0];
	}

	DllExport char* make_key(char* c_title, char* c_copyright, char* c_project, char magic_char) {
		reset_xmmword();
		char* buffer_ptr = (char*)&_magic_key[0];
		memset(buffer_ptr, 0, 256);

		size_t str_size = 0;
		memcpy(buffer_ptr, c_title, strlen(c_title));
		str_size += strlen(c_title);
		memcpy(buffer_ptr + str_size, c_copyright, strlen(c_copyright));
		str_size += strlen(c_copyright);
		memcpy(buffer_ptr + str_size, c_project, strlen(c_project));

		memset(buffer_ptr + 128, 0, 0x80u);

		size_t v33 = strlen(buffer_ptr);
		return scramble(buffer_ptr, v33, (uint8_t)magic_char);
	}

	DllExport char* make_key_combined(char* data, char magic_char) {
		reset_xmmword();
		char* buffer_ptr = (char*)&_magic_key[0];
		memset(buffer_ptr, 0, 256);

		memcpy(buffer_ptr, data, strlen(data));
		memset(buffer_ptr + 128, 0, 0x80u);

		size_t v33 = strlen(buffer_ptr);
		return scramble(buffer_ptr, v33, (uint8_t)magic_char);
	}

	DllExport char* make_key_w(wchar_t* c_title, wchar_t* c_copyright, wchar_t* c_project, char magic_char) {
		reset_xmmword();
		char* buffer_ptr = (char*)&_magic_key[0];

		size_t s_title = wcslen(c_title) * 2;
		size_t s_copyright = wcslen(c_copyright) * 2;
		size_t s_project = wcslen(c_project) * 2;

		if (s_title && ((char*)c_title)[s_title - 1] == 0) s_title--;
		if (s_copyright && ((char*)c_copyright)[s_copyright - 1] == 0) s_copyright--;
		if (s_project && ((char*)c_project)[s_project - 1] == 0) s_project--;

		memset(buffer_ptr, 0, 256);
		size_t str_size = 0;
		memcpy(buffer_ptr, c_title, s_title);
		str_size += s_title;
		memcpy(buffer_ptr + str_size, c_copyright, s_copyright);
		str_size += s_copyright;
		memcpy(buffer_ptr + str_size, c_project, s_project);

		memset(buffer_ptr + 128, 0, 0x80u);

		size_t v33 = wcslen((wchar_t*)buffer_ptr) * 2;
		return scramble(buffer_ptr, v33, (uint8_t)magic_char);
	}

	DllExport uint8_t* decode_chunk(char* chunk_data, int chunk_size, char magic_key, char* wrapperKey) {
		uint8_t* buff = (uint8_t*)malloc(chunk_size);
		vector<uint8_t> chunk_buffer(&chunk_data[0], &chunk_data[chunk_size]);
		vector<uint8_t> key_buffer(&wrapperKey[0], &wrapperKey[257]);

		DecodeChunk(chunk_buffer, key_buffer, &_xmmword, magic_key);
		for (int i = 0; i < chunk_size; i++) buff[i] = chunk_buffer[i];
		return buff;
	}

	DllExport uint32_t GenChecksum(wchar_t* name, wchar_t* password)
	{
		int32_t xor_result = 0x3939;
		int16_t v2 = (int16_t)name[0];
		wchar_t* v3 = name;
		if (v2)
		{
			do
			{
				++v3;
				xor_result += v2 ^ 0x7FFF;
				v2 = (int16_t)*v3;
			} while (*v3);
		}
		wchar_t* v4 = password;
		const wchar_t* v5 = L"mqojhm:qskjhdsmkjsmkdjhq\u0063clkcdhdlkjhd";
		if (password[0])
		{
			do
			{
				const wchar_t* v6 = v5 + 1;
				wchar_t v7 = (wchar_t)(*(v6 - 1) + (*v4 ^ 0xC3C3));
				++v4;
				*(v4 - 1) = v7;
				xor_result += v7 ^ 0xF3F3;
				const wchar_t* v8 = L"mqojhm:qskjhdsmkjsmkdjhq\u0063clkcdhdlkjhd";
				if (*v6) v8 = v6;
				v5 = v8;
			} while (*v4);
		}
		return (uint32_t)xor_result;
	}

	DllExport int32_t decompressOld(uint8_t* source, int source_size, uint8_t* output, uint32_t output_size) {
		tinf_init();
		uint32_t actual_size = output_size;
		int result = tinf_uncompress(output, &actual_size, source, source_size);
		if (result > 0)
			return result;
		return (int32_t)actual_size;
	}
}
