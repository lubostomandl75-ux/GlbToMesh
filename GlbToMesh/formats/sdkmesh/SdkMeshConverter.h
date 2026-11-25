/**
 * @file      SdkMeshConverter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Převod načtené scény glTF na model SDKMESH (geometrie, materiály, hierarchie snímků, skinning) a animace SDKANIMATION
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
#include "SdkMeshModel.h"

#include "core/ExportUtil.h"
#include "core/GltfScene.h"
#include "core/OutputFormat.h"

namespace SdkMeshConvert {

	/// Volby převodu: společné volby (výchozí soustava je pravotočivá jako v glTF a v DirectXTK) a volby SDKMESH
	struct Options : ExportUtil::ConvertOptions {
		/// Zapsat soubor SDKMESH verze 200 s PBR materiály (albedo, normála, RMA, emisivní textura) místo verze 101 (Blinn-Phong)
		bool pbr = false;

		/**
		 * Zapéct transformace uzlů do vrcholů neskinovaných meshů: vrcholy se zapíšou ve světových souřadnicích a mesh visí na snímku
		 * s jednotkovou maticí (jako skinované meshe). Hodí se pro prohlížeče, které matice snímků nepoužijí (DirectXTK bez kostí).
		 * Meshe pod animovanými uzly se nezapékají, protože by se při přehrávání animace nehýbaly.
		 */
		bool bakeTransforms = false;

		/// Počet klíčů za sekundu v souborech .sdkmesh_anim (animace glTF se vzorkují v pevných časech)
		uint32_t animationFps = 30;
	};

	/// Počty vytvořených prvků (pro závěrečné shrnutí)
	struct Stats {
		size_t frames = 0;
		size_t meshes = 0;
		size_t skinnedMeshes = 0;
		size_t bakedMeshes = 0; // meshe se zapečenou světovou transformací (volba bakeTransforms)
		size_t vertexBuffers = 0;
		size_t subsets = 0;
		size_t vertices = 0;
		size_t triangles = 0;
		size_t materials = 0;
		size_t animations = 0;
		size_t keys = 0;
	};

	/// Výsledek převodu: model, soubory textur, animace a statistika
	struct Result {
		SdkMesh::Model model;
		std::vector<Formats::OutputFile> textures;
		std::vector<SdkMesh::Animation> animations;
		Stats stats;
	};

	/**
	 * Převede scénu glTF na model SDKMESH.
	 *
	 * @param const Gltf::Scene& scene Načtená scéna
	 * @param const Options& options Volby převodu
	 *
	 * @return Result Model, soubory textur, animace a statistika
	 *
	 * @throws std::runtime_error Pokud scéna neobsahuje žádnou použitelnou geometrii nebo porušuje limity formátu
	 */
	Result Convert(const Gltf::Scene &scene, const Options &options);
}
