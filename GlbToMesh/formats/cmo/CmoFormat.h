/**
 * @file      CmoFormat.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Rozložení souboru CMO (Visual Studio Compiled Mesh Object) podle Src/CMO.h a ModelLoadCMO.cpp z DirectXTK
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Soubor je posloupnost záznamů bez zarovnání (little endian). Texty jsou uloženy jako 32bitová délka (počet znaků
 * wchar_t včetně ukončující nuly) následovaná znaky UTF-16. Struktury níže jsou proto "packed" a načítají se přes memcpy.
 *
 * Matice 4x4 jsou řádkové (v' = v * M, posun v poslední řádce). Jejich paměťové rozložení je shodné s maticí glTF
 * (po sloupcích, v' = M * v), takže se při načtení netransponují.
 *
 * Rozložení souboru:
 *   UINT   počet meshů
 *   { pro každý mesh
 *       string název
 *       UINT   počet materiálů
 *       { string název; MaterialRecord; string pixel shader; string textury[8] }
 *       BYTE   1 = mesh obsahuje data skeletu (kosti, animace)
 *       UINT   počet podmeshů;                  SubMeshRecord[]
 *       UINT   počet index bufferů;            { UINT počet; uint16[] indexy }
 *       UINT   počet vertex bufferů;           { UINT počet; VertexRecord[] }
 *       UINT   počet skinning bufferů (0 nebo shodný s vertex buffery); { UINT počet; SkinningVertexRecord[] }
 *       MeshExtents
 *       pokud je skelet:
 *           UINT počet kostí;                   { string název; BoneRecord }
 *           UINT počet animací;                 { string název; ClipRecord; KeyframeRecord[] }
 *   }
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>

namespace Cmo {

	constexpr uint32_t MAX_TEXTURE = 8;
	constexpr uint32_t NUM_BONE_INFLUENCES = 4;

	/// Nejvyšší počet meshů, materiálů, index a vertex bufferů (stejný limit jako v DirectXTK: UINT16_MAX)
	constexpr uint32_t MAX_COUNT = 0xFFFFu;

#pragma pack(push, 1)

	/// Záznam materiálu: barvy, lesk a transformace UV
	struct MaterialRecord {
		float ambient[4];
		float diffuse[4]; // w = průhlednost
		float specular[4];
		float specularPower;
		float emissive[4];
		float uvTransform[16];
	};

	/// Záznam podmeshe: rozsah indexů s jedním materiálem
	struct SubMeshRecord {
		uint32_t materialIndex;
		uint32_t indexBufferIndex;
		uint32_t vertexBufferIndex;
		uint32_t startIndex;
		uint32_t primCount; // počet trojúhelníků
	};

	/// Vrchol: pozice, normála, tangenta, barva a souřadnice UV
	struct VertexRecord {
		float position[3];
		float normal[3];
		float tangent[4];
		uint32_t color; // bajty R, G, B, A
		float texCoord[2];
	};

	/// Skinning vrcholu: indexy kostí a váhy (čtyři vlivy)
	struct SkinningVertexRecord {
		uint32_t boneIndex[NUM_BONE_INFLUENCES];
		float boneWeight[NUM_BONE_INFLUENCES];
	};

	/// Rozměry meshe: střed, poloměr ohraničující koule a meze ohraničujícího kvádru (10 čísel)
	struct MeshExtentsRecord {
		float values[10];
	};

	/// Kost: rodič, inverzní bind matice, bind pozice a lokální transformace
	struct BoneRecord {
		int32_t parentIndex; // záporné = kořen
		float invBindPos[16];
		float bindPos[16];
		float localTransform[16];
	};

	/// Animační klip: časový rozsah a počet klíčů
	struct ClipRecord {
		float startTime;
		float endTime;
		uint32_t keys;
	};

	/// Klíč animace jedné kosti
	struct KeyframeRecord {
		uint32_t boneIndex;
		float time;
		float transform[16]; // lokální transformace kosti (vůči rodiči) v tomto čase
	};

#pragma pack(pop)

	static_assert(sizeof(MaterialRecord) == 132, "CMO Material size incorrect");
	static_assert(sizeof(SubMeshRecord) == 20, "CMO SubMesh size incorrect");
	static_assert(sizeof(VertexRecord) == 52, "CMO Vertex size incorrect");
	static_assert(sizeof(SkinningVertexRecord) == 32, "CMO SkinningVertex size incorrect");
	static_assert(sizeof(MeshExtentsRecord) == 40, "CMO MeshExtents size incorrect");
	static_assert(sizeof(BoneRecord) == 196, "CMO Bone size incorrect");
	static_assert(sizeof(ClipRecord) == 12, "CMO Clip size incorrect");
	static_assert(sizeof(KeyframeRecord) == 72, "CMO Keyframe size incorrect");
}
