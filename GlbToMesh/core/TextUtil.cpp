/**
 * @file      TextUtil.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace pomocných funkcí pro texty a cesty (viz TextUtil.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <cstdint>

// Hlavičkové soubory projektu
#include "TextUtil.h"

namespace TextUtil {

	namespace {
		/**
		 * Zjistí délku platné UTF-8 sekvence na zadané pozici. Kontrola je striktní (RFC 3629): odmítá nadměrně dlouhé
		 * zápisy, náhradní páry UTF-16 a hodnoty nad U+10FFFF.
		 *
		 * @return size_t Délka sekvence v bajtech (1 - 4), 0 = na pozici je neplatná sekvence
		 */
		size_t Utf8SequenceLength(const std::string &text, size_t pos)
		{
			const auto at = [&text](size_t i) { return static_cast<unsigned char>(text[i]); };
			const unsigned char c = at(pos);

			if (c < 0x80) {
				return 1;
			}

			size_t length = 0;
			unsigned char low = 0x80; // Přípustný rozsah druhého bajtu
			unsigned char high = 0xBF;

			if (c >= 0xC2 && c <= 0xDF) {
				length = 2;
			}
			else if (c >= 0xE0 && c <= 0xEF) {
				length = 3;

				if (c == 0xE0) {
					low = 0xA0;
				}
				else if (c == 0xED) {
					high = 0x9F;
				}
			}
			else if (c >= 0xF0 && c <= 0xF4) {
				length = 4;

				if (c == 0xF0) {
					low = 0x90;
				}
				else if (c == 0xF4) {
					high = 0x8F;
				}
			}
			else {
				return 0;
			}

			if (pos + length > text.size() || at(pos + 1) < low || at(pos + 1) > high) {
				return 0;
			}

			for (size_t k = 2; k < length; ++k) {

				if ((at(pos + k) & 0xC0) != 0x80) {
					return 0;
				}
			}

			return length;
		}

		/// Zjistí, zda je řetězec platné UTF-8
		bool IsValidUtf8(const std::string &text)
		{
			size_t i = 0;

			while (i < text.size()) {
				const size_t length = Utf8SequenceLength(text, i);

				if (length == 0) {
					return false;
				}

				i += length;
			}

			return true;
		}

		/// Připojí k řetězci jeden bajt ze Windows-1250 zapsaný jako UTF-8 (bajty bez znaku jako U+FFFD)
		void AppendCp1250(std::string &result, unsigned char byte)
		{
			// Znaky Windows-1250 pro bajty 0x80 - 0xFF
			static constexpr char16_t CP1250[128] = {
				0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80
				0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179, // 0x88
				0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90
				0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, // 0x98
				0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, // 0xA0
				0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B, // 0xA8
				0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, // 0xB0
				0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C, // 0xB8
				0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, // 0xC0
				0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E, // 0xC8
				0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, // 0xD0
				0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF, // 0xD8
				0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, // 0xE0
				0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F, // 0xE8
				0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, // 0xF0
				0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9, // 0xF8
			};

			if (byte < 0x80) {
				result += static_cast<char>(byte);

				return;
			}

			const char16_t code = CP1250[byte - 0x80];

			if (code < 0x800) {
				result += static_cast<char>(0xC0 | (code >> 6));
				result += static_cast<char>(0x80 | (code & 0x3F));
			}
			else {
				result += static_cast<char>(0xE0 | (code >> 12));
				result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
				result += static_cast<char>(0x80 | (code & 0x3F));
			}
		}
	}

	std::string FromFixedArray(const char *data, size_t capacity)
	{
		size_t length = 0;

		while (length < capacity && data[length] != '\0') {
			++length;
		}

		return std::string(data, length);
	}

	std::string ToUtf8(const std::string &text)
	{
		if (IsValidUtf8(text)) {
			return text;
		}

		std::string result;
		result.reserve(text.size() * 2);

		for (const char c : text) {
			AppendCp1250(result, static_cast<unsigned char>(c));
		}

		return result;
	}

	std::filesystem::path MakePath(const std::string &text)
	{
		return PathFromUtf8(ToUtf8(text));
	}

	std::string PathToUtf8(const std::filesystem::path &path)
	{
		const std::u8string u8 = path.u8string();

		return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
	}

	std::filesystem::path PathFromUtf8(const std::string &utf8)
	{
		return std::filesystem::path(
			std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size())
		);
	}

	std::string ToLowerAscii(std::string text)
	{
		for (char &c : text) {

			if (c >= 'A' && c <= 'Z') {
				c = static_cast<char>(c - 'A' + 'a');
			}
		}

		return text;
	}
	std::u16string ToUtf16(const std::string &text)
	{
		const std::string utf8 = ToUtf8(text);
		std::u16string result;
		result.reserve(utf8.size());

		size_t i = 0;

		while (i < utf8.size()) {
			const size_t length = Utf8SequenceLength(utf8, i);
			const auto at = [&utf8](size_t k) { return static_cast<unsigned char>(utf8[k]); };
			uint32_t cp = 0xFFFD;

			if (length == 1) {
				cp = at(i);
			}
			else if (length == 2) {
				cp = (static_cast<uint32_t>(at(i) & 0x1F) << 6) | (at(i + 1) & 0x3F);
			}
			else if (length == 3) {
				cp = (static_cast<uint32_t>(at(i) & 0x0F) << 12) | (static_cast<uint32_t>(at(i + 1) & 0x3F) << 6) | (at(i + 2) & 0x3F);
			}
			else if (length == 4) {
				cp = (static_cast<uint32_t>(at(i) & 0x07) << 18) | (static_cast<uint32_t>(at(i + 1) & 0x3F) << 12) | (static_cast<uint32_t>(at(i + 2) & 0x3F) << 6) |
					 (at(i + 3) & 0x3F);
			}

			if (cp >= 0x10000) {
				cp -= 0x10000;
				result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
				result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
			}
			else {
				result.push_back(static_cast<char16_t>(cp));
			}

			i += length == 0 ? 1 : length;
		}

		return result;
	}

	std::string PercentDecode(const std::string &text)
	{
		const auto hex = [](char c) -> int {
			if (c >= '0' && c <= '9') {
				return c - '0';
			}

			if (c >= 'a' && c <= 'f') {
				return c - 'a' + 10;
			}

			if (c >= 'A' && c <= 'F') {
				return c - 'A' + 10;
			}

			return -1;
		};

		std::string result;
		result.reserve(text.size());

		for (size_t i = 0; i < text.size(); ++i) {

			if (text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0) {
				result.push_back(static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2])));
				i += 2;
			}
			else {
				result.push_back(text[i]);
			}
		}

		return result;
	}

	std::string SanitizeFileName(const std::string &text, const std::string &fallback)
	{
		std::string source = ToUtf8(text);

		// Jen poslední část cesty
		const size_t slash = source.find_last_of("/\\");

		if (slash != std::string::npos) {
			source.erase(0, slash + 1);
		}

		std::string result;

		for (const char c : source) {
			const unsigned char u = static_cast<unsigned char>(c);

			if (u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*' || c == '/' || c == '\\') {
				result.push_back('_');
			}
			else {
				result.push_back(c);
			}
		}

		// Windows nepřipouští názvy končící tečkou nebo mezerou; samotné tečky by označovaly adresář
		while (!result.empty() && (result.back() == '.' || result.back() == ' ')) {
			result.pop_back();
		}

		while (!result.empty() && result.front() == ' ') {
			result.erase(result.begin());
		}

		if (result.empty()) {
			return fallback;
		}

		// Vyhrazené názvy zařízení Windows (CON, NUL, COM1, ...) včetně přípony
		std::string stem = ToLowerAscii(result.substr(0, result.find('.')));
		const bool reserved = stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
							  ((stem.size() == 4) && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9');

		return reserved ? "_" + result : result;
	}
}
