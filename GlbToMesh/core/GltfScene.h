/**
 * @file      GltfScene.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Paměťový model načteného souboru glTF/GLB nezávislý na knihovně Microsoft glTF-SDK
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Načítání (GltfLoader.cpp) převede dokument glTF na tyto struktury: akcesory jsou už přečtené do polí floatů, trojúhelníkové
 * pruhy a vějíře rozložené na seznamy trojúhelníků, uzly mají index rodiče a lokální matici. Převodníky do výstupních formátů
 * (adresář formats) knihovnu glTF-SDK vůbec nepotřebují.
 *
 * Matice jsou 4x4 po sloupcích (index = sloupec * 4 + řádek), tedy stejně jako v glTF; viz MathUtil.h.
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Hlavičkové soubory projektu
#include "MathUtil.h"

namespace Gltf {

	/// Odkaz na texturu v materiálu
	struct TextureRef {
		int image = -1;		   // index do Scene::images, -1 = bez textury (nebo ji nelze použít)
		size_t texCoord = 0;   // číslo sady souřadnic UV
		bool hasTransform = false; // KHR_texture_transform
		double offset[2] = {0.0, 0.0};
		double rotation = 0.0;
		double scale[2] = {1.0, 1.0};
	};

	/// Režim průhlednosti materiálu (alphaMode v glTF)
	enum class AlphaMode {
		Opaque,
		Mask,
		Blend
	};

	/// Materiál glTF (metallic-roughness, případně specular-glossiness)
	struct Material {
		std::string name;
		float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		float metallic = 1.0f;
		float roughness = 1.0f;
		float emissive[3] = {0.0f, 0.0f, 0.0f}; // už vynásobeno KHR_materials_emissive_strength
		AlphaMode alphaMode = AlphaMode::Opaque;
		float alphaCutoff = 0.5f;
		bool doubleSided = false;
		bool unlit = false;

		/// KHR_materials_pbrSpecularGlossiness: difuzní barva a textura jsou v baseColor, tady je zbytek
		bool specularGlossiness = false;
		float specular[3] = {1.0f, 1.0f, 1.0f};
		float glossiness = 1.0f;

		TextureRef baseColorTexture;
		TextureRef normalTexture;
		TextureRef emissiveTexture;
		TextureRef metallicRoughnessTexture; // v CMO nemá obdobu, ve verzi 101 SDKMESH také ne (ve verzi 200 je to textura RMA)
		TextureRef occlusionTexture;		 // v CMO nemá obdobu, ve verzi 101 SDKMESH také ne (ve verzi 200 je v textuře RMA)
		TextureRef specularGlossinessTexture;
	};

	/// Obrázek tak, jak je uložen v souboru glTF; převodníky ho zapisují beze změny
	struct Image {
		std::string name;
		std::string extension; // včetně tečky, podle obsahu souboru (".png", ".jpg", ".dds", ...); prázdné = neznámý formát
		std::vector<uint8_t> data;
	};

	/// Jeden primitiv meshe: geometrie rozložená na seznam trojúhelníků
	struct Primitive {
		size_t vertexCount = 0;
		std::vector<float> positions;	// 3 na vrchol
		std::vector<float> normals;		// 3 na vrchol, nebo prázdné
		std::vector<float> tangents;	// 4 na vrchol, nebo prázdné
		std::vector<float> texCoords;	// 2 na vrchol, nebo prázdné (sada 0)
		std::vector<float> colors;		// 4 na vrchol (RGBA 0 - 1), nebo prázdné
		std::vector<uint32_t> joints;	// 4 na vrchol, nebo prázdné
		std::vector<float> weights;		// 4 na vrchol, nebo prázdné
		std::vector<uint32_t> triangles; // seznam trojúhelníků: 3 indexy vrcholů na trojúhelník
		int material = -1;				 // index do Scene::materials, -1 = výchozí materiál glTF
		size_t extraInfluences = 0;		 // počet vrcholů s víc než 4 vlivy kostí (zahozeny nejslabší)
	};

	/// Mesh: pojmenovaný seznam primitiv
	struct Mesh {
		std::string name;
		std::vector<Primitive> primitives;
	};

	/// Uzel hierarchie scény
	struct Node {
		std::string name;
		int parent = -1;
		std::vector<int> children;
		MathUtil::Mat4 local = MathUtil::Identity();
		MathUtil::Vec3 translation;
		MathUtil::Quat rotation;
		MathUtil::Vec3 scale {1.0, 1.0, 1.0};
		int mesh = -1;
		int skin = -1;
	};

	/// Skin: klouby a jejich inverzní bind matice
	struct Skin {
		std::string name;
		std::vector<int> joints;						  // indexy uzlů
		std::vector<MathUtil::Mat4> inverseBind;		  // stejný počet jako joints
	};

	/// Vlastnost uzlu, kterou animační kanál mění
	enum class Path {
		Translation,
		Rotation,
		Scale
	};

	/// Interpolace mezi klíči animačního kanálu
	enum class Interpolation {
		Linear,
		Step,
		CubicSpline
	};

	/// Animační kanál: pro CubicSpline obsahuje values na každý čas trojici (vstupní tečna, hodnota, výstupní tečna)
	struct Channel {
		int node = -1;
		Path path = Path::Translation;
		Interpolation interpolation = Interpolation::Linear;
		std::vector<float> times;
		std::vector<float> values;
	};

	/// Animace: sada kanálů
	struct Animation {
		std::string name;
		std::vector<Channel> channels;
	};

	/// Načtená scéna: vše, co převodníky z glTF potřebují
	struct Scene {
		std::string name;
		std::vector<Image> images;
		std::vector<Material> materials;
		std::vector<Mesh> meshes;
		std::vector<Node> nodes;
		std::vector<Skin> skins;
		std::vector<Animation> animations;
		std::vector<int> roots; // kořenové uzly vybrané scény
		size_t skippedChannels = 0; // kanály, které nejdou převést (váhy morph targetů, chybějící cíl, neplatná data)
		size_t morphTargetPrimitives = 0;
	};
}
