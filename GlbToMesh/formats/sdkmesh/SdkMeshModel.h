/**
 * @file      SdkMeshModel.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Paměťový model vytvářeného souboru SDKMESH a souboru animace SDKANIMATION (záznamy v podobě, jak se zapisují;
 *            viz SdkMeshFormat.h)
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
#include "SdkMeshFormat.h"

namespace SdkMesh {

	/// Vertex buffer: jedna deklarace a pole vrcholů (surové bajty, stride bajtů na vrchol)
	struct VertexBuffer {
		std::vector<D3DVertexElement9> decl; // bez ukončovacího prvku, ten doplní zapisovač
		uint32_t stride = 0;
		uint64_t vertexCount = 0;
		std::vector<uint8_t> data;
	};

	/// Index buffer; zapisuje se jako 16bitový, nebo jako 32bitový (wide)
	struct IndexBuffer {
		bool wide = false;
		std::vector<uint32_t> indices;
	};

	/// Podmnožina meshe s jedním materiálem
	struct Subset {
		std::string name;
		uint32_t materialId = 0;
		uint32_t primitiveType = PT_TRIANGLE_LIST;
		uint64_t indexStart = 0;  // v indexech, ne v bajtech
		uint64_t indexCount = 0;
		uint64_t vertexStart = 0; // základní vrchol (base vertex)
		uint64_t vertexCount = 0;
	};

	/// Mesh: vertex a index buffer, podmnožiny a ovlivňující snímky
	struct Mesh {
		std::string name;
		uint32_t vertexBuffer = 0;
		uint32_t indexBuffer = 0;
		std::vector<uint32_t> subsets;	  // indexy do Model::subsets
		std::vector<uint32_t> influences; // indexy snímků (kostí): vrchol odkazuje na pozici v tomto poli
		Float3 boundingBoxCenter {0.0f, 0.0f, 0.0f};
		Float3 boundingBoxExtents {0.0f, 0.0f, 0.0f};
	};

	/// Snímek hierarchie (uzel scény)
	struct Frame {
		std::string name;
		uint32_t mesh = INVALID_MESH;
		uint32_t parent = INVALID_FRAME;
		uint32_t child = INVALID_FRAME;
		uint32_t sibling = INVALID_FRAME;
		Float4x4 matrix {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	};

	/// Materiál verze 101 (Blinn-Phong) nebo 200 (PBR), podle Model::version
	struct Material {
		std::string name;

		// Verze 101
		std::string diffuseTexture;
		std::string specularTexture;
		Float4 diffuse {0.0f, 0.0f, 0.0f, 0.0f};
		Float4 ambient {0.0f, 0.0f, 0.0f, 0.0f};
		Float4 specular {0.0f, 0.0f, 0.0f, 0.0f};
		Float4 emissive {0.0f, 0.0f, 0.0f, 0.0f};
		float power = 0.0f;

		// Obě verze
		std::string normalTexture;

		// Verze 200
		std::string albedoTexture;
		std::string rmaTexture;
		std::string emissiveTexture;
		float alpha = 1.0f;
	};

	/// Model SDKMESH: buffery, meshe, podmnožiny, snímky a materiály
	struct Model {
		uint32_t version = FILE_VERSION;
		std::vector<VertexBuffer> vertexBuffers;
		std::vector<IndexBuffer> indexBuffers;
		std::vector<Mesh> meshes;
		std::vector<Subset> subsets;
		std::vector<Frame> frames;
		std::vector<Material> materials;
	};

	/// Stopa jednoho snímku v animaci: klíče v pevných časech k / FPS
	struct AnimationTrack {
		std::string frameName;
		std::vector<AnimationKeyRecord> keys;
	};

	/// Jedna animace = jeden soubor .sdkmesh_anim; všechny stopy mají stejný počet klíčů
	struct Animation {
		std::string name;
		uint32_t fps = 30;
		uint32_t keyCount = 0;
		std::vector<AnimationTrack> tracks;
	};
}
