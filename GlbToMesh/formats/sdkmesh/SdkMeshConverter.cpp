/**
 * @file      SdkMeshConverter.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace převodu glTF -> SDKMESH (viz SdkMeshConverter.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Souřadnice: glTF je pravotočivé a DirectXTK také, takže se pozice, normály a matice ve výchozím stavu kopírují beze změny.
 * Volba leftHanded zrcadlí osu Z (pozice, normály, tangenty a matice jako S * M * S pro S = diag(1, 1, -1), kvaterniony animací
 * (x, y, z, w) -> (-x, -y, z, w), posuny z -> -z) a dává data ve stylu DXUT (soubor soldier.sdkmesh).
 *
 * Vinutí: glTF má přední strany proti směru hodin, SDKMESH má trojúhelníky po směru hodin (výchozí stav rasterizéru Direct3D, v pravotočivé
 * podobě tak vypadá tank.sdkmesh z tutoriálů DirectXTK, v levotočivé soldier.sdkmesh), proto se u každého trojúhelníku prohodí druhý
 * a třetí index. To platí v obou soustavách: zrcadlení osy Z orientaci ploch otáčí, ale současně se otáčí i vektorový součin
 * hran vůči normálám, takže výsledek je shodný se soubory z DXUT. Výjimka: instance pod uzlem se zápornou determinantou matice
 * (zrcadlení) už po transformaci obrací orientaci sama, takže se tam indexy neprohazují (geometrie se pro ně zapíše zvlášť).
 *
 * Hierarchie: každý uzel scény je snímek (frame); mesh nese vlastní vrcholy v místních souřadnicích a snímek na něj odkazuje
 * (DirectXTK svazuje mesh jen s prvním snímkem, proto má každá instance vlastní záznam meshe, vertex a index buffery se sdílejí).
 * Skinované meshe jsou v klidové poloze ve světových souřadnicích (podle specifikace glTF se transformace jejich uzlu ignoruje):
 * do vrcholů se vloží matice Σ w * (světová matice kloubu * inverzní bind matice), která je u správně připravených modelů společná
 * a rovná se světové transformaci kostry; mesh visí na samostatném snímku s jednotkovou maticí. Inverzní bind matice si DirectXTK i DXUT
 * dopočítají z klidové pózy snímků.
 *
 * Zapékání (volba bakeTransforms): neskinované meshe, jejichž uzel ani žádný předek nemá animační kanál, dostanou vrcholy ve světových
 * souřadnicích (pozice, normály, tangenty a binormály se vynásobí světovou maticí uzlu, potom se případně zrcadlí osa Z) a visí na
 * samostatném snímku s jednotkovou maticí pod kořenem, stejně jako skinované meshe. Prohlížeč, který matice snímků nepoužije (DirectXTK
 * bez kostí), tak kreslí model správně. Geometrie se kvůli tomu nesdílí mezi instancemi. Meshe pod animovanými uzly se nezapékají,
 * protože by se při přehrávání animace nehýbaly; zůstávají na snímcích uzlů a jejich transformaci řeší přehrávač animací.
 *
 * Matice v SDKMESH jsou řádkové (v' = v * M) a mají stejné paměťové rozložení jako sloupcové matice glTF, proto se netransponují.
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

// Hlavičkové soubory projektu
#include "SdkMeshConverter.h"

#include "core/ExportUtil.h"
#include "core/Log.h"
#include "core/MathUtil.h"
#include "core/TextUtil.h"

namespace SdkMeshConvert {

	namespace {

		using ExportUtil::Clamp01;
		using ExportUtil::LinearToSrgb;
		using ExportUtil::NodeAnimation;
		using ExportUtil::Prepared;
		using ExportUtil::Trs;
		using MathUtil::Mat4;
		using MathUtil::Quat;
		using MathUtil::Vec3;

		/// Nejvyšší počet kostí (vlivů) jednoho meshe, který se vejde do 8bitových indexů
		constexpr size_t MAX_INFLUENCES = 256;

		/// Nejvyšší počet kostí, který zvládají skinované efekty DirectXTK
		constexpr size_t EFFECT_MAX_BONES = 72;

		/// Nejvyšší počet klíčů v animaci krát počet stop (ochrana proti obrovským souborům)
		constexpr size_t MAX_TOTAL_KEYS = 40000000;

		/// Odchylka klidové pózy od bind pózy, do které se skinované vrcholy nemění (šum plovoucí řádové čárky)
		constexpr double BIND_TOLERANCE = 1e-5;

		using TextUtil::TruncateUtf8;

		/// Jedinečný název (bez ohledu na velikost písmen) o nejvýš maxBytes bajtech; shodný název dostane příponu _2, _3, ...
		std::string UniqueName(std::set<std::string> &used, const std::string &wanted, size_t maxBytes)
		{
			return TextUtil::UniqueName(used, wanted, std::string(), maxBytes);
		}

		/// Rozdělí váhy na 8bitové tak, aby jejich součet byl přesně 255 (metoda největšího zbytku); w musí být normalizované
		void QuantizeWeights(const double w[4], uint8_t out[4])
		{
			int total = 0;
			double fraction[4];

			for (size_t i = 0; i < 4; ++i) {
				const double scaled = std::clamp(w[i], 0.0, 1.0) * 255.0;
				const double whole = std::floor(scaled);
				out[i] = static_cast<uint8_t>(whole);
				fraction[i] = scaled - whole;
				total += out[i];
			}

			while (total < 255) {
				size_t best = 0;

				for (size_t i = 1; i < 4; ++i) {
					if (fraction[i] > fraction[best]) {
						best = i;
					}
				}

				++out[best];
				fraction[best] = -1.0;
				++total;
			}
		}

		/// Kostra jednoho skinu: matice "světová matice kloubu * inverzní bind matice" pro každý kloub
		struct SkinInfo {
			bool built = false;
			std::vector<Mat4> bind;
			bool identity = true;	  // všechny matice jsou jednotkové (v mezích BIND_TOLERANCE): vrcholy se nemění
			bool flipWinding = false; // matice obrací orientaci
		};

		/// Geometrie jednoho meshe glTF pro danou kostru a orientaci vinutí: buffery a podmnožiny, na které odkazují instance
		struct Variant {
			bool valid = false;
			bool skinned = false;
			uint32_t vertexBuffer = 0;
			uint32_t indexBuffer = 0;
			std::vector<uint32_t> subsets;
			std::vector<uint32_t> influences;
			SdkMesh::Float3 center {0.0f, 0.0f, 0.0f};
			SdkMesh::Float3 extents {0.0f, 0.0f, 0.0f};
			size_t vertices = 0;
			size_t triangles = 0;
		};

		/// Rozložení vrcholu (pořadí jako v souborech DXUT: pozice, váhy a indexy kostí, normála, barva, UV, tangenta, binormála)
		struct Layout {
			bool skinned = false;
			bool color = false;
			bool texcoord = false;
			bool tangents = false;
			uint32_t stride = 0;
			uint32_t offsetWeights = 0, offsetIndices = 0, offsetNormal = 0, offsetColor = 0, offsetTexcoord = 0, offsetTangent = 0, offsetBinormal = 0;
			std::vector<SdkMesh::D3DVertexElement9> decl;
		};

		/// Sestaví rozložení vrcholu podle toho, které atributy mesh má
		Layout MakeLayout(bool skinned, bool color, bool texcoord, bool tangents)
		{
			Layout layout;
			layout.skinned = skinned;
			layout.color = color;
			layout.texcoord = texcoord;
			layout.tangents = tangents;

			uint32_t offset = 0;

			const auto add = [&](uint8_t type, uint8_t usage, uint32_t size) -> uint32_t {
				SdkMesh::D3DVertexElement9 element {};
				element.Stream = 0;
				element.Offset = static_cast<uint16_t>(offset);
				element.Type = type;
				element.Method = 0;
				element.Usage = usage;
				element.UsageIndex = 0;
				layout.decl.push_back(element);
				offset += size;

				return element.Offset;
			};

			add(SdkMesh::D3DDECLTYPE_FLOAT3, SdkMesh::D3DDECLUSAGE_POSITION, 12);

			if (skinned) {
				layout.offsetWeights = add(SdkMesh::D3DDECLTYPE_UBYTE4N, SdkMesh::D3DDECLUSAGE_BLENDWEIGHT, 4);
				layout.offsetIndices = add(SdkMesh::D3DDECLTYPE_UBYTE4, SdkMesh::D3DDECLUSAGE_BLENDINDICES, 4);
			}

			layout.offsetNormal = add(SdkMesh::D3DDECLTYPE_FLOAT3, SdkMesh::D3DDECLUSAGE_NORMAL, 12);

			if (color) {
				layout.offsetColor = add(SdkMesh::D3DDECLTYPE_D3DCOLOR, SdkMesh::D3DDECLUSAGE_COLOR, 4);
			}

			if (texcoord) {
				layout.offsetTexcoord = add(SdkMesh::D3DDECLTYPE_FLOAT2, SdkMesh::D3DDECLUSAGE_TEXCOORD, 8);
			}

			if (tangents) {
				layout.offsetTangent = add(SdkMesh::D3DDECLTYPE_FLOAT3, SdkMesh::D3DDECLUSAGE_TANGENT, 12);
				layout.offsetBinormal = add(SdkMesh::D3DDECLTYPE_FLOAT3, SdkMesh::D3DDECLUSAGE_BINORMAL, 12);
			}

			layout.stride = offset;

			return layout;
		}

		/// Převodník scény na model SDKMESH a soubory animací (pravidla převodu jsou popsána v záhlaví souboru)
		class Converter : public ExportUtil::SceneConverter
		{
		  public:
			/// @param scene Načtená scéna (musí přežít převodník)
			/// @param options Volby převodu (musí přežít převodník)
			Converter(const Gltf::Scene &scene, const Options &options) : SceneConverter(scene, options), options(options)
			{
			}

			/// Převede celou scénu a vrátí model SDKMESH, soubory textur, animace a statistiku
			Result Run();

		  private:
			const Options &options;
			Result result;

			std::vector<int> frameOfNode; // uzel -> index snímku, -1 = uzel bez snímku
			std::vector<int> nodeOfFrame; // index snímku -> uzel, -1 = snímek bez uzlu (kořen, držák skinovaného meshe)
			std::vector<int> holderOfNode; // skinovaný nebo zapékaný uzel -> snímek, na kterém visí jeho mesh
			std::vector<bool> bakedNode;   // uzel -> mesh se zapeče do světových souřadnic (volba bakeTransforms)
			size_t unbakedAnimated = 0;	   // počet instancí pod animovanými uzly, které se při zapékání ponechaly
			std::set<std::string> usedFrameNames;
			std::set<std::string> usedMeshNames;
			std::set<std::string> usedMaterialNames;
			std::map<std::tuple<int, bool, bool>, uint32_t> materialRecords; // (materiál glTF, skinovaný, barvy vrcholů) -> index materiálu SDKMESH
			std::vector<SkinInfo> skins;
			std::map<std::tuple<int, int, bool, int>, Variant> variants; // (mesh, skin, obrácené vinutí, zapečený uzel nebo -1) -> geometrie

			// Textury a materiály
			/// Název textury je v SDKMESH pole 260 bajtů; delší název se odmítne
			void CheckTextureFileName(const std::string &fileName) const override;
			/// Zajistí záznam materiálu SDKMESH pro (materiál glTF, skinovaný, barvy vrcholů) a vrátí jeho index
			uint32_t EnsureMaterial(int index, bool skinned, bool vertexColor);
			const Gltf::TextureRef *UvTransformSource(const Gltf::Material &material);

			// Animace
			/// Vytvoří animace: každá animace glTF se vzorkuje v pevných časech podle --anim-fps
			void BuildAnimations();

			// Hierarchie
			bool IsSkinnedNode(const Gltf::Node &node) const;
			/// Mesh má aspoň jeden použitelný trojúhelník
			bool HasTriangles(const Gltf::Mesh &mesh) const;
			/// Určí, které neskinované meshe se zapečou do světových souřadnic (volba bakeTransforms); volá se po vytvoření snímků uzlů
			void ChooseBakedNodes();
			/// Postaví hierarchii snímků: každý uzel scény je jeden snímek, skinované a zapečené meshe visí na samostatných snímcích
			void BuildFrames();

			// Geometrie
			/// Při prvním použití postaví podklady skinu: matice kloubů a inverzní bind matice
			SkinInfo &EnsureSkin(int skinIndex);
			/// Geometrie meshe pro daný skin, směr vinutí a případně zapékaný uzel; sestaví se jednou a sdílí se mezi instancemi
			const Variant &EnsureVariant(int meshIndex, int skinIndex, bool flipWinding, int bakeNode);
			/// Sestaví geometrii meshe: vrcholy, indexy a podmnožiny po primitivech; bakeNode >= 0 vrcholy převede do světových souřadnic
			Variant BuildVariant(int meshIndex, int skinIndex, bool flipWinding, int bakeNode);
			/// Přidá instanci meshe uzlu: záznam meshe odkazující na sdílené buffery
			void AddInstance(int nodeIndex);
		};

		// ---------------------------------------------------------------------------------------------------
		// Textury a materiály

		void Converter::CheckTextureFileName(const std::string &fileName) const
		{
			// Název textury v SDKMESH je pole 260 bajtů
			if (fileName.size() >= SdkMesh::MAX_TEXTURE_NAME) {
				throw std::runtime_error("The texture file name '" + fileName.substr(0, 40) + "...' is too long for SDKMESH");
			}
		}

		/// Textura, jejíž transformace UV (KHR_texture_transform) se zapíše do souřadnic UV vrcholů; SDKMESH nemá matici UV
		const Gltf::TextureRef *Converter::UvTransformSource(const Gltf::Material &material)
		{
			const std::string name = material.name;

			// Textura se musí opravdu zapsat (TextureFile ji zároveň zaregistruje); emisivní textura se ve verzi 101 neexportuje,
			// takže její transformace nic neznamená
			const auto accept = [&](const Gltf::TextureRef &ref) { return !this->TextureFile(ref.image, name).empty() && !(&ref == &material.emissiveTexture && !this->options.pbr); };

			return this->PickUvTransform({&material.baseColorTexture, &material.normalTexture, &material.emissiveTexture}, accept, name, "SDKMESH has one set of UVs");
		}

		uint32_t Converter::EnsureMaterial(int index, bool skinned, bool vertexColor)
		{
			const auto key = std::make_tuple(index, skinned, vertexColor);
			const auto cached = this->materialRecords.find(key);

			if (cached != this->materialRecords.end()) {
				return cached->second;
			}

			const Gltf::Material &src = this->SourceMaterial(index);
			const std::string sourceName = src.name.empty() ? "Material_" + std::to_string(index) : TextUtil::ToUtf8(src.name);

			// DirectXTK zná efekt jen podle názvu materiálu (EffectFactory ukládá efekty podle názvu) a podle příznaků z deklarace vrcholů
			// (skinning, barvy vrcholů), proto má každá kombinace příznaků vlastní záznam s jedinečným názvem
			std::string wanted = sourceName;

			if (this->usedMaterialNames.find(TextUtil::ToLowerAscii(TruncateUtf8(wanted, SdkMesh::MAX_MATERIAL_NAME - 1))) != this->usedMaterialNames.end()) {
				wanted += std::string(skinned ? "_skinned" : "") + (vertexColor ? "_vcolor" : "");
			}

			SdkMesh::Material out;
			out.name = UniqueName(this->usedMaterialNames, wanted, SdkMesh::MAX_MATERIAL_NAME - 1);

			// Průhlednost: jen režim BLEND; MASK (ořez podle alfa) SDKMESH nezná
			float alpha = 1.0f;

			if (src.alphaMode == Gltf::AlphaMode::Blend) {
				alpha = Clamp01(src.baseColor[3]);
			}
			else if (src.alphaMode == Gltf::AlphaMode::Mask) {
				this->WarnOnce("mask:" + out.name, "Material '" + sourceName + "' uses alphaMode MASK, which SDKMESH does not support - exported as opaque.");
			}

			if (src.doubleSided) {
				this->WarnOnce("doublesided", "Some materials are double-sided; SDKMESH has no such flag, so DirectXTK draws them single-sided (draw the model with culling disabled if needed).");
			}

			if (this->options.pbr) {
				// Verze 200: albedo, normála, RMA (R = okluze, G = drsnost, B = kovovost, tedy vrstvení glTF) a emisivní textura
				out.alpha = alpha;
				out.albedoTexture = this->TextureFile(src.baseColorTexture.image, out.name);
				out.normalTexture = this->TextureFile(src.normalTexture.image, out.name);
				out.emissiveTexture = this->TextureFile(src.emissiveTexture.image, out.name);
				out.rmaTexture = this->TextureFile(src.metallicRoughnessTexture.image, out.name);

				if (src.occlusionTexture.image >= 0 && src.occlusionTexture.image != src.metallicRoughnessTexture.image) {
					this->WarnOnce("occlusion:" + out.name, "Material '" + sourceName + "': the occlusion texture is a different image than the metallic-roughness texture; SDKMESH has just one RMA texture - the occlusion is not exported.");
				}

				if (out.albedoTexture.empty() || out.normalTexture.empty() || out.rmaTexture.empty()) {
					this->WarnOnce(
						"pbrmissing:" + out.name,
						"Material '" + sourceName + "' has no base color, normal and metallic-roughness texture together; the PBR effects of DirectXTK need all of them, and SDKMESH cannot store the material factors."
					);
				}
				else if (src.baseColor[0] != 1.0f || src.baseColor[1] != 1.0f || src.baseColor[2] != 1.0f) {
					this->WarnOnce("pbrfactor:" + out.name, "Material '" + sourceName + "': the base color factor cannot be stored in an SDKMESH PBR material - only the textures are exported.");
				}

				this->materialRecords[key] = static_cast<uint32_t>(this->result.model.materials.size());
				this->result.model.materials.push_back(std::move(out));

				return this->materialRecords[key];
			}

			const auto color = [this](float c) { return this->options.srgbColors ? LinearToSrgb(c) : c; };

			float diffuse[3];

			for (size_t c = 0; c < 3; ++c) {
				diffuse[c] = Clamp01(src.baseColor[c]);
			}

			// Odlesk: z drsnosti (Blinn-Phong: roughness = sqrt(2 / (n + 2))) a základní odrazivosti; matné materiály bez odlesku
			float specular[3] = {0.0f, 0.0f, 0.0f};
			float power = 0.0f;
			float roughness = src.roughness;

			if (src.specularGlossiness) {
				roughness = 1.0f - Clamp01(src.glossiness);

				for (size_t c = 0; c < 3; ++c) {
					specular[c] = Clamp01(src.specular[c]);
				}
			}
			else {
				const float metallic = Clamp01(src.metallic);

				for (size_t c = 0; c < 3; ++c) {
					specular[c] = 0.04f * (1.0f - metallic) + diffuse[c] * metallic;
				}
			}

			roughness = Clamp01(roughness);

			if (src.unlit || roughness >= 0.95f) {
				specular[0] = specular[1] = specular[2] = 0.0f;
			}
			else {
				// DirectXTK bere odlesek jen při Power > 0
				power = std::clamp(2.0f / (std::max(roughness, 0.045f) * std::max(roughness, 0.045f)) - 2.0f, 1.0f, 1000.0f);
			}

			out.diffuse = {color(diffuse[0]), color(diffuse[1]), color(diffuse[2]), alpha};
			out.ambient = {color(diffuse[0] * 0.2f), color(diffuse[1] * 0.2f), color(diffuse[2] * 0.2f), 1.0f}; // ambient.w = 1: blok barev není "nevyplněný"
			out.specular = {color(specular[0]), color(specular[1]), color(specular[2]), 1.0f};

			const auto emissive = [&](size_t c) { return std::isfinite(src.emissive[c]) ? color(std::max(src.emissive[c], 0.0f)) : 0.0f; };
			out.emissive = {emissive(0), emissive(1), emissive(2), 0.0f};
			out.power = power;

			// Textury verze 101: difuzní a normálová; ostatní nemají v SDKMESH obdobu
			out.diffuseTexture = this->TextureFile(src.baseColorTexture.image, out.name);
			out.normalTexture = this->TextureFile(src.normalTexture.image, out.name);

			if (!out.normalTexture.empty() && out.diffuseTexture.empty()) {
				this->WarnOnce("nmnodiffuse:" + out.name, "Material '" + sourceName + "' has a normal map but no base color texture; NormalMapEffect of DirectXTK needs a diffuse texture, so the model will look black.");
			}

			if (src.emissiveTexture.image >= 0 && !this->options.noTextures) {
				this->WarnOnce("emissivetex:" + out.name, "Material '" + sourceName + "': SDKMESH 101 has no emissive texture - not exported (use --pbr for SDKMESH 200).");
			}

			this->WarnUnsupportedPbrTextures(src, out.name, sourceName, "SDKMESH 101");
			this->WarnTexCoordSet({&src.baseColorTexture, &src.normalTexture}, out.name, sourceName, "the exported vertices have just one");
			NoteUnlit(src, sourceName);

			this->materialRecords[key] = static_cast<uint32_t>(this->result.model.materials.size());
			this->result.model.materials.push_back(std::move(out));

			return this->materialRecords[key];
		}

		// ---------------------------------------------------------------------------------------------------
		// Animace (vzorkování kanálů)

		// ---------------------------------------------------------------------------------------------------
		// Animace (soubory .sdkmesh_anim)

		void Converter::BuildAnimations()
		{
			const uint32_t fps = this->options.animationFps;
			size_t skippedChannels = this->scene.skippedChannels;

			for (size_t animationIndex = 0; animationIndex < this->scene.animations.size(); ++animationIndex) {
				const Gltf::Animation &animation = this->scene.animations[animationIndex];
				const std::string clipName = animation.name.empty() ? "Clip_" + std::to_string(animationIndex) : TextUtil::ToUtf8(animation.name);

				// Kanály podle uzlů; uzel bez snímku (mimo vybranou scénu) animovat nejde
				std::unordered_map<int, NodeAnimation> byNode;
				double start = std::numeric_limits<double>::infinity();
				double end = -std::numeric_limits<double>::infinity();

				for (const Gltf::Channel &channel : animation.channels) {

					if (channel.node < 0 || static_cast<size_t>(channel.node) >= this->frameOfNode.size() || this->frameOfNode[static_cast<size_t>(channel.node)] < 0 || channel.times.empty()) {
						++skippedChannels;

						continue;
					}

					const Gltf::Channel *&slot = byNode[channel.node].Slot(channel.path);

					if (slot) {
						++skippedChannels; // dva kanály téhož uzlu a vlastnosti specifikace zakazuje

						continue;
					}

					slot = &channel;
					start = std::min(start, static_cast<double>(channel.times.front()));
					end = std::max(end, static_cast<double>(channel.times.back()));
				}

				if (byNode.empty()) {
					Log::Detail("Animation '" + clipName + "' animates no exported node - skipped.");

					continue;
				}

				// Klíče leží v pevných časech k / FPS od začátku animace; DXUT přehrává klíče 1 .. N-1, proto jsou potřeba aspoň dva
				const double duration = std::max(end - start, 0.0);
				const double wanted = std::ceil(duration * fps - 1e-3) + 1.0;
				const size_t keyCount = static_cast<size_t>(std::max(wanted, 2.0));

				if (!std::isfinite(wanted) || keyCount * byNode.size() > MAX_TOTAL_KEYS) {
					throw std::runtime_error("Animation '" + clipName + "' is too long for " + std::to_string(fps) + " keys per second - lower --anim-fps");
				}

				// Uzly v pořadí snímků
				std::vector<int> nodes;

				for (const auto &entry : byNode) {
					nodes.push_back(entry.first);
				}

				std::sort(nodes.begin(), nodes.end(), [this](int a, int b) { return this->frameOfNode[static_cast<size_t>(a)] < this->frameOfNode[static_cast<size_t>(b)]; });

				SdkMesh::Animation out;
				out.name = clipName;
				out.fps = fps;
				out.keyCount = static_cast<uint32_t>(keyCount);

				for (const int node : nodes) {
					SdkMesh::AnimationTrack track;
					track.frameName = this->result.model.frames[static_cast<size_t>(this->frameOfNode[static_cast<size_t>(node)])].name;
					track.keys.reserve(keyCount);
					Quat previous;
					bool hasPrevious = false;

					for (size_t k = 0; k < keyCount; ++k) {
						const Trs trs = this->SampleNode(node, byNode, start + static_cast<double>(k) / static_cast<double>(fps));
						Quat q = MathUtil::NormalizeQuat(trs.q);
						Vec3 t = trs.t;

						if (this->options.leftHanded) {
							// Zrcadlení osy Z: S * R * S má kvaternion (-x, -y, z, w), posun (x, y, -z)
							q = {-q.x, -q.y, q.z, q.w};
							t.z = -t.z;
						}

						// Spojitost kvaternionů (q a -q jsou táž rotace): žádné skoky při interpolaci
						if (hasPrevious && MathUtil::Dot(q, previous) < 0.0) {
							q = {-q.x, -q.y, -q.z, -q.w};
						}

						previous = q;
						hasPrevious = true;

						SdkMesh::AnimationKeyRecord key {};
						key.Translation = {static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z)};
						key.Orientation = {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
						key.Scaling = {static_cast<float>(trs.s.x), static_cast<float>(trs.s.y), static_cast<float>(trs.s.z)};
						track.keys.push_back(key);
					}

					out.tracks.push_back(std::move(track));
				}

				this->result.stats.keys += keyCount * out.tracks.size();
				++this->result.stats.animations;
				this->result.animations.push_back(std::move(out));
			}

			if (skippedChannels > 0) {
				this->WarnOnce("skipped-channels", std::to_string(skippedChannels) + " animation channels were not exported (nodes outside of the exported scene, morph target weights or duplicate channels).");
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// Hierarchie snímků

		bool Converter::HasTriangles(const Gltf::Mesh &mesh) const
		{
			for (const Gltf::Primitive &primitive : mesh.primitives) {

				if (primitive.triangles.size() >= 3 && !primitive.positions.empty()) {
					return true;
				}
			}

			return false;
		}

		/// Uzel se skinovaným meshem: má skin a aspoň jedno jeho primitivum má váhy kostí
		bool Converter::IsSkinnedNode(const Gltf::Node &node) const
		{
			if (node.mesh < 0 || node.skin < 0 || static_cast<size_t>(node.skin) >= this->scene.skins.size()) {
				return false;
			}

			for (const Gltf::Primitive &primitive : this->scene.meshes[static_cast<size_t>(node.mesh)].primitives) {

				if (primitive.triangles.size() >= 3 && !primitive.positions.empty() && !primitive.joints.empty() && !primitive.weights.empty()) {
					return true;
				}
			}

			return false;
		}

		void Converter::ChooseBakedNodes()
		{
			const size_t nodeCount = this->scene.nodes.size();
			const std::vector<SdkMesh::Frame> &frames = this->result.model.frames;
			this->bakedNode.assign(nodeCount, false);
			this->unbakedAnimated = 0;

			if (!this->options.bakeTransforms) {
				return;
			}

			// Uzly, které animace mění (bez exportu animací se nic nehýbe); stejná pravidla jako v BuildAnimations
			const std::vector<bool> animated = this->options.noAnimations ? std::vector<bool>(nodeCount, false) : this->AnimatedNodes();

			// Snímky jsou v pořadí do hloubky (rodič před potomkem), takže se příznak "uzel nebo předek je animovaný" dá odvodit v jednom průchodu
			std::vector<bool> underAnimation(frames.size(), false);

			for (size_t f = 1; f < frames.size(); ++f) {
				const int node = this->nodeOfFrame[f];

				if (node < 0) {
					continue;
				}

				const uint32_t parent = frames[f].parent;
				underAnimation[f] = animated[static_cast<size_t>(node)] || (parent > 0 && underAnimation[parent]);

				const Gltf::Node &source = this->scene.nodes[static_cast<size_t>(node)];

				if (source.mesh < 0 || !this->HasTriangles(this->scene.meshes[static_cast<size_t>(source.mesh)]) || this->IsSkinnedNode(source)) {
					continue;
				}

				if (underAnimation[f]) {
					++this->unbakedAnimated;
				}
				else {
					this->bakedNode[static_cast<size_t>(node)] = true;
				}
			}
		}

		void Converter::BuildFrames()
		{
			const size_t nodeCount = this->scene.nodes.size();
			std::vector<bool> inScene(nodeCount, false);

			// Uzly vybrané scény
			std::vector<int> roots;

			for (const int root : this->scene.roots) {

				if (root < 0 || static_cast<size_t>(root) >= nodeCount) {
					continue;
				}

				roots.push_back(root);
				std::vector<int> stack {root};

				while (!stack.empty()) {
					const int current = stack.back();
					stack.pop_back();

					if (inScene[static_cast<size_t>(current)]) {
						continue;
					}

					inScene[static_cast<size_t>(current)] = true;

					for (const int child : this->scene.nodes[static_cast<size_t>(current)].children) {
						stack.push_back(child);
					}
				}
			}

			// Klouby skinů mimo scénu (a jejich předky) je potřeba také zahrnout, jinak by skinovaný mesh neměl na co odkazovat
			bool changed = true;
			size_t outside = 0;

			while (changed) {
				changed = false;

				for (size_t i = 0; i < nodeCount; ++i) {

					if (!inScene[i] || !this->IsSkinnedNode(this->scene.nodes[i])) {
						continue;
					}

					for (const int joint : this->scene.skins[static_cast<size_t>(this->scene.nodes[i].skin)].joints) {
						std::vector<int> path;
						int current = joint;

						while (current >= 0 && !inScene[static_cast<size_t>(current)]) {
							path.push_back(current);
							current = this->scene.nodes[static_cast<size_t>(current)].parent;
						}

						if (path.empty()) {
							continue;
						}

						for (const int node : path) {
							inScene[static_cast<size_t>(node)] = true;
						}

						if (current < 0) {
							roots.push_back(path.back());
						}

						outside += path.size();
						changed = true;
					}
				}
			}

			if (outside > 0) {
				Log::Warn(std::to_string(outside) + " joint nodes of the skins are not in the exported scene; they were added to the hierarchy.");
			}

			// Snímky v pořadí do hloubky (rodič před potomkem); snímek 0 je jednotkový kořen jako ve všech souborech DXUT
			std::vector<SdkMesh::Frame> &frames = this->result.model.frames;
			this->frameOfNode.assign(nodeCount, -1);
			this->nodeOfFrame.assign(1, -1);
			this->holderOfNode.assign(nodeCount, -1);
			frames.emplace_back();
			std::vector<std::vector<uint32_t>> kids(1);

			for (const int root : roots) {
				std::vector<std::pair<int, uint32_t>> stack {{root, 0}};

				while (!stack.empty()) {
					const auto [node, parent] = stack.back();
					stack.pop_back();

					if (this->frameOfNode[static_cast<size_t>(node)] >= 0) {
						continue;
					}

					const Gltf::Node &source = this->scene.nodes[static_cast<size_t>(node)];
					const uint32_t index = static_cast<uint32_t>(frames.size());
					SdkMesh::Frame frame;
					frame.name = UniqueName(this->usedFrameNames, source.name.empty() ? "Node_" + std::to_string(node) : TextUtil::ToUtf8(source.name), SdkMesh::MAX_FRAME_NAME - 1);
					frame.parent = parent;
					MathUtil::ToFloats(this->Mirror(source.local), frame.matrix.m);

					frames.push_back(std::move(frame));
					kids.emplace_back();
					kids[parent].push_back(index);
					this->frameOfNode[static_cast<size_t>(node)] = static_cast<int>(index);
					this->nodeOfFrame.push_back(node);

					for (size_t k = source.children.size(); k-- > 0;) {
						const int child = source.children[k];

						if (child >= 0 && static_cast<size_t>(child) < nodeCount && inScene[static_cast<size_t>(child)]) {
							stack.push_back({child, index});
						}
					}
				}
			}

			// Skinované meshe nesou transformaci kostí, ne uzlu, a zapečené meshe už mají transformaci ve vrcholech, proto visí na vlastním
			// snímku s jednotkovou maticí pod kořenem
			this->ChooseBakedNodes();
			const size_t hierarchyFrames = frames.size();

			for (size_t f = 1; f < hierarchyFrames; ++f) {
				const int node = this->nodeOfFrame[f];

				if (node < 0 || !(this->IsSkinnedNode(this->scene.nodes[static_cast<size_t>(node)]) || this->bakedNode[static_cast<size_t>(node)])) {
					continue;
				}

				SdkMesh::Frame holder;
				holder.name = UniqueName(this->usedFrameNames, frames[f].name + "_Mesh", SdkMesh::MAX_FRAME_NAME - 1);
				holder.parent = 0;
				const uint32_t index = static_cast<uint32_t>(frames.size());
				frames.push_back(std::move(holder));
				kids.emplace_back();
				kids[0].push_back(index);
				this->nodeOfFrame.push_back(-1);
				this->holderOfNode[static_cast<size_t>(node)] = static_cast<int>(index);
			}

			for (size_t f = 0; f < frames.size(); ++f) {
				const std::vector<uint32_t> &children = kids[f];

				if (!children.empty()) {
					frames[f].child = children.front();
				}

				for (size_t k = 0; k + 1 < children.size(); ++k) {
					frames[children[k]].sibling = children[k + 1];
				}
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// Skinning a geometrie

		SkinInfo &Converter::EnsureSkin(int skinIndex)
		{
			SkinInfo &info = this->skins[static_cast<size_t>(skinIndex)];

			if (info.built) {
				return info;
			}

			info.built = true;
			const Gltf::Skin &skin = this->scene.skins[static_cast<size_t>(skinIndex)];
			const Mat4 identity = MathUtil::Identity();
			info.bind.resize(skin.joints.size());
			double deviation = 0.0;
			double spread = 0.0;

			for (size_t k = 0; k < skin.joints.size(); ++k) {
				const int joint = skin.joints[k];

				if (joint < 0 || static_cast<size_t>(joint) >= this->frameOfNode.size() || this->frameOfNode[static_cast<size_t>(joint)] < 0) {
					throw std::runtime_error("The skin '" + skin.name + "' has a joint that is not part of the exported hierarchy");
				}

				info.bind[k] = MathUtil::Multiply(this->world[static_cast<size_t>(joint)], skin.inverseBind[k]);

				for (size_t i = 0; i < 16; ++i) {
					deviation = std::max(deviation, std::fabs(info.bind[k][i] - identity[i]));
					spread = std::max(spread, std::fabs(info.bind[k][i] - info.bind[0][i]));
				}
			}

			info.identity = !(deviation > BIND_TOLERANCE);

			for (const Mat4 &m : info.bind) {
				const double det = MathUtil::Determinant3(m);

				if (std::isfinite(det) && det != 0.0) {
					info.flipWinding = det < 0.0;

					break;
				}
			}

			if (spread > 1e-3) {
				this->WarnOnce(
					"bindspread:" + std::to_string(skinIndex),
					"The skin '" + skin.name + "': the rest pose of the nodes differs from the bind pose (inverse bind matrices) differently for different joints; the model is exported in the rest pose, and animations may deviate from glTF."
				);
			}

			return info;
		}

		const Variant &Converter::EnsureVariant(int meshIndex, int skinIndex, bool flipWinding, int bakeNode)
		{
			const auto key = std::make_tuple(meshIndex, skinIndex, flipWinding, bakeNode);
			const auto found = this->variants.find(key);

			if (found != this->variants.end()) {
				return found->second;
			}

			Variant variant = this->BuildVariant(meshIndex, skinIndex, flipWinding, bakeNode);

			return this->variants.emplace(key, std::move(variant)).first->second;
		}

		Variant Converter::BuildVariant(int meshIndex, int skinIndex, bool flipWinding, int bakeNode)
		{
			const Gltf::Mesh &mesh = this->scene.meshes[static_cast<size_t>(meshIndex)];
			const bool skinned = skinIndex >= 0;
			const SkinInfo *skin = skinned ? &this->EnsureSkin(skinIndex) : nullptr;
			const Gltf::Skin *skinSource = skinned ? &this->scene.skins[static_cast<size_t>(skinIndex)] : nullptr;
			const std::string meshName = mesh.name.empty() ? "Mesh_" + std::to_string(meshIndex) : TextUtil::ToUtf8(mesh.name);

			if (skinned) {
				flipWinding = skin->flipWinding;
			}

			/// Primitiv připravený k zápisu
			struct Item {
				const Gltf::Primitive *source;
				Prepared prepared;
			};

			std::vector<Item> items;

			for (const Gltf::Primitive &primitive : mesh.primitives) {

				if (primitive.triangles.size() >= 3 && !primitive.positions.empty()) {
					items.push_back({&primitive, this->Prepare(primitive)});
				}
			}

			Variant variant;

			if (items.empty()) {
				return variant;
			}

			variant.valid = true;
			variant.skinned = skinned;

			// Rozložení vrcholu a materiály
			bool hasColor = false;
			bool hasUv = false;

			for (const Item &item : items) {
				hasColor = hasColor || !item.source->colors.empty();
				hasUv = hasUv || !item.source->texCoords.empty();
			}

			std::vector<uint32_t> materialIds;

			for (const Item &item : items) {
				materialIds.push_back(this->EnsureMaterial(item.source->material, skinned, hasColor));
			}

			bool anyTexture = false;
			bool needTangents = false;

			for (const uint32_t id : materialIds) {
				const SdkMesh::Material &material = this->result.model.materials[id];
				anyTexture = anyTexture || !material.diffuseTexture.empty() || !material.normalTexture.empty() || !material.albedoTexture.empty() || !material.rmaTexture.empty() ||
							 !material.emissiveTexture.empty();
				needTangents = needTangents || !material.normalTexture.empty();
			}

			// Skinované efekty DirectXTK (SkinnedEffect, SkinnedNormalMapEffect) čtou TEXCOORD0 vždy, i bez textury; bez něj by se nevytvořil vstupní layout
			const Layout layout = MakeLayout(skinned, hasColor, skinned || anyTexture || hasUv, needTangents);

			size_t totalVertices = 0;
			size_t totalIndices = 0;

			for (const Item &item : items) {
				totalVertices += item.prepared.vertexCount;
				totalIndices += item.prepared.triangles.size();
			}

			if (totalVertices > std::numeric_limits<uint32_t>::max() || static_cast<uint64_t>(totalVertices) * layout.stride > std::numeric_limits<uint32_t>::max()) {
				throw std::runtime_error("The mesh '" + meshName + "' is too large for SDKMESH (the vertex buffer would be over 4 GB)");
			}

			// --- Skinning: vybrat používané klouby, rozdělit váhy na 8bitové a spočítat indexy kostí ----------------------------
			std::vector<std::vector<uint32_t>> slots(items.size());		  // 4 sloty kloubu ve skinu na vrchol
			std::vector<std::vector<double>> normalized(items.size());	  // 4 normalizované váhy na vrchol
			std::vector<std::vector<uint8_t>> quantized(items.size());	  // 4 váhy 0 - 255 na vrchol
			std::vector<uint32_t> boneOfSlot;

			if (skinned) {
				std::set<uint32_t> used;
				bool missing = false, invalid = false;

				for (size_t i = 0; i < items.size(); ++i) {
					const Prepared &p = items[i].prepared;
					slots[i].assign(p.vertexCount * 4, 0);
					normalized[i].assign(p.vertexCount * 4, 0.0);
					quantized[i].assign(p.vertexCount * 4, 0);
					const bool has = p.joints.size() == p.vertexCount * 4 && p.weights.size() == p.vertexCount * 4;
					missing = missing || !has;

					for (size_t v = 0; v < p.vertexCount; ++v) {
						double w[4] = {1.0, 0.0, 0.0, 0.0};
						uint32_t j[4] = {0, 0, 0, 0};

						if (has) {
							double sum = 0.0;

							for (size_t k = 0; k < 4; ++k) {
								j[k] = p.joints[v * 4 + k];
								w[k] = p.weights[v * 4 + k];

								if (!std::isfinite(w[k]) || w[k] < 0.0) {
									w[k] = 0.0;
								}

								if (j[k] >= skinSource->joints.size()) {
									invalid = invalid || w[k] > 0.0;
									w[k] = 0.0;
									j[k] = 0;
								}

								sum += w[k];
							}

							if (sum > 0.0) {

								for (size_t k = 0; k < 4; ++k) {
									w[k] /= sum;
								}
							}
							else {
								// Vrchol bez vah: patří prvnímu kloubu (a slot 0, pokud je i ten neplatný)
								w[0] = 1.0;
								w[1] = w[2] = w[3] = 0.0;
								j[1] = j[2] = j[3] = 0;
							}
						}

						uint8_t q[4];
						QuantizeWeights(w, q);

						for (size_t k = 0; k < 4; ++k) {
							slots[i][v * 4 + k] = j[k];
							normalized[i][v * 4 + k] = w[k];
							quantized[i][v * 4 + k] = q[k];

							if (q[k] > 0) {
								used.insert(j[k]);
							}
						}
					}
				}

				if (missing) {
					this->WarnOnce("noweights:" + meshName, "The mesh '" + meshName + "' has primitives without joints or weights; their vertices are bound to the first joint.");
				}

				if (invalid) {
					this->WarnOnce("badjoint:" + meshName, "The mesh '" + meshName + "' has vertices that refer to joints outside of the skin; those influences were dropped.");
				}

				if (used.size() > MAX_INFLUENCES) {
					throw std::runtime_error(
						"The mesh '" + meshName + "' is influenced by " + std::to_string(used.size()) + " joints; SDKMESH stores bone indices in a byte, so at most " + std::to_string(MAX_INFLUENCES) + " are possible"
					);
				}

				if (used.size() > EFFECT_MAX_BONES) {
					this->WarnOnce(
						"manybones:" + meshName,
						"The mesh '" + meshName + "' is influenced by " + std::to_string(used.size()) + " joints; the skinned effects of DirectXTK support at most " + std::to_string(EFFECT_MAX_BONES) +
							" bones per mesh, so Model::DrawSkinned refuses to draw it."
					);
				}

				boneOfSlot.assign(skinSource->joints.size(), 0);

				for (const uint32_t slot : used) {
					boneOfSlot[slot] = static_cast<uint32_t>(variant.influences.size());
					variant.influences.push_back(static_cast<uint32_t>(this->frameOfNode[static_cast<size_t>(skinSource->joints[slot])]));
				}
			}

			// --- Vrcholy --------------------------------------------------------------------------------------------------
			SdkMesh::VertexBuffer vb;
			vb.decl = layout.decl;
			vb.stride = layout.stride;
			vb.vertexCount = totalVertices;
			vb.data.assign(totalVertices * layout.stride, 0);

			const bool mirror = this->options.leftHanded;
			const Mat4 *bake = bakeNode >= 0 ? &this->world[static_cast<size_t>(bakeNode)] : nullptr; // zapékaná světová matice uzlu (v souřadnicích glTF)
			const Mat4 bakeNormal = bake ? MathUtil::NormalMatrix(*bake) : MathUtil::Identity();
			ExportUtil::Bounds bounds;
			size_t base = 0;

			for (size_t i = 0; i < items.size(); ++i) {
				const Prepared &p = items[i].prepared;
				const Gltf::Material &material = this->SourceMaterial(items[i].source->material);
				const Gltf::TextureRef *uvSource = this->UvTransformSource(material);
				const ExportUtil::UvTransform uv = uvSource ? ExportUtil::MakeUvTransform(*uvSource) : ExportUtil::UvTransform();
				const double *uvMatrix = uv.m; // u' = a u + b v + c, v' = d u + e v + f

				for (size_t v = 0; v < p.vertexCount; ++v) {
					uint8_t *dst = &vb.data[(base + v) * layout.stride];
					const auto put = [&](size_t at, const void *src, size_t bytes) { std::memcpy(dst + at, src, bytes); };
					const auto putVec3 = [&](size_t at, const Vec3 &value) {
						float f[3];
						MathUtil::ToFloats(value, f);
						put(at, f, sizeof(f));
					};

					Vec3 position = ExportUtil::Vec3At(p.positions, v, 3);
					Vec3 normal = ExportUtil::Vec3At(p.normals, v, 3);
					Vec3 tangent, binormal;

					if (layout.tangents) {
						tangent = ExportUtil::Vec3At(p.tangents, v, 4);
						const Vec3 bitangent = MathUtil::Cross(normal, tangent);
						const double w = p.tangents[v * 4 + 3] < 0.0f ? -1.0 : 1.0;
						binormal = {bitangent.x * w, bitangent.y * w, bitangent.z * w};
					}

					if (skinned && !skin->identity) {
						// Klidová póza do vrcholů: Σ w * (světová matice kloubu * inverzní bind matice)
						Mat4 blended {};

						for (size_t k = 0; k < 4; ++k) {
							const double w = normalized[i][v * 4 + k];

							if (w <= 0.0) {
								continue;
							}

							const Mat4 &m = skin->bind[slots[i][v * 4 + k]];

							for (size_t e = 0; e < 16; ++e) {
								blended[e] += w * m[e];
							}
						}

						const Mat4 normalMatrix = MathUtil::NormalMatrix(blended);
						position = MathUtil::TransformPoint(blended, position);
						normal = MathUtil::Normalize(MathUtil::TransformVector(normalMatrix, normal));

						if (layout.tangents) {
							tangent = MathUtil::TransformVector(blended, tangent);
							binormal = MathUtil::TransformVector(blended, binormal);
						}
					}
					else if (bake) {
						// Transformace uzlu do vrcholů: mesh pak visí na snímku s jednotkovou maticí
						position = MathUtil::TransformPoint(*bake, position);
						normal = MathUtil::Normalize(MathUtil::TransformVector(bakeNormal, normal));

						if (layout.tangents) {
							tangent = MathUtil::TransformVector(*bake, tangent);
							binormal = MathUtil::TransformVector(*bake, binormal);
						}
					}

					if (layout.tangents) {
						tangent = MathUtil::Normalize(tangent);
						binormal = MathUtil::Normalize(binormal);
					}

					if (mirror) {
						position = MathUtil::MirrorZ(position);
						normal = MathUtil::MirrorZ(normal);
						tangent = MathUtil::MirrorZ(tangent);
						binormal = MathUtil::MirrorZ(binormal);
					}

					float pos[3];
					MathUtil::ToFloats(position, pos);
					put(0, pos, sizeof(pos));
					putVec3(layout.offsetNormal, normal);

					// Obálka z hodnot, které se skutečně zapíšou (po zaokrouhlení na float)
					bounds.Add({pos[0], pos[1], pos[2]});

					if (skinned) {
						uint8_t indices[4] = {0, 0, 0, 0};
						uint8_t weights[4] = {0, 0, 0, 0};

						for (size_t k = 0; k < 4; ++k) {
							weights[k] = quantized[i][v * 4 + k];
							indices[k] = weights[k] > 0 ? static_cast<uint8_t>(boneOfSlot[slots[i][v * 4 + k]]) : 0;
						}

						put(layout.offsetWeights, weights, 4);
						put(layout.offsetIndices, indices, 4);
					}

					if (layout.color) {
						// D3DCOLOR: bajty v pořadí B, G, R, A
						const uint32_t rgba = items[i].source->colors.empty() ? 0xFFFFFFFFu : p.colors[v];
						const uint8_t bgra[4] = {static_cast<uint8_t>(rgba >> 16), static_cast<uint8_t>(rgba >> 8), static_cast<uint8_t>(rgba), static_cast<uint8_t>(rgba >> 24)};
						put(layout.offsetColor, bgra, 4);
					}

					if (layout.texcoord) {
						float uv[2] = {0.0f, 0.0f};

						if (!p.texCoords.empty()) {
							const double u = p.texCoords[v * 2], w = p.texCoords[v * 2 + 1];
							uv[0] = static_cast<float>(uvMatrix[0] * u + uvMatrix[1] * w + uvMatrix[2]);
							uv[1] = static_cast<float>(uvMatrix[3] * u + uvMatrix[4] * w + uvMatrix[5]);
						}

						put(layout.offsetTexcoord, uv, sizeof(uv));
					}

					if (layout.tangents) {
						putVec3(layout.offsetTangent, tangent);
						putVec3(layout.offsetBinormal, binormal);
					}
				}

				base += p.vertexCount;
			}

			// --- Indexy a podmnožiny ----------------------------------------------------------------------------------------
			SdkMesh::IndexBuffer ib;
			ib.wide = totalVertices > 65536;
			ib.indices.reserve(totalIndices);
			base = 0;

			std::vector<SdkMesh::Subset> subsets;

			for (size_t i = 0; i < items.size(); ++i) {
				const Prepared &p = items[i].prepared;
				SdkMesh::Subset subset;
				subset.name = TruncateUtf8("subset" + std::to_string(i) + "_" + this->result.model.materials[materialIds[i]].name, SdkMesh::MAX_SUBSET_NAME - 1);
				subset.materialId = materialIds[i];
				subset.primitiveType = SdkMesh::PT_TRIANGLE_LIST;
				subset.indexStart = ib.indices.size();
				subset.indexCount = p.triangles.size();
				subset.vertexStart = 0;
				subset.vertexCount = totalVertices;

				// Vinutí po směru hodin: prohodit druhý a třetí index (viz začátek souboru)
				for (size_t t = 0; t + 2 < p.triangles.size(); t += 3) {
					const uint32_t a = static_cast<uint32_t>(base + p.triangles[t]);
					const uint32_t b = static_cast<uint32_t>(base + p.triangles[t + 1]);
					const uint32_t c = static_cast<uint32_t>(base + p.triangles[t + 2]);
					ib.indices.push_back(a);
					ib.indices.push_back(flipWinding ? b : c);
					ib.indices.push_back(flipWinding ? c : b);
				}

				subsets.push_back(std::move(subset));
				base += p.vertexCount;
			}

			if (static_cast<uint64_t>(vb.data.size()) > 128ull * 1024 * 1024 || static_cast<uint64_t>(ib.indices.size()) * (ib.wide ? 4 : 2) > 128ull * 1024 * 1024) {
				this->WarnOnce("large:" + meshName, "The mesh '" + meshName + "' has a buffer over 128 MB; DirectXTK loads it only with ModelLoader_AllowLargeModels.");
			}

			float center[3], extent[3];
			MathUtil::ToFloats(bounds.Center(), center);
			MathUtil::ToFloats(bounds.HalfExtents(), extent);
			variant.center = {center[0], center[1], center[2]};
			variant.extents = {extent[0], extent[1], extent[2]};

			variant.vertexBuffer = static_cast<uint32_t>(this->result.model.vertexBuffers.size());
			variant.indexBuffer = static_cast<uint32_t>(this->result.model.indexBuffers.size());
			variant.vertices = totalVertices;
			variant.triangles = totalIndices / 3;
			this->result.model.vertexBuffers.push_back(std::move(vb));
			this->result.model.indexBuffers.push_back(std::move(ib));

			for (SdkMesh::Subset &subset : subsets) {
				variant.subsets.push_back(static_cast<uint32_t>(this->result.model.subsets.size()));
				this->result.model.subsets.push_back(std::move(subset));
			}

			++this->result.stats.vertexBuffers;
			this->result.stats.subsets += variant.subsets.size();
			this->result.stats.vertices += totalVertices;

			return variant;
		}

		void Converter::AddInstance(int nodeIndex)
		{
			const Gltf::Node &node = this->scene.nodes[static_cast<size_t>(nodeIndex)];

			if (node.mesh < 0 || !this->HasTriangles(this->scene.meshes[static_cast<size_t>(node.mesh)])) {
				return;
			}

			const bool skinned = this->IsSkinnedNode(node);

			// Uzel se zápornou determinantou (zrcadlení) obrací orientaci ploch sám, proto se pro něj nesmí prohazovat indexy
			// Totéž platí pro zapečenou instanci: vrcholy už jsou převedené, takže orientaci obrátila jejich transformace
			const bool flipWinding = !skinned && MathUtil::Determinant3(this->world[static_cast<size_t>(nodeIndex)]) < 0.0;
			const bool baked = !skinned && this->bakedNode[static_cast<size_t>(nodeIndex)];
			const Variant &variant = this->EnsureVariant(node.mesh, skinned ? node.skin : -1, flipWinding, baked ? nodeIndex : -1);

			if (!variant.valid) {
				return;
			}

			const Gltf::Mesh &source = this->scene.meshes[static_cast<size_t>(node.mesh)];
			std::string wanted = source.name.empty() ? (node.name.empty() ? "Mesh_" + std::to_string(node.mesh) : node.name) : source.name;

			SdkMesh::Mesh mesh;
			mesh.name = UniqueName(this->usedMeshNames, TextUtil::ToUtf8(wanted), SdkMesh::MAX_MESH_NAME - 1);
			mesh.vertexBuffer = variant.vertexBuffer;
			mesh.indexBuffer = variant.indexBuffer;
			mesh.subsets = variant.subsets;
			mesh.influences = variant.influences;
			mesh.boundingBoxCenter = variant.center;
			mesh.boundingBoxExtents = variant.extents;

			const int frame = (skinned || baked) ? this->holderOfNode[static_cast<size_t>(nodeIndex)] : this->frameOfNode[static_cast<size_t>(nodeIndex)];

			if (frame < 0) {
				throw std::runtime_error("Internal error: the node '" + node.name + "' has no frame");
			}

			this->result.model.frames[static_cast<size_t>(frame)].mesh = static_cast<uint32_t>(this->result.model.meshes.size());
			this->result.model.meshes.push_back(std::move(mesh));

			++this->result.stats.meshes;
			this->result.stats.skinnedMeshes += skinned ? 1 : 0;
			this->result.stats.bakedMeshes += baked ? 1 : 0;
			this->result.stats.triangles += variant.triangles;
		}

		Result Converter::Run()
		{
			if (this->options.animationFps == 0 || this->options.animationFps > 1000) {
				throw std::runtime_error("The animation FPS must be between 1 and 1000");
			}

			this->result.model.version = this->options.pbr ? SdkMesh::FILE_VERSION_V2 : SdkMesh::FILE_VERSION;
			this->ComputeWorld();
			this->skins.assign(this->scene.skins.size(), SkinInfo());
			this->BuildFrames();

			// Instance meshů v pořadí snímků (do hloubky)
			for (size_t f = 0; f < this->nodeOfFrame.size(); ++f) {

				if (this->nodeOfFrame[f] >= 0 && this->scene.nodes[static_cast<size_t>(this->nodeOfFrame[f])].mesh >= 0) {
					this->AddInstance(this->nodeOfFrame[f]);
				}
			}

			if (this->result.model.meshes.empty()) {
				throw std::runtime_error("The scene contains no triangle geometry that can be exported");
			}

			if (this->unbakedAnimated > 0) {
				this->WarnOnce(
					"bake-animated",
					std::to_string(this->unbakedAnimated) + " meshes are under animated nodes and were not baked into world coordinates (they keep their frame transforms, so a viewer that ignores the frames will misplace them); use --no-anim to export without animations and bake them too."
				);
			}

			if (!this->options.noAnimations) {
				this->BuildAnimations();
			}

			this->WarnGeometryLimits("SDKMESH");

			this->result.stats.frames = this->result.model.frames.size();
			this->result.stats.materials = this->result.model.materials.size();

			this->result.textures = std::move(this->textures);

			return std::move(this->result);
		}
	}

	Result Convert(const Gltf::Scene &scene, const Options &options)
	{
		Converter converter(scene, options);

		return converter.Run();
	}
}
