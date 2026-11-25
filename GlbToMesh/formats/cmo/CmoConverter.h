/**
 * @file      CmoConverter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Převod načtené scény glTF na model CMO (geometrie, materiály, kostra, skinning, animace)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <string>
#include <vector>

// Hlavičkové soubory projektu
#include "CmoModel.h"

#include "core/ExportUtil.h"
#include "core/GltfScene.h"
#include "core/OutputFormat.h"

namespace CmoConvert {

	/// Volby převodu: společné volby (výchozí soustava je pravotočivá jako ve Visual Studiu) a volby CMO
	struct Options : ExportUtil::ConvertOptions {
		/// Název pixel shaderu zapsaný do materiálů (výchozí prázdný: DirectXTK použije vestavěný materiál)
		std::string pixelShader;
	};

	/// Počty vytvořených prvků (pro závěrečné shrnutí)
	struct Stats {
		size_t meshes = 0;
		size_t skinnedMeshes = 0;
		size_t subMeshes = 0;
		size_t vertices = 0;
		size_t triangles = 0;
		size_t materials = 0;
		size_t bones = 0;
		size_t clips = 0;
		size_t keys = 0;
	};

	/// Výsledek převodu: model, soubory textur a statistika
	struct Result {
		Cmo::Model model;
		std::vector<Formats::OutputFile> textures;
		Stats stats;
	};

	/**
	 * Převede scénu glTF na model CMO.
	 *
	 * @param const Gltf::Scene& scene Načtená scéna
	 * @param const Options& options Volby převodu
	 *
	 * @return Result Model, soubory textur a statistika
	 *
	 * @throws std::runtime_error Pokud scéna neobsahuje žádnou použitelnou geometrii nebo porušuje limity formátu
	 */
	Result Convert(const Gltf::Scene &scene, const Options &options);
}
