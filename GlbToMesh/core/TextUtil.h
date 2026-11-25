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
#include <set>
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

	/// Zkrátí UTF-8 text na nejvýš maxBytes bajtů, aniž by přerušil znak
	std::string TruncateUtf8(const std::string &text, size_t maxBytes);

	/**
	 * Vytvoří název, který ještě není v množině použitých, a přidá ho do ní. Obsazený název dostane před příponou
	 * pořadové číslo: "stem.ext", "stem_2.ext", "stem_3.ext", ...
	 *
	 * @param std::set<std::string>& used Použité názvy (při ignoreCase uložené malými písmeny)
	 * @param const std::string& stem Požadovaný název bez přípony (UTF-8)
	 * @param const std::string& extension Přípona připojená za název a pořadové číslo (např. ".png"); může být prázdná
	 * @param size_t maxBytes Nejvyšší délka názvu bez přípony v bajtech, včetně pořadového čísla (npos = bez omezení)
	 * @param bool ignoreCase Porovnávat bez ohledu na velikost ASCII písmen
	 *
	 * @return std::string Jedinečný název včetně přípony
	 */
	std::string UniqueName(std::set<std::string> &used, const std::string &stem, const std::string &extension = std::string(), size_t maxBytes = std::string::npos, bool ignoreCase = true);
}
