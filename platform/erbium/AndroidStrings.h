// AndroidStrings.h — UTF conversions for the Android port.
// Windows wchar_t = UTF-16, Android wchar_t = UTF-32; UE on Android uses
// UTF-8 char for FString/FName. These helpers convert between all three.
#pragma once

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace ErbiumAndroid
{
    // UTF-16LE byte pairs → UTF-8 string (for reading UE wide data on Windows
    // builds; kept for parity) — not used on Android directly.
    inline std::string Utf16ToUtf8(const uint16_t* u16)
    {
        std::string out;
        for (; *u16; ++u16)
        {
            uint32_t cp = *u16;
            if (cp >= 0xD800 && cp <= 0xDBFF && u16[1] >= 0xDC00 && u16[1] <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (u16[1] - 0xDC00);
                ++u16;
            }
            if (cp < 0x80)
                out.push_back((char)cp);
            else if (cp < 0x800)
            {
                out.push_back((char)(0xC0 | (cp >> 6)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back((char)(0xE0 | (cp >> 12)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back((char)(0xF0 | (cp >> 18)));
                out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    // UTF-32 wchar_t* (Android/Linux wchar_t) → UTF-8
    inline std::string WideToUtf8(const wchar_t* w)
    {
        std::string out;
        if (!w)
            return out;
        for (; *w; ++w)
        {
            uint32_t cp = (uint32_t)*w;
            if (cp < 0x80)
                out.push_back((char)cp);
            else if (cp < 0x800)
            {
                out.push_back((char)(0xC0 | (cp >> 6)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back((char)(0xE0 | (cp >> 12)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back((char)(0xF0 | (cp >> 18)));
                out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    // UTF-8 → UTF-32 std::wstring
    inline std::wstring Utf8ToWide(const char* s)
    {
        std::wstring out;
        for (const uint8_t* p = (const uint8_t*)s; *p;)
        {
            uint32_t cp;
            int len;
            if ((*p & 0x80) == 0)
            {
                cp = *p++;
                len = 0;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                cp = *p++ & 0x1F;
                len = 1;
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                cp = *p++ & 0x0F;
                len = 2;
            }
            else
            {
                cp = *p++ & 0x07;
                len = 3;
            }
            while (len--)
                cp = (cp << 6) | (*p++ & 0x3F);
            out.push_back((wchar_t)cp);
        }
        return out;
    }
} // namespace ErbiumAndroid
