/**
 * @file      Log.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Velmi jednoduchý výpis zpráv do konzole s počítadlem varování
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <iostream>
#include <string>

namespace Log {

	/// Počet varování od spuštění programu (vypíše se v závěrečném shrnutí)
	inline size_t &WarningCount()
	{
		static size_t count = 0;

		return count;
	}

	/// Zapne podrobné výpisy (přepínač --verbose)
	inline bool &Verbose()
	{
		static bool verbose = false;

		return verbose;
	}

	/// Informace pro uživatele
	inline void Info(const std::string &message)
	{
		std::cout << message << std::endl;
	}

	/// Podrobnost, vypisuje se jen s přepínačem --verbose
	inline void Detail(const std::string &message)
	{
		if (Verbose()) {
			std::cout << "  " << message << std::endl;
		}
	}

	/// Varování: výstup vznikne, ale nemusí odpovídat zdroji přesně
	inline void Warn(const std::string &message)
	{
		++WarningCount();
		std::cerr << "Warning: " << message << std::endl;
	}
}
