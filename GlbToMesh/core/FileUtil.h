/**
 * @file      FileUtil.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Zápis souborů z paměti na disk
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <filesystem>
#include <vector>

namespace FileUtil {

	/**
	 * Zapíše celý soubor (existující přepíše).
	 *
	 * @param const std::filesystem::path& path Cesta k souboru (adresář musí existovat)
	 * @param const std::vector<uint8_t>& bytes Obsah souboru
	 *
	 * @throws std::runtime_error Pokud soubor nelze vytvořit nebo zapsat
	 */
	void WriteWholeFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes);
}
