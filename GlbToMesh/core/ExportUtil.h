/**
 * @file      ExportUtil.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Společný základ převodníků scény glTF na formáty Direct3D (CMO, SDKMESH, ...)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Základní třída SceneConverter obsahuje to, co potřebuje každý převodník stejně: světové matice uzlů, zrcadlení osy Z, vzorkování
 * animačních kanálů, přípravu primitiv (doplnění normál, tangent a barev), extrakci textur a jednorázová varování. Převodník
 * formátu z ní jen odvodí třídu a doplní vlastní stavbu modelu.
 */

#pragma once

// Hlavičkové soubory C++
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// Hlavičkové soubory projektu
#include "GltfScene.h"
#include "MathUtil.h"
#include "OutputFormat.h"

namespace ExportUtil {

	/// Lineární složku barvy převede na sRGB (hodnota se omezí na 0 až 1)
	float LinearToSrgb(float c);

	/// Omezí hodnotu na 0 až 1; neplatná hodnota (NaN, nekonečno) dá 0
	float Clamp01(float v);

	/// Barvu RGBA (4 složky 0 - 1) zabalí do 32 bitů: R v nejnižším bajtu, A v nejvyšším
	uint32_t PackColor(const float *rgba);

	/// Trojice čísel z pole atributů vrcholů (index = číslo vrcholu, stride = počet čísel na vrchol)
	inline MathUtil::Vec3 Vec3At(const std::vector<float> &data, size_t index, size_t stride)
	{
		return {data[index * stride], data[index * stride + 1], data[index * stride + 2]};
	}

	/// Osově zarovnaný ohraničující kvádr bodů
	struct Bounds {
		MathUtil::Vec3 min {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
		MathUtil::Vec3 max {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};

		/// Rozšíří kvádr o bod
		void Add(const MathUtil::Vec3 &p)
		{
			this->min = {std::min(this->min.x, p.x), std::min(this->min.y, p.y), std::min(this->min.z, p.z)};
			this->max = {std::max(this->max.x, p.x), std::max(this->max.y, p.y), std::max(this->max.z, p.z)};
		}

		/// Střed kvádru
		MathUtil::Vec3 Center() const
		{
			return {(this->min.x + this->max.x) * 0.5, (this->min.y + this->max.y) * 0.5, (this->min.z + this->max.z) * 0.5};
		}

		/// Poloviční rozměry kvádru (vzdálenost středu od stěn)
		MathUtil::Vec3 HalfExtents() const
		{
			return {(this->max.x - this->min.x) * 0.5, (this->max.y - this->min.y) * 0.5, (this->max.z - this->min.z) * 0.5};
		}
	};

	/// Kanály jedné animace pro jeden uzel
	struct NodeAnimation {
		const Gltf::Channel *translation = nullptr;
		const Gltf::Channel *rotation = nullptr;
		const Gltf::Channel *scale = nullptr;

		/// Ukazatel na kanál pro danou vlastnost uzlu (posun, rotace nebo měřítko)
		const Gltf::Channel *&Slot(Gltf::Path path)
		{
			return path == Gltf::Path::Translation ? this->translation : (path == Gltf::Path::Rotation ? this->rotation : this->scale);
		}
	};

	/// Lokální transformace uzlu: posun, rotace a měřítko
	struct Trs {
		MathUtil::Vec3 t;
		MathUtil::Quat q;
		MathUtil::Vec3 s {1.0, 1.0, 1.0};
	};

	/// Připravená primitiva ve zdrojových souřadnicích (po doplnění normál, tangent a barev)
	struct Prepared {
		size_t vertexCount = 0;
		std::vector<float> positions;
		std::vector<float> normals;
		std::vector<float> tangents;
		std::vector<float> texCoords;
		std::vector<uint32_t> colors; // RGBA, R v nejnižším bajtu
		std::vector<uint32_t> joints;
		std::vector<float> weights;
		std::vector<uint32_t> triangles;
	};

