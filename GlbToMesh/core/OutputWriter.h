/**
 * @file      OutputWriter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Zápis souborů, které vrátil výstupní formát
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <filesystem>

// Hlavičkové soubory projektu
#include "OutputFormat.h"

namespace OutputWriter {

	/**
	 * Zapíše na disk všechny soubory výsledku exportu: nejdřív model, pak textury a doprovodné soubory, a vypíše o nich zprávy.
	 * Adresáře se podle potřeby vytvoří. Obsah je celý v paměti, takže se při chybě exportu nevytvoří jen kus výstupu.
	 *
	 * @param const Formats::ExportResult& result Výsledek exportu
	 * @param const std::filesystem::path& output Cesta k hlavnímu souboru
	 * @param const std::filesystem::path& textureDirectory Adresář textur; prázdná cesta = adresář hlavního souboru
	 *
	 * @throws std::runtime_error Pokud se některý soubor nepodaří zapsat
	 */
	void Write(const Formats::ExportResult &result, const std::filesystem::path &output, const std::filesystem::path &textureDirectory);
}
