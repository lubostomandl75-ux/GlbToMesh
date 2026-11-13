/**
 * @file      CmoModel.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Paměťový model vytvářeného souboru CMO (záznamy přesně v podobě, jak se zapisují; viz CmoFormat.h)
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
#include "CmoFormat.h"

namespace Cmo {

	/// Materiál meshe: název, pixel shader, záznam a názvy souborů textur
	struct Material {
		std::string name;
		std::string pixelShader;
		MaterialRecord record {};
		std::string textures[MAX_TEXTURE];
	};

	/// Kost s názvem
	struct Bone {
		std::string name;
		BoneRecord record {};
	};

	/// Animační klip s názvem a klíči
	struct Clip {
		std::string name;
		ClipRecord record {};
		std::vector<KeyframeRecord> keys;
	};

	/// Mesh: materiály, podmeshe, index a vertex buffery, kostra a klipy
	struct Mesh {
		std::string name;
		std::vector<Material> materials;
		std::vector<SubMeshRecord> subMeshes;
		std::vector<std::vector<uint16_t>> indexBuffers;
		std::vector<std::vector<VertexRecord>> vertexBuffers;
		std::vector<std::vector<SkinningVertexRecord>> skinBuffers; // prázdné (bez skinningu) nebo jeden pro každý vertex buffer
		MeshExtentsRecord extents {};
		bool hasSkeleton = false;
		std::vector<Bone> bones;
		std::vector<Clip> clips;
	};

	/// Model CMO: posloupnost meshů
	struct Model {
		std::vector<Mesh> meshes;
	};
}
