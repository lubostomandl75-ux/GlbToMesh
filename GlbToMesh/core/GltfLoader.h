/**
 * @file      GltfLoader.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Načtení souboru glTF/GLB do paměťového modelu scény (viz GltfScene.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <filesystem>
#include <string>

// Hlavičkové soubory projektu
#include "GltfScene.h"

namespace Gltf {

	/**
	 * Načte soubor .glb nebo .gltf (včetně externích dat a vložených dat URI).
	 *
	 * @param const std::filesystem::path& path Cesta k souboru
	 * @param const std::string& targetFormat Název výstupního formátu (např. "CMO"); objeví se v hlášení o povinných rozšířeních
	 *                                        glTF, která formát nedokáže vyjádřit (a model se přesto převede)
	 *
	 * @return Scene Načtená scéna
	 *
	 * @throws std::runtime_error Pokud je soubor poškozený nebo nepodporovaný
	 */
	Scene Load(const std::filesystem::path &path, const std::string &targetFormat);
}
