/**
 * @file      AppInfo.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace údajů o programu (viz AppInfo.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <iostream>
#include <vector>

// Hlavičkové soubory projektu
#include "AppInfo.h"

// Windows.h jako poslední, aby jeho makra (min, max, ...) neovlivnila ostatní hlavičky
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#pragma comment(lib, "version.lib")
#endif

namespace AppInfo {

	namespace {

#ifdef _WIN32
		/// Text UTF-16 převede na UTF-8
		std::string ToUtf8(const wchar_t *text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

			if (size <= 1) {
				return std::string();
			}

			std::string utf8(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), size, nullptr, nullptr);

			return utf8;
		}

		/// Hodnota z bloku StringFileInfo pro daný jazyk a kódovou stránku; chybějící hodnota dá prázdný text
		std::string QueryString(const std::vector<BYTE> &data, const wchar_t *block, const wchar_t *key)
		{
			const std::wstring path = std::wstring(L"\\StringFileInfo\\") + block + L"\\" + key;
			void *value = nullptr;
			UINT length = 0;

			if (!VerQueryValueW(data.data(), path.c_str(), &value, &length) || value == nullptr || length == 0) {
				return std::string();
			}

			return ToUtf8(static_cast<const wchar_t *>(value));
		}
#endif
	}

	Info Read()
	{
		Info info;

#ifdef _WIN32
		wchar_t module[MAX_PATH];
		const DWORD moduleLength = GetModuleFileNameW(nullptr, module, MAX_PATH);

		if (moduleLength > 0 && moduleLength < MAX_PATH) {
			DWORD handle = 0;
			const DWORD size = GetFileVersionInfoSizeW(module, &handle);

			if (size > 0) {
				std::vector<BYTE> data(size);

				if (GetFileVersionInfoW(module, 0, size, data.data())) {
					// Jazyk a kódová stránka bloku textů podle tabulky Translation (např. "040504b0")
					struct Translation {
						WORD language;
						WORD codePage;
					};

					Translation *translation = nullptr;
					UINT length = 0;

					if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translation), &length) && translation != nullptr && length >= sizeof(Translation)) {
						wchar_t block[9];
						swprintf_s(block, L"%04x%04x", translation->language, translation->codePage);

						info.name = QueryString(data, block, L"ProductName");
						info.version = QueryString(data, block, L"ProductVersion");
						info.author = QueryString(data, block, L"CompanyName");
					}
				}
			}
		}
#endif

		if (info.name.empty()) {
			info.name = "GlbToMesh";
		}

		return info;
	}

	void PrintBanner()
	{
		const Info info = Read();

		std::cout << info.name << (info.version.empty() ? "" : " " + info.version) << "\n";

		if (!info.author.empty()) {
			std::cout << "Author: " << info.author << "\n";
		}

		std::cout << "\n";
	}
}
