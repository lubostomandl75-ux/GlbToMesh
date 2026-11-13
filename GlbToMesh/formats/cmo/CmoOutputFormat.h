/**
 * @file      CmoOutputFormat.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Výstupní formát CMO (Visual Studio Compiled Mesh Object, včetně skeletu a animací)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <memory>

// Hlavičkové soubory projektu
#include "core/OutputFormat.h"

namespace Cmo {

	/// Vytvoří formát CMO pro registraci v FormatRegistry
	std::unique_ptr<Formats::OutputFormat> CreateOutputFormat();
}
