/**
 * @file      SdkMeshOutputFormat.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Výstupní formát SDKMESH (DXUT, DirectXTK; včetně skeletu a animací v souborech .sdkmesh_anim)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <memory>

// Hlavičkové soubory projektu
#include "core/OutputFormat.h"

namespace SdkMesh {

	/// Vytvoří formát SDKMESH pro registraci v FormatRegistry
	std::unique_ptr<Formats::OutputFormat> CreateOutputFormat();
}