	/**
	 * Základ převodníků scény glTF na model výstupního formátu. Odvozená třída nastaví vlastní volby a v metodě Run postaví model;
	 * soubory textur, které při tom vzniknou, najde v poli textures.
	 */
	class SceneConverter {
	  public:
		/// Virtuální destruktor: odvozené převodníky lze uvolňovat přes ukazatel na základní třídu
		virtual ~SceneConverter() = default;

	  protected:
		/**
		 * @param const Gltf::Scene& scene Načtená scéna (musí přežít převodník)
		 * @param bool leftHanded Zrcadlit osu Z (levotočivá soustava Direct3D)
		 * @param bool noTextures Neexportovat textury
		 * @param const std::string& modelName Základ názvu pro textury bez vlastního názvu
		 */
		SceneConverter(const Gltf::Scene &scene, bool leftHanded, bool noTextures, const std::string &modelName) : scene(scene), leftHanded(leftHanded), noTextures(noTextures), modelName(modelName)
		{
			this->defaultMaterial.name = "Default";
		}

		const Gltf::Scene &scene;
		const bool leftHanded;
		const bool noTextures;
		const std::string modelName;
		Gltf::Material defaultMaterial;			// výchozí materiál glTF pro primitiva bez materiálu

		std::vector<MathUtil::Mat4> world;		// světová matice každého uzlu (viz ComputeWorld)
		std::set<std::string> warned;			// klíče už vypsaných varování
		std::set<std::string> usedFileNames;	// názvy souborů textur malými písmeny
		std::map<int, std::string> textureFiles; // index obrázku -> název souboru ("" = nelze použít)
		std::vector<Formats::OutputFile> textures; // soubory textur, které se mají zapsat

		/// Vypíše varování jen jednou pro daný klíč
		void WarnOnce(const std::string &key, const std::string &message);

		/// Při levotočivém výstupu zrcadlí matici podél osy Z (S * M * S), jinak ji vrátí beze změny
		MathUtil::Mat4 Mirror(const MathUtil::Mat4 &m) const;

		/// Spočítá světové matice všech uzlů scény (world)
		void ComputeWorld();

		/// Uzly, které mění aspoň jeden animační kanál některé animace (index = uzel)
		std::vector<bool> AnimatedNodes() const;

		/// Materiál glTF s daným indexem; záporný index = výchozí materiál
		const Gltf::Material &SourceMaterial(int index) const;

		/**
		 * Zajistí soubor textury (vyextrahuje obrázek z glTF) a vrátí jeho název.
		 *
		 * @param int image Index obrázku ve scéně (záporný = bez textury)
		 * @param const std::string& materialName Název materiálu pro hlášení
		 *
		 * @return std::string Název souboru; prázdný řetězec, pokud se textura nepoužije (bez textur, neznámý formát obrázku)
		 *
		 * @throws std::runtime_error Pokud CheckTextureFileName odmítne název souboru
		 */
		std::string TextureFile(int image, const std::string &materialName);

		/**
		 * Zkontroluje název souboru textury podle omezení formátu (výchozí: bez omezení).
		 *
		 * @param const std::string& fileName Název souboru textury (UTF-8)
		 *
		 * @throws std::runtime_error Pokud název formát nepřijme (např. je příliš dlouhý)
		 */
		virtual void CheckTextureFileName(const std::string &fileName) const;

		/// Hodnota kanálu v čase; mimo rozsah klíčů platí první a poslední hodnota. Výstup: 3 (posun, měřítko) nebo 4 (rotace) čísla
		void SampleChannel(const Gltf::Channel &channel, double time, double *out) const;

		/// Lokální transformace uzlu (posun, rotace, měřítko) v daném čase; kanály, které uzel nemá, nechají hodnoty z klidové pózy
		Trs SampleNode(int node, const std::unordered_map<int, NodeAnimation> &animation, double time) const;

		/// Doplní normály (plošné), tangenty a barvy a převede data do podoby, kterou potřebuje zápis vertexů
		Prepared Prepare(const Gltf::Primitive &src) const;
	};
}
