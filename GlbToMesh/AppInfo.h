/**
 * @file      AppInfo.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Údaje o programu (název, autor, verze) z prostředku VERSIONINFO v exe a jejich výpis v hlavičce
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <string>

namespace AppInfo {

	/// Údaje o programu; chybějící hodnota prostředku zůstane prázdná (název má náhradní hodnotu "GlbToMesh")
	struct Info {
		std::string name;	 // ProductName
		std::string version; // ProductVersion
		std::string author;	 // CompanyName
	};

	/// Přečte údaje z prostředku VERSIONINFO běžícího exe (mimo Windows jen náhradní název)
	Info Read();

	/// Vypíše hlavičku programu (název a verze, autor) a prázdný řádek na standardní výstup
	void PrintBanner();
}
