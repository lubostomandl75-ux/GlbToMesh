/**
 * @file      GltfLoader.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Načtení souborů glTF/GLB pomocí Microsoft glTF-SDK do paměťového modelu (viz GltfScene.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Čtení akcesorů přes SDK (ReadBinaryData) vyžaduje přesný typ složky, proto se každý akcesor přečte ve své skutečné podobě
 * a převede na float (včetně příznaku normalized). Tím se zvládnou i kvantizované atributy (KHR_mesh_quantization). Soubor
 * glTF smí odkazovat na další soubory jen relativními cestami uvnitř svého adresáře, ne na absolutní cesty ani na ".." (přečtený
 * obsah by se jinak mohl dostat do výstupních souborů textur).
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

// Hlavičkové soubory knihovny Microsoft glTF-SDK
#include <glTF-SDK/Inc/Deserialize.h>
#include <glTF-SDK/Inc/Document.h>
#include <glTF-SDK/Inc/ExtensionsKHR.h>
#include <glTF-SDK/Inc/GLBResourceReader.h>
#include <glTF-SDK/Inc/GLTF.h>
#include <glTF-SDK/Inc/GLTFResourceReader.h>
#include <glTF-SDK/Inc/IStreamReader.h>
#include <glTF-SDK/RapidJson/document.h>

// Hlavičkové soubory projektu
#include "GltfLoader.h"
#include "Log.h"
#include "TextUtil.h"

namespace Gltf {

	namespace gltf = Microsoft::glTF;
	namespace fs = std::filesystem;

	namespace {

		/// Nejvyšší počet prvků jednoho akcesoru (chrání před paměťovou bombou v poškozených souborech)
		constexpr size_t MAX_ACCESSOR_COUNT = size_t(1) << 27;

		/// Čtení souborů vedle dokumentu; nepovoluje cesty mimo adresář dokumentu
		class FileStreamReader : public gltf::IStreamReader
		{
		  public:
			/// @param directory Adresář dokumentu glTF, vůči kterému se řeší relativní URI souborů
			explicit FileStreamReader(fs::path directory) : directory(std::move(directory))
			{
			}

			/// Otevře soubor, na který odkazuje URI (po dekódování procent); absolutní cesty a cesty s ".." odmítne
			std::shared_ptr<std::istream> GetInputStream(const std::string &uri) const override
			{
				const fs::path relative = TextUtil::PathFromUtf8(TextUtil::PercentDecode(uri));

				if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
					throw std::runtime_error("The file refers to the file '" + uri + "' by an absolute or empty path, which is not allowed");
				}

				for (const fs::path &part : relative) {

					if (part == "..") {
						throw std::runtime_error("The file refers to '" + uri + "' outside of its own directory, which is not allowed");
					}
				}

				auto stream = std::make_shared<std::ifstream>(this->directory / relative, std::ios::binary);

				if (!stream || !(*stream)) {
					throw std::runtime_error("Unable to open the referenced file: " + uri);
				}

				return stream;
			}

		  private:
			fs::path directory;
		};

		/// Počet složek jednoho prvku akcesoru (SCALAR = 1, VEC3 = 3, MAT4 = 16, ...)
		size_t ComponentCount(gltf::AccessorType type)
		{
			switch (type) {
				case gltf::TYPE_SCALAR:
					return 1;
				case gltf::TYPE_VEC2:
					return 2;
				case gltf::TYPE_VEC3:
					return 3;
				case gltf::TYPE_VEC4:
				case gltf::TYPE_MAT2:
					return 4;
				case gltf::TYPE_MAT3:
					return 9;
				case gltf::TYPE_MAT4:
					return 16;
				default:
					throw std::runtime_error("Unknown accessor type");
			}
		}

		/// Přečte akcesor, hodnoty převede na float (normalized celá čísla do rozsahu 0 - 1 nebo -1 - 1)
		template <typename T> std::vector<float> ReadConverted(const gltf::Document &doc, const gltf::GLTFResourceReader &reader, const gltf::Accessor &accessor, double divisor, bool isSigned)
		{
			const std::vector<T> raw = reader.ReadBinaryData<T>(doc, accessor);
			std::vector<float> result(raw.size());

			for (size_t i = 0; i < raw.size(); ++i) {
				double value = static_cast<double>(raw[i]);

				if (accessor.normalized) {
					value = isSigned ? std::max(value / divisor, -1.0) : value / divisor;
				}

				result[i] = static_cast<float>(value);
			}

			return result;
		}

		/// Převod dokumentu glTF na paměťový model scény; metody Load* naplňují jednotlivé části Scene
		class Loader
		{
		  public:
			/// @param doc Načtený dokument glTF
			/// @param reader Čtečka dat dokumentu (vyrovnávací paměti, obrázky)
			/// @param targetFormat Název výstupního formátu pro hlášení (musí přežít načítání)
			Loader(const gltf::Document &doc, const gltf::GLTFResourceReader &reader, const std::string &targetFormat) : doc(doc), reader(reader), targetFormat(targetFormat)
			{
			}

			/// Provede načtení: zkontroluje rozšíření a načte materiály, meshe, uzly, skiny a animace
			Scene Run();

		  private:
			const gltf::Document &doc;
			const gltf::GLTFResourceReader &reader;
			const std::string &targetFormat; // název výstupního formátu pro hlášení o rozšířeních, která neumí vyjádřit
			Scene scene;
			std::map<size_t, int> imageIndex; // index obrázku v dokumentu -> index v Scene::images (-1 = nelze načíst)

			/// Přečte akcesor do pole floatů; počet složek musí odpovídat očekávání (what pojmenovává atribut v chybových hlášeních)
			std::vector<float> ReadFloats(const std::string &accessorId, size_t expectedComponents, const char *what) const;
			/// Přečte akcesor s indexy do pole 32bitových indexů
			std::vector<uint32_t> ReadIndices(const std::string &accessorId) const;
			/// Načte obrázek a vrátí jeho index v Scene::images; -1 = obrázek nelze použít
			int LoadImage(const std::string &imageId);
			/// Vytvoří odkaz na texturu materiálu (obrázek, sada UV, KHR_texture_transform); slot slouží jen k hlášení
			TextureRef MakeTextureRef(const gltf::TextureInfo &info, const std::string &materialName, const char *slot);
			/// Načte materiály (metallic-roughness, specular-glossiness, unlit, síla emise)
			void LoadMaterials();
			/// Načte meshe: každý primitiv se rozloží na seznam trojúhelníků
			void LoadMeshes();
			/// Načte jeden primitiv; vrací false, pokud ho nelze použít (nejde o trojúhelníky, chybí pozice nebo je prázdný)
			bool LoadPrimitive(const gltf::MeshPrimitive &src, const std::string &meshName, size_t primitiveIndex, Primitive &out);
			/// Načte uzly: rodiče, lokální matice, odkazy na mesh a skin a kořeny scény
			void LoadNodes();
			/// Načte skiny: klouby a inverzní bind matice
			void LoadSkins();
			/// Načte animace: kanály posunu, rotace a měřítka (kanály vah morph targets se přeskočí)
			void LoadAnimations();
			/// Zkontroluje povinná rozšíření glTF: ta, která mění jen vzhled, ohlásí varováním, ostatní nepodporovaná odmítne výjimkou
			void CheckExtensions() const;
		};

		std::vector<float> Loader::ReadFloats(const std::string &accessorId, size_t expectedComponents, const char *what) const
		{
			const gltf::Accessor &accessor = this->doc.accessors.Get(accessorId);

			if (ComponentCount(accessor.type) != expectedComponents) {
				throw std::runtime_error(std::string(what) + ": the accessor has " + std::to_string(ComponentCount(accessor.type)) + " components, " + std::to_string(expectedComponents) + " expected");
			}

			if (accessor.count > MAX_ACCESSOR_COUNT) {
				throw std::runtime_error(std::string(what) + ": the accessor has too many elements (" + std::to_string(accessor.count) + ")");
			}

			std::vector<float> values;

			switch (accessor.componentType) {
				case gltf::COMPONENT_FLOAT:
					values = this->reader.ReadBinaryData<float>(this->doc, accessor);
					break;
				case gltf::COMPONENT_BYTE:
					values = ReadConverted<int8_t>(this->doc, this->reader, accessor, 127.0, true);
					break;
				case gltf::COMPONENT_UNSIGNED_BYTE:
					values = ReadConverted<uint8_t>(this->doc, this->reader, accessor, 255.0, false);
					break;
				case gltf::COMPONENT_SHORT:
					values = ReadConverted<int16_t>(this->doc, this->reader, accessor, 32767.0, true);
					break;
				case gltf::COMPONENT_UNSIGNED_SHORT:
					values = ReadConverted<uint16_t>(this->doc, this->reader, accessor, 65535.0, false);
					break;
				case gltf::COMPONENT_UNSIGNED_INT:
					values = ReadConverted<uint32_t>(this->doc, this->reader, accessor, 4294967295.0, false);
					break;
				default:
					throw std::runtime_error(std::string(what) + ": unsupported accessor component type");
			}

			if (values.size() != accessor.count * expectedComponents) {
				throw std::runtime_error(std::string(what) + ": the accessor data have an unexpected size");
			}

			for (const float v : values) {

				if (!std::isfinite(v)) {
					throw std::runtime_error(std::string(what) + ": the accessor contains a value that is not finite");
				}
			}

			return values;
		}

		std::vector<uint32_t> Loader::ReadIndices(const std::string &accessorId) const
		{
			const gltf::Accessor &accessor = this->doc.accessors.Get(accessorId);

			if (accessor.type != gltf::TYPE_SCALAR) {
				throw std::runtime_error("Indices: the accessor is not a scalar");
			}

			if (accessor.count > MAX_ACCESSOR_COUNT) {
				throw std::runtime_error("Indices: the accessor has too many elements (" + std::to_string(accessor.count) + ")");
			}

			switch (accessor.componentType) {
				case gltf::COMPONENT_UNSIGNED_BYTE: {
					const auto raw = this->reader.ReadBinaryData<uint8_t>(this->doc, accessor);

					return std::vector<uint32_t>(raw.begin(), raw.end());
				}
				case gltf::COMPONENT_UNSIGNED_SHORT: {
					const auto raw = this->reader.ReadBinaryData<uint16_t>(this->doc, accessor);

					return std::vector<uint32_t>(raw.begin(), raw.end());
				}
				case gltf::COMPONENT_UNSIGNED_INT:
					return this->reader.ReadBinaryData<uint32_t>(this->doc, accessor);
				default:
					throw std::runtime_error("Indices: unsupported component type (unsigned byte, short or int expected)");
			}
		}

		// -----------------------------------------------------------------------------------------------------------
		// Textury a materiály

		/// Přípona souboru podle obsahu (podpis souboru), jinak podle typu MIME nebo přípony v URI
		std::string DetectExtension(const std::vector<uint8_t> &data, const std::string &mime, const std::string &uri)
		{
			const auto starts = [&data](const char *magic, size_t length, size_t offset = 0) {
				return data.size() >= offset + length && std::equal(magic, magic + length, reinterpret_cast<const char *>(data.data()) + offset);
			};

			if (starts("\x89PNG\r\n\x1a\n", 8)) {
				return ".png";
			}

			if (starts("\xFF\xD8\xFF", 3)) {
				return ".jpg";
			}

			if (starts("DDS ", 4)) {
				return ".dds";
			}

			if (starts("RIFF", 4) && starts("WEBP", 4, 8)) {
				return ".webp";
			}

			if (starts("\xABKTX 20\xBB", 8)) {
				return ".ktx2";
			}

			if (starts("BM", 2)) {
				return ".bmp";
			}

			const std::string lowerMime = TextUtil::ToLowerAscii(mime);

			if (lowerMime == "image/png") {
				return ".png";
			}

			if (lowerMime == "image/jpeg" || lowerMime == "image/jpg") {
				return ".jpg";
			}

			if (lowerMime == "image/webp") {
				return ".webp";
			}

			if (lowerMime == "image/ktx2") {
				return ".ktx2";
			}

			if (!uri.empty() && uri.compare(0, 5, "data:") != 0) {
				return TextUtil::ToLowerAscii(TextUtil::PathToUtf8(TextUtil::PathFromUtf8(TextUtil::PercentDecode(uri)).extension()));
			}

			return std::string();
		}

		int Loader::LoadImage(const std::string &imageId)
		{
			if (imageId.empty()) {
				return -1;
			}

			const size_t docIndex = this->doc.images.GetIndex(imageId);
			const auto cached = this->imageIndex.find(docIndex);

			if (cached != this->imageIndex.end()) {
				return cached->second;
			}

			const gltf::Image &src = this->doc.images.Get(docIndex);
			Image image;

			try {
				image.data = this->reader.ReadBinaryData(this->doc, src);
			}
			catch (const std::exception &ex) {
				Log::Warn("Image '" + (src.name.empty() ? imageId : src.name) + "' cannot be read (" + ex.what() + ") - materials using it are exported without the texture.");
				this->imageIndex[docIndex] = -1;

				return -1;
			}

			if (image.data.empty()) {
				Log::Warn("Image '" + (src.name.empty() ? imageId : src.name) + "' is empty - materials using it are exported without the texture.");
				this->imageIndex[docIndex] = -1;

				return -1;
			}

			image.extension = DetectExtension(image.data, src.mimeType, src.uri);

			if (!src.name.empty()) {
				image.name = src.name;
			}
			else if (!src.uri.empty() && src.uri.compare(0, 5, "data:") != 0) {
				image.name = TextUtil::PathToUtf8(TextUtil::PathFromUtf8(TextUtil::PercentDecode(src.uri)).stem());
			}

			this->scene.images.push_back(std::move(image));
			const int index = static_cast<int>(this->scene.images.size() - 1);
			this->imageIndex[docIndex] = index;

			return index;
		}

		TextureRef Loader::MakeTextureRef(const gltf::TextureInfo &info, const std::string &materialName, const char *slot)
		{
			TextureRef ref;

			if (info.textureId.empty()) {
				return ref;
			}

			const gltf::Texture &texture = this->doc.textures.Get(info.textureId);
			ref.texCoord = info.texCoord;

			if (info.HasExtension<gltf::KHR::TextureInfos::TextureTransform>()) {
				const auto &tt = info.GetExtension<gltf::KHR::TextureInfos::TextureTransform>();
				ref.hasTransform = true;
				ref.offset[0] = tt.offset.x;
				ref.offset[1] = tt.offset.y;
				ref.rotation = tt.rotation;
				ref.scale[0] = tt.scale.x;
				ref.scale[1] = tt.scale.y;

				if (tt.texCoord) {
					ref.texCoord = tt.texCoord.Get();
				}
			}

			if (texture.imageId.empty()) {
				Log::Warn("Material '" + materialName + "': the " + slot + " texture has no image (an unsupported texture extension?) - exported without it.");

				return ref;
			}

			ref.image = this->LoadImage(texture.imageId);

			return ref;
		}

		void Loader::LoadMaterials()
		{
			for (const gltf::Material &src : this->doc.materials.Elements()) {
				Material m;
				m.name = src.name;

				const auto &pbr = src.metallicRoughness;
				m.baseColor[0] = pbr.baseColorFactor.r;
				m.baseColor[1] = pbr.baseColorFactor.g;
				m.baseColor[2] = pbr.baseColorFactor.b;
				m.baseColor[3] = pbr.baseColorFactor.a;
				m.metallic = pbr.metallicFactor;
				m.roughness = pbr.roughnessFactor;
				m.emissive[0] = src.emissiveFactor.r;
				m.emissive[1] = src.emissiveFactor.g;
				m.emissive[2] = src.emissiveFactor.b;
				m.alphaMode = src.alphaMode == gltf::ALPHA_BLEND ? AlphaMode::Blend : (src.alphaMode == gltf::ALPHA_MASK ? AlphaMode::Mask : AlphaMode::Opaque);
				m.alphaCutoff = src.alphaCutoff;
				m.doubleSided = src.doubleSided;
				m.unlit = src.HasExtension<gltf::KHR::Materials::Unlit>();

				const auto strength = src.extensions.find("KHR_materials_emissive_strength");

				if (strength != src.extensions.end()) {
					rapidjson::Document json;
					json.Parse(strength->second.c_str());

					if (!json.HasParseError() && json.IsObject() && json.HasMember("emissiveStrength") && json["emissiveStrength"].IsNumber()) {
						const double factor = json["emissiveStrength"].GetDouble();

						if (std::isfinite(factor) && factor >= 0.0) {

							for (float &c : m.emissive) {
								c = static_cast<float>(c * factor);
							}
						}
					}
				}

				m.baseColorTexture = this->MakeTextureRef(pbr.baseColorTexture, m.name, "base color");
				m.normalTexture = this->MakeTextureRef(src.normalTexture, m.name, "normal");
				m.emissiveTexture = this->MakeTextureRef(src.emissiveTexture, m.name, "emissive");
				m.metallicRoughnessTexture = this->MakeTextureRef(pbr.metallicRoughnessTexture, m.name, "metallic-roughness");
				m.occlusionTexture = this->MakeTextureRef(src.occlusionTexture, m.name, "occlusion");

				if (src.HasExtension<gltf::KHR::Materials::PBRSpecularGlossiness>()) {
					const auto &sg = src.GetExtension<gltf::KHR::Materials::PBRSpecularGlossiness>();
					m.specularGlossiness = true;
					m.baseColor[0] = sg.diffuseFactor.r;
					m.baseColor[1] = sg.diffuseFactor.g;
					m.baseColor[2] = sg.diffuseFactor.b;
					m.baseColor[3] = sg.diffuseFactor.a;
					m.specular[0] = sg.specularFactor.r;
					m.specular[1] = sg.specularFactor.g;
					m.specular[2] = sg.specularFactor.b;
					m.glossiness = sg.glossinessFactor;
					m.baseColorTexture = this->MakeTextureRef(sg.diffuseTexture, m.name, "diffuse");
					m.specularGlossinessTexture = this->MakeTextureRef(sg.specularGlossinessTexture, m.name, "specular-glossiness");
				}

				this->scene.materials.push_back(std::move(m));
			}
		}

		// -----------------------------------------------------------------------------------------------------------
		// Geometrie

		bool Loader::LoadPrimitive(const gltf::MeshPrimitive &src, const std::string &meshName, size_t primitiveIndex, Primitive &out)
		{
			const std::string label = "Mesh '" + meshName + "' primitive " + std::to_string(primitiveIndex);

			if (src.mode != gltf::MESH_TRIANGLES && src.mode != gltf::MESH_TRIANGLE_STRIP && src.mode != gltf::MESH_TRIANGLE_FAN) {
				Log::Warn(label + " is not made of triangles (points or lines) - skipped.");

				return false;
			}

			if (!src.HasAttribute(gltf::ACCESSOR_POSITION)) {
				Log::Warn(label + " has no positions - skipped.");

				return false;
			}

			if (!src.targets.empty()) {
				++this->scene.morphTargetPrimitives;
			}

			out.positions = this->ReadFloats(src.GetAttributeAccessorId(gltf::ACCESSOR_POSITION), 3, "POSITION");
			out.vertexCount = out.positions.size() / 3;

			if (out.vertexCount == 0) {
				return false;
			}

			const auto optional = [&](const char *name, size_t components, std::vector<float> &target) {
				if (!src.HasAttribute(name)) {
					return;
				}

				target = this->ReadFloats(src.GetAttributeAccessorId(name), components, name);

				if (target.size() != out.vertexCount * components) {
					throw std::runtime_error(label + ": the attribute " + name + " has a different number of vertices than POSITION");
				}
			};

			optional(gltf::ACCESSOR_NORMAL, 3, out.normals);
			optional(gltf::ACCESSOR_TANGENT, 4, out.tangents);
			optional(gltf::ACCESSOR_TEXCOORD_0, 2, out.texCoords);

			// Barvy: vec3 nebo vec4
			if (src.HasAttribute(gltf::ACCESSOR_COLOR_0)) {
				const std::string &id = src.GetAttributeAccessorId(gltf::ACCESSOR_COLOR_0);
				const bool rgb = this->doc.accessors.Get(id).type == gltf::TYPE_VEC3;
				const std::vector<float> raw = this->ReadFloats(id, rgb ? 3 : 4, gltf::ACCESSOR_COLOR_0);

				if (raw.size() != out.vertexCount * (rgb ? 3 : 4)) {
					throw std::runtime_error(label + ": the attribute COLOR_0 has a different number of vertices than POSITION");
				}

				out.colors.resize(out.vertexCount * 4);

				for (size_t v = 0; v < out.vertexCount; ++v) {

					for (size_t c = 0; c < 4; ++c) {
						out.colors[v * 4 + c] = (c < 3 || !rgb) ? raw[v * (rgb ? 3 : 4) + c] : 1.0f;
					}
				}
			}

			// Kosti: až dvě sady po čtyřech vlivech, zůstane čtyři nejsilnější
			if (src.HasAttribute(gltf::ACCESSOR_JOINTS_0) && src.HasAttribute(gltf::ACCESSOR_WEIGHTS_0)) {
				std::vector<float> joints[2], weights[2];
				optional(gltf::ACCESSOR_JOINTS_0, 4, joints[0]);
				optional(gltf::ACCESSOR_WEIGHTS_0, 4, weights[0]);

				if (src.HasAttribute("JOINTS_1") && src.HasAttribute("WEIGHTS_1")) {
					optional("JOINTS_1", 4, joints[1]);
					optional("WEIGHTS_1", 4, weights[1]);
				}

				out.joints.assign(out.vertexCount * 4, 0);
				out.weights.assign(out.vertexCount * 4, 0.0f);

				for (size_t v = 0; v < out.vertexCount; ++v) {
					std::pair<float, uint32_t> influences[8];
					size_t count = 0;

					for (size_t set = 0; set < 2; ++set) {

						if (joints[set].empty()) {
							continue;
						}

						for (size_t c = 0; c < 4; ++c) {
							const float weight = weights[set][v * 4 + c];

							if (weight > 0.0f) {
								influences[count++] = {weight, static_cast<uint32_t>(joints[set][v * 4 + c])};
							}
						}
					}

					std::stable_sort(influences, influences + count, [](const auto &a, const auto &b) { return a.first > b.first; });

					if (count > 4) {
						++out.extraInfluences;
						count = 4;
					}

					float sum = 0.0f;

					for (size_t c = 0; c < count; ++c) {
						sum += influences[c].first;
					}

					if (count == 0 || !(sum > 0.0f)) {
						out.weights[v * 4] = 1.0f; // vrchol bez vlivu: pevně na první kost
						continue;
					}

					for (size_t c = 0; c < count; ++c) {
						out.joints[v * 4 + c] = influences[c].second;
						out.weights[v * 4 + c] = influences[c].first / sum;
					}
				}
			}

			// Trojúhelníky
			std::vector<uint32_t> indices;

			if (!src.indicesAccessorId.empty()) {
				indices = this->ReadIndices(src.indicesAccessorId);
			}
			else {
				indices.resize(out.vertexCount);

				for (size_t i = 0; i < indices.size(); ++i) {
					indices[i] = static_cast<uint32_t>(i);
				}
			}

			for (const uint32_t index : indices) {

				if (index >= out.vertexCount) {
					throw std::runtime_error(label + " refers to the vertex " + std::to_string(index) + " but has only " + std::to_string(out.vertexCount));
				}
			}

			const auto addTriangle = [&out](uint32_t a, uint32_t b, uint32_t c) {
				if (a != b && b != c && a != c) {
					out.triangles.push_back(a);
					out.triangles.push_back(b);
					out.triangles.push_back(c);
				}
			};

			if (src.mode == gltf::MESH_TRIANGLES) {

				for (size_t i = 0; i + 2 < indices.size(); i += 3) {
					addTriangle(indices[i], indices[i + 1], indices[i + 2]);
				}
			}
			else if (src.mode == gltf::MESH_TRIANGLE_STRIP) {

				for (size_t i = 0; i + 2 < indices.size(); ++i) {

					// Každý druhý trojúhelník pruhu má opačné pořadí, aby zůstalo vinutí
					if ((i & 1) == 0) {
						addTriangle(indices[i], indices[i + 1], indices[i + 2]);
					}
					else {
						addTriangle(indices[i + 1], indices[i], indices[i + 2]);
					}
				}
			}
			else {

				for (size_t i = 1; i + 1 < indices.size(); ++i) {
					addTriangle(indices[0], indices[i], indices[i + 1]);
				}
			}

			if (out.triangles.empty()) {
				Log::Detail(label + " has no triangles - skipped.");

				return false;
			}

			if (!src.materialId.empty()) {
				out.material = static_cast<int>(this->doc.materials.GetIndex(src.materialId));
			}

			return true;
		}

		void Loader::LoadMeshes()
		{
			for (const gltf::Mesh &src : this->doc.meshes.Elements()) {
				Mesh mesh;
				mesh.name = src.name;

				for (size_t p = 0; p < src.primitives.size(); ++p) {
					Primitive primitive;

					if (this->LoadPrimitive(src.primitives[p], src.name.empty() ? src.id : src.name, p, primitive)) {
						mesh.primitives.push_back(std::move(primitive));
					}
				}

				this->scene.meshes.push_back(std::move(mesh));
			}
		}

		// -----------------------------------------------------------------------------------------------------------
		// Uzly, skiny a animace

		void Loader::LoadNodes()
		{
			const auto &nodes = this->doc.nodes.Elements();
			this->scene.nodes.resize(nodes.size());

			for (size_t i = 0; i < nodes.size(); ++i) {
				const gltf::Node &src = nodes[i];
				Node &node = this->scene.nodes[i];
				node.name = src.name;

				if (src.GetTransformationType() == gltf::TRANSFORMATION_MATRIX) {

					for (size_t k = 0; k < 16; ++k) {
						node.local[k] = static_cast<double>(src.matrix.values[k]);
					}

					MathUtil::Decompose(node.local, node.translation, node.rotation, node.scale);
				}
				else {
					node.translation = {src.translation.x, src.translation.y, src.translation.z};
					node.rotation = MathUtil::NormalizeQuat({src.rotation.x, src.rotation.y, src.rotation.z, src.rotation.w});
					node.scale = {src.scale.x, src.scale.y, src.scale.z};
					node.local = MathUtil::Compose(node.translation, node.rotation, node.scale);
				}

				for (const double v : node.local) {

					if (!std::isfinite(v)) {
						throw std::runtime_error("Node " + std::to_string(i) + " has a transformation that is not finite");
					}
				}

				if (!src.meshId.empty()) {
					node.mesh = static_cast<int>(this->doc.meshes.GetIndex(src.meshId));
				}

				if (!src.skinId.empty()) {
					node.skin = static_cast<int>(this->doc.skins.GetIndex(src.skinId));
				}

				for (const std::string &childId : src.children) {
					node.children.push_back(static_cast<int>(this->doc.nodes.GetIndex(childId)));
				}
			}

			for (size_t i = 0; i < this->scene.nodes.size(); ++i) {

				for (const int child : this->scene.nodes[i].children) {
					Node &c = this->scene.nodes[static_cast<size_t>(child)];

					if (c.parent >= 0 || static_cast<size_t>(child) == i) {
						throw std::runtime_error("Node " + std::to_string(child) + " has more than one parent or is its own parent");
					}

					c.parent = static_cast<int>(i);
				}
			}

			// Cyklus v hierarchii: cesta k rodičům by se nikdy neskončila
			for (size_t i = 0; i < this->scene.nodes.size(); ++i) {
				int current = static_cast<int>(i);
				size_t steps = 0;

				while (current >= 0) {

					if (++steps > this->scene.nodes.size()) {
						throw std::runtime_error("The node hierarchy contains a cycle");
					}

					current = this->scene.nodes[static_cast<size_t>(current)].parent;
				}
			}

			// Vybraná scéna: výchozí, jinak první; bez scén všechny kořeny
			std::string sceneId = this->doc.defaultSceneId;

			if (sceneId.empty() && this->doc.scenes.Size() > 0) {
				sceneId = this->doc.scenes.Elements().front().id;
			}

			if (!sceneId.empty()) {
				const gltf::Scene &selected = this->doc.scenes.Get(sceneId);
				this->scene.name = selected.name;

				for (const std::string &id : selected.nodes) {
					this->scene.roots.push_back(static_cast<int>(this->doc.nodes.GetIndex(id)));
				}
			}
			else {

				for (size_t i = 0; i < this->scene.nodes.size(); ++i) {

					if (this->scene.nodes[i].parent < 0) {
						this->scene.roots.push_back(static_cast<int>(i));
					}
				}
			}
		}

		void Loader::LoadSkins()
		{
			for (const gltf::Skin &src : this->doc.skins.Elements()) {
				Skin skin;
				skin.name = src.name;

				for (const std::string &id : src.jointIds) {
					skin.joints.push_back(static_cast<int>(this->doc.nodes.GetIndex(id)));
				}

				skin.inverseBind.assign(skin.joints.size(), MathUtil::Identity());

				if (!src.inverseBindMatricesAccessorId.empty()) {
					const std::vector<float> values = this->ReadFloats(src.inverseBindMatricesAccessorId, 16, "inverseBindMatrices");

					if (values.size() < skin.joints.size() * 16) {
						throw std::runtime_error("Skin '" + src.name + "' has fewer inverse bind matrices than joints");
					}

					for (size_t j = 0; j < skin.joints.size(); ++j) {

						for (size_t k = 0; k < 16; ++k) {
							skin.inverseBind[j][k] = static_cast<double>(values[j * 16 + k]);
						}
					}
				}

				this->scene.skins.push_back(std::move(skin));
			}
		}

		void Loader::LoadAnimations()
		{
			size_t animationIndex = 0;

			for (const gltf::Animation &src : this->doc.animations.Elements()) {
				Animation animation;
				animation.name = src.name;

				for (const gltf::AnimationChannel &channel : src.channels.Elements()) {

					if (channel.target.nodeId.empty() || channel.target.path == gltf::TARGET_WEIGHTS || channel.target.path == gltf::TARGET_UNKNOWN) {
						++this->scene.skippedChannels;
						continue;
					}

					const gltf::AnimationSampler &sampler = src.samplers.Get(channel.samplerId);
					Channel out;
					out.node = static_cast<int>(this->doc.nodes.GetIndex(channel.target.nodeId));
					out.path = channel.target.path == gltf::TARGET_TRANSLATION ? Path::Translation : (channel.target.path == gltf::TARGET_ROTATION ? Path::Rotation : Path::Scale);
					out.interpolation = sampler.interpolation == gltf::INTERPOLATION_STEP ? Interpolation::Step : (sampler.interpolation == gltf::INTERPOLATION_CUBICSPLINE ? Interpolation::CubicSpline : Interpolation::Linear);

					const std::string label = "Animation " + std::to_string(animationIndex) + " ('" + src.name + "')";
					const size_t stride = out.path == Path::Rotation ? 4 : 3;

					out.times = this->ReadFloats(sampler.inputAccessorId, 1, "animation input");
					out.values = this->ReadFloats(sampler.outputAccessorId, stride, "animation output");

					const size_t expected = out.times.size() * stride * (out.interpolation == Interpolation::CubicSpline ? 3 : 1);

					if (out.times.empty() || out.values.size() != expected || (out.interpolation == Interpolation::CubicSpline && out.times.size() < 2)) {
						Log::Warn(label + ": a channel has inconsistent data (" + std::to_string(out.times.size()) + " times, " + std::to_string(out.values.size()) + " values) - skipped.");
						++this->scene.skippedChannels;
						continue;
					}

					if (!std::is_sorted(out.times.begin(), out.times.end())) {
						Log::Warn(label + ": a channel has key times that do not increase - skipped.");
						++this->scene.skippedChannels;
						continue;
					}

					animation.channels.push_back(std::move(out));
				}

				this->scene.animations.push_back(std::move(animation));
				++animationIndex;
			}
		}

		void Loader::CheckExtensions() const
		{
			static const std::set<std::string> supported = {
				"KHR_texture_transform", "KHR_materials_unlit", "KHR_materials_pbrSpecularGlossiness", "KHR_materials_emissive_strength", "KHR_mesh_quantization",
			};

			// Rozšíření, která mění jen vzhled nebo přidávají věci mimo geometrii (světla, varianty a podrobnosti materiálů): když je soubor
			// označí za povinná, cílový formát o ně přijde, ale model se převést dá. Cokoli jiného (komprese geometrie, jiné formáty textur) se odmítne.
			static const std::set<std::string> appearanceOnly = {
				"KHR_lights_punctual", "KHR_materials_variants", "KHR_materials_ior", "KHR_materials_specular", "KHR_materials_clearcoat", "KHR_materials_sheen",
				"KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_iridescence", "KHR_materials_anisotropy", "KHR_materials_dispersion",
				"KHR_materials_diffuse_transmission", "KHR_materials_volume_scatter", "KHR_xmp_json_ld",
			};

			for (const std::string &name : this->doc.extensionsRequired) {

				if (supported.find(name) != supported.end()) {
					continue;
				}

				if (appearanceOnly.find(name) == appearanceOnly.end()) {
					throw std::runtime_error("The file requires the extension " + name + ", which is not supported");
				}

				Log::Warn("The file requires the extension " + name + ", which " + targetFormat + " cannot express - it is ignored (the geometry, skeleton and animations are converted).");
			}

			for (const std::string &name : this->doc.extensionsUsed) {

				if (supported.find(name) == supported.end() && this->doc.extensionsRequired.find(name) == this->doc.extensionsRequired.end()) {
					Log::Detail("The extension " + name + " is used by the file but not supported - ignored.");
				}
			}
		}

		Scene Loader::Run()
		{
			this->CheckExtensions();
			this->LoadMaterials();
			this->LoadMeshes();
			this->LoadNodes();
			this->LoadSkins();
			this->LoadAnimations();

			return std::move(this->scene);
		}
	}

	Scene Load(const fs::path &path, const std::string &targetFormat)
	{
		std::error_code ec;

		if (!fs::is_regular_file(path, ec)) {
			throw std::runtime_error("The input file does not exist: " + TextUtil::PathToUtf8(path));
		}

		// Soubor se pozná podle podpisu "glTF", přípona rozhoduje jen o případné chybě
		char magic[4] = {};

		{
			std::ifstream probe(path, std::ios::binary);
			probe.read(magic, 4);

			if (!probe && probe.gcount() < 4) {
				magic[0] = '\0';
			}
		}

		const bool isGlb = std::string(magic, 4) == "glTF";
		const fs::path directory = path.has_parent_path() ? path.parent_path() : fs::path(".");
		auto streamReader = std::make_shared<FileStreamReader>(directory);

		try {
			std::unique_ptr<gltf::GLTFResourceReader> reader;
			gltf::Document document;

			if (isGlb) {
				auto glbStream = std::make_shared<std::ifstream>(path, std::ios::binary);

				if (!glbStream || !(*glbStream)) {
					throw std::runtime_error("Unable to open " + TextUtil::PathToUtf8(path));
				}

				auto glbReader = std::make_unique<gltf::GLBResourceReader>(streamReader, glbStream);
				document = gltf::Deserialize(glbReader->GetJson(), gltf::KHR::GetKHRExtensionDeserializer());
				reader = std::move(glbReader);
			}
			else {
				std::ifstream json(path, std::ios::binary);

				if (!json) {
					throw std::runtime_error("Unable to open " + TextUtil::PathToUtf8(path));
				}

				document = gltf::Deserialize(json, gltf::KHR::GetKHRExtensionDeserializer());
				reader = std::make_unique<gltf::GLTFResourceReader>(streamReader);
			}

			Loader loader(document, *reader, targetFormat);

			return loader.Run();
		}
		catch (const gltf::GLTFException &ex) {
			throw std::runtime_error(std::string("Microsoft glTF-SDK: ") + ex.what());
		}
	}
}
