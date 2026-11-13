/**
 * @file      TextUtil.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Pomocné funkce pro texty a cesty: UTF-8, Windows-1250, převod pevných polí znaků
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Názvy v souborech (textury, materiály) mohou být ANSI řetězce nebo UTF-8. glTF vyžaduje JSON v UTF-8 (Blender jinak hlásí
 * "json error: utf-8"), proto se všechny názvy před zápisem do dokumentu převedou: platné UTF-8 zůstane beze změny,
 * cokoliv jiného se bere jako Windows-1250 (výchozí kódování českých Windows).
 */

#pragma once

// Hlavičkové soubory C++
#include <cstddef>
#include <filesystem>
#include <string>

namespace TextUtil {

	/**
	 * Vyzvedne text z pevného pole znaků (pole nemusí být ukončené nulou).
	 *
	 * @param const char* data Začátek pole
	 * @param size_t capacity Velikost pole v bajtech
	 *
	 * @return std::string Text po první nulový bajt (nebo celé pole)
	 */
	std::string FromFixedArray(const char *data, size_t capacity);

	/**
	 * Převede řetězec do UTF-8. Platné UTF-8 (včetně čistého ASCII) vrací beze změny, jiný řetězec celý převede
	 * z Windows-1250.
	 */
	std::string ToUtf8(const std::string &text);

	/// Vytvoří cestu z řetězce (UTF-8 nebo Windows-1250), nezávisle na nastavení systému
	std::filesystem::path MakePath(const std::string &text);

	/// Cesta jako UTF-8 řetězec (pro výpisy do konzole a názvy v glTF)
	std::string PathToUtf8(const std::filesystem::path &path);

	/// Cesta z UTF-8 řetězce
	std::filesystem::path PathFromUtf8(const std::string &utf8);

	/// Převede ASCII znaky řetězce na malá písmena (pro porovnání přípon a názvů souborů)
	std::string ToLowerAscii(std::string text);

	/**
	 * Převede text na UTF-16 (jednotky wchar_t ve Windows, tedy jak je ukládá CMO). Platné UTF-8 se převede přesně, cokoliv jiného
	 * se bere jako Windows-1250 (viz ToUtf8).
	 *
	 * @param const std::string& text Text v UTF-8 (nebo Windows-1250)
	 *
	 * @return std::u16string Text bez ukončující nuly
	 */
	std::u16string ToUtf16(const std::string &text);

	/// Dekóduje procentové kódování z URI v glTF ("a%20b.png" -> "a b.png"); neplatná sekvence zůstane beze změny
	std::string PercentDecode(const std::string &text);

	/**
	 * Z textu vytvoří bezpečný název souboru (bez adresářů a znaků, které Windows v názvech zakazují). Prázdný výsledek nahradí
	 * náhradním názvem.
	 *
	 * @param const std::string& text Libovolný název (UTF-8)
	 * @param const std::string& fallback Název, pokud po úpravě nic nezbude
	 *
	 * @return std::string Název souboru v UTF-8
	 */
	std::string SanitizeFileName(const std::string &text, const std::string &fallback);
}
