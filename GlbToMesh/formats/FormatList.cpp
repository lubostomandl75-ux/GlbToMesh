/**
 * @file      FormatList.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Seznam výstupních formátů programu
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Nový formát se přidá tady: #include jeho hlavičky a jeden řádek v RegisterAllFormats (postup viz README, kapitola
 * "Jak přidat další výstupní formát").
 */

// Hlavičkové soubory projektu
#include "core/OutputFormat.h"

// Hlavičkové soubory exportérů
#include "cmo/CmoOutputFormat.h"
#include "sdkmesh/SdkMeshOutputFormat.h"

namespace Formats {

	void RegisterAllFormats(FormatRegistry &registry)
	{
		registry.Register(Cmo::CreateOutputFormat());
		registry.Register(SdkMesh::CreateOutputFormat());
	}
}
