/**
 * @file      CmoWriter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Zápis modelu do formátu CMO (viz CmoFormat.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <vector>

// Hlavičkové soubory projektu
#include "CmoModel.h"

namespace CmoWriter {

	/**
	 * Sestaví obsah souboru CMO. Kontroluje limity, které ověřuje i Model::CreateFromCMO v DirectXTK (nejvýš 65535 meshů,
	 * materiálů, index a vertex bufferů, neprázdné buffery, odkazy podmeshů v rozsahu), aby se neukládal soubor, který by
	 * DirectXTK odmítlo.
	 *
	 * @param const Cmo::Model& model Hotový model
	 *
	 * @return std::vector<uint8_t> Obsah souboru
	 *
	 * @throws std::runtime_error Pokud model nesplňuje limity formátu
	 */
	std::vector<uint8_t> Serialize(const Cmo::Model &model);
}
