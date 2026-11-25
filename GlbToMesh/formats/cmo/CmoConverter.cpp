/**
 * @file      CmoConverter.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace převodu glTF -> CMO (viz CmoConverter.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Souřadnice: glTF je pravotočivé, stejně jako CMO z Visual Studia (viz skutečný soubor teapot.cmo), takže se pozice a normály
 * kopírují beze změny. Volba leftHanded zrcadlí osu Z (pozice, normály, tangenty a matice jako S * M * S pro S = diag(1, 1, -1)).
 * Vinutí trojúhelníků: glTF má přední strany proti směru hodin, Visual Studio zapisuje trojúhelníky po směru hodin (výchozí stav
 * rasterizéru Direct3D; vektorový součin hran míří proti normálám), proto se u každého trojúhelníku prohodí druhý a třetí index.
 * Totéž platí pro levotočivý výstup, kde zrcadlení orientaci ploch otáčí a výsledek musí mít součin souhlasný s normálami.
 * Uzel se zápornou osou měřítka (zrcadlení) orientaci otáčí také, takže tam se indexy neprohazují.
 *
 * Statické meshe: CMO nemá hierarchii uzlů, proto se světová matice uzlu zapeče do vrcholů. Skinované meshe zůstávají v klidové
 * poloze (transformace jejich uzlu se podle specifikace glTF ignoruje); kostra CMO vznikne z kloubů skinu. Uzly mezi klouby
 * (a uzly nad kořenem skeletu) se složí do lokální matice kosti, takže světové matice kostí odpovídají glTF.
 *
 * Matice v CMO jsou řádkové (v' = v * M) s posunem v poslední řádce; jejich paměťové rozložení je shodné s maticí glTF (po
 * sloupcích, v' = M * v), proto se při zápisu netransponují (viz CmoFormat.h).
 *
 * Animace: klip CMO obsahuje pro každou kost klíče s plnou lokální maticí. Klíče vznikají vyhodnocením kanálů glTF (posun,
 * rotace, měřítko) v čase všech klíčů kanálů, které kost ovlivňují. Interpolace STEP se doplní klíčem těsně před dalším
 * klíčem, CUBICSPLINE se vzorkuje v mezilehlých časech. Čas klipu začíná nulou.
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

// Hlavičkové soubory projektu
#include "CmoConverter.h"

#include "core/ExportUtil.h"
#include "core/Log.h"
#include "core/MathUtil.h"
#include "core/TextUtil.h"

namespace CmoConvert {

	namespace {

		using ExportUtil::NodeAnimation;
		using ExportUtil::Prepared;
		using ExportUtil::Trs;
		using MathUtil::Mat4;
		using MathUtil::Vec3;

		/// Nejvyšší počet vrcholů jednoho vertex bufferu (indexy jsou 16bitové a 0xFFFF označuje odebraný trojúhelník)
		constexpr size_t MAX_BUFFER_VERTICES = 65535;

		/// Počet úseků, na které se dělí každý úsek kubické interpolace při vzorkování
		constexpr size_t CUBIC_SUBDIVISIONS = 4;

		using MathUtil::ToFloats;

		/// Kostra jednoho skinu v podobě, jak půjde do CMO
		struct SkinData {
			bool built = false;
			std::vector<int> boneNodes;				// uzel každé kosti (v pořadí v CMO)
			std::vector<std::vector<int>> chains;	// uzly od potomka předchozího kloubu po uzel kosti (shora dolů)
			std::vector<int> jointToBone;			// slot kloubu ve skinu -> index kosti
			std::vector<Cmo::Bone> bones;
			std::vector<Cmo::Clip> clips;
			bool clipsGiven = false; // klipy se zapisují jen do prvního meshe skinu
		};

		/// Převodník scény na model CMO (pravidla převodu jsou popsána v záhlaví souboru)
		class Converter : public ExportUtil::SceneConverter
		{
		  public:
			/// @param scene Načtená scéna (musí přežít převodník)
			/// @param options Volby převodu (musí přežít převodník)
			Converter(const Gltf::Scene &scene, const Options &options) : SceneConverter(scene, options), options(options)
			{
			}

			/// Převede celou scénu a vrátí model CMO se soubory textur a statistikou
			Result Run();

		  private:
			const Options &options;
			Result result;

			std::vector<bool> nodeAnimated;
			std::vector<SkinData> skins;
			std::set<std::string> usedMeshNames;
			std::set<int> animatedChainNodes; // uzly, které animují kosti některého použitého skinu
			size_t meshCounter = 0;

			// Textury a materiály
			/// Převede materiál glTF na materiál CMO a zajistí soubory jeho textur
			Cmo::Material MakeMaterial(const Gltf::Material &src, const std::string &name);

			// Kostry a animace
			/// Při prvním použití postaví kostru skinu: kosti z kloubů, jejich hierarchii a animační klipy
			SkinData &EnsureSkin(size_t skinIndex);
			/// Vytvoří animační klipy kostí skinu vzorkováním kanálů animací glTF
			void BuildClips(size_t skinIndex, SkinData &data);
			Mat4 SampleChain(const std::vector<int> &chain, const std::unordered_map<int, NodeAnimation> &animation, double time) const;
			void CollectChannelTimes(const Gltf::Channel &channel, std::vector<float> &times) const;

			// Geometrie
			/// Přidá mesh uzlu do modelu: statický se zapeče do světových souřadnic, skinovaný zůstane v klidové poloze
			void AddInstance(int nodeIndex);
		};

		// ---------------------------------------------------------------------------------------------------
		// Textury a materiály

		Cmo::Material Converter::MakeMaterial(const Gltf::Material &src, const std::string &name)
		{
			Cmo::Material out;
			out.name = name;
			out.pixelShader = this->options.pixelShader;

			// Průhlednost: jen režim BLEND; MASK (ořez podle alfa) CMO nezná. Bez odlesku zapisuje CMO exponent 1.
			const float alpha = this->MaterialAlpha(src, name, name, "CMO");
			const ExportUtil::PhongMaterial phong = ExportUtil::MakePhongMaterial(src, alpha, this->options.srgbColors, 1.0f);

			Cmo::MaterialRecord &m = out.record;
			std::copy_n(phong.diffuse, 4, m.diffuse);
			std::copy_n(phong.ambient, 4, m.ambient);
			std::copy_n(phong.specular, 4, m.specular);
			std::copy_n(phong.emissive, 4, m.emissive);
			m.specularPower = phong.power;

			for (size_t i = 0; i < 16; ++i) {
				m.uvTransform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
			}

			// Textury: 0 = difuzní, 2 = normálová, 3 = emisivní
			out.textures[0] = this->TextureFile(src.baseColorTexture.image, name);
			out.textures[2] = this->TextureFile(src.normalTexture.image, name);
			out.textures[3] = this->TextureFile(src.emissiveTexture.image, name);

			this->WarnUnsupportedPbrTextures(src, name, name, "CMO");

			// Transformace UV: CMO má jednu na materiál, bere se z první textury, která ji má
			const Gltf::TextureRef *transformSource = this->PickUvTransform({&src.baseColorTexture, &src.normalTexture, &src.emissiveTexture}, nullptr, name, "CMO has one per material");

			if (transformSource) {
				// Matice UV v CMO je řádková (u', v') = (u, v, 1) * M: sloupec výsledku u' leží v prvcích 0, 4 a 12, v' v prvcích 1, 5 a 13
				const ExportUtil::UvTransform t = ExportUtil::MakeUvTransform(*transformSource);
				m.uvTransform[0] = static_cast<float>(t.m[0]);
				m.uvTransform[1] = static_cast<float>(t.m[3]);
				m.uvTransform[4] = static_cast<float>(t.m[1]);
				m.uvTransform[5] = static_cast<float>(t.m[4]);
				m.uvTransform[12] = static_cast<float>(t.m[2]);
				m.uvTransform[13] = static_cast<float>(t.m[5]);
			}

			this->WarnTexCoordSet({&src.baseColorTexture, &src.normalTexture, &src.emissiveTexture}, name, name, "CMO has just one");
			NoteUnlit(src, name);

			return out;
		}

		// ---------------------------------------------------------------------------------------------------
		// Animace

		/// Časy, ve kterých je potřeba kanál vyhodnotit: všechny klíče a navíc doplňkové časy pro STEP a CUBICSPLINE
		void Converter::CollectChannelTimes(const Gltf::Channel &channel, std::vector<float> &times) const
		{
			for (size_t k = 0; k < channel.times.size(); ++k) {
				times.push_back(channel.times[k]);

				if (k + 1 >= channel.times.size() || !(channel.times[k + 1] > channel.times[k])) {
					continue;
				}

				if (channel.interpolation == Gltf::Interpolation::Step) {
					// Těsně před dalším klíčem platí ještě stará hodnota
					const float before = std::nextafter(channel.times[k + 1], channel.times[k]);

					if (before > channel.times[k]) {
						times.push_back(before);
					}
				}
				else if (channel.interpolation == Gltf::Interpolation::CubicSpline) {

					for (size_t i = 1; i < CUBIC_SUBDIVISIONS; ++i) {
						times.push_back(channel.times[k] + (channel.times[k + 1] - channel.times[k]) * static_cast<float>(i) / static_cast<float>(CUBIC_SUBDIVISIONS));
					}
				}
			}
		}

		/// Lokální matice kosti v čase: součin lokálních matic uzlů jejího řetězce
		Mat4 Converter::SampleChain(const std::vector<int> &chain, const std::unordered_map<int, NodeAnimation> &animation, double time) const
		{
			Mat4 combined = MathUtil::Identity();

			for (const int node : chain) {
				const Trs trs = this->SampleNode(node, animation, time);
				combined = MathUtil::Multiply(combined, MathUtil::Compose(trs.t, trs.q, trs.s));
			}

			return combined;
		}

		void Converter::BuildClips(size_t skinIndex, SkinData &data)
		{
			(void)skinIndex;

			if (this->options.noAnimations) {
				return;
			}

			for (size_t a = 0; a < this->scene.animations.size(); ++a) {
				const Gltf::Animation &animation = this->scene.animations[a];

				// Uzly kostí tohoto skinu, které animace ovlivňuje
				std::set<int> chainNodes;

				for (const auto &chain : data.chains) {
					chainNodes.insert(chain.begin(), chain.end());
				}

				std::unordered_map<int, NodeAnimation> byNode;
				double minTime = std::numeric_limits<double>::max(), maxTime = std::numeric_limits<double>::lowest();

				for (const Gltf::Channel &channel : animation.channels) {

					if (chainNodes.find(channel.node) == chainNodes.end()) {
						continue;
					}

					byNode[channel.node].Slot(channel.path) = &channel;
					minTime = std::min(minTime, static_cast<double>(channel.times.front()));
					maxTime = std::max(maxTime, static_cast<double>(channel.times.back()));
					this->animatedChainNodes.insert(channel.node);
				}

				if (byNode.empty()) {
					continue;
				}

				Cmo::Clip clip;
				clip.name = ExportUtil::ClipName(animation, a);
				clip.record.startTime = 0.0f;
				clip.record.endTime = static_cast<float>(maxTime - minTime);

				for (size_t b = 0; b < data.bones.size(); ++b) {
					const std::vector<int> &chain = data.chains[b];
					std::vector<float> times;
					bool animated = false;

					for (const int node : chain) {
						const auto found = byNode.find(node);

						if (found == byNode.end()) {
							continue;
						}

						animated = true;

						for (const Gltf::Channel *channel : {found->second.translation, found->second.rotation, found->second.scale}) {

							if (channel) {
								this->CollectChannelTimes(*channel, times);
							}
						}
					}

					// Každá kost má klíče v celém rozsahu klipu, i když se nehýbe
					times.push_back(static_cast<float>(minTime));
					times.push_back(static_cast<float>(maxTime));
					std::sort(times.begin(), times.end());
					times.erase(std::unique(times.begin(), times.end()), times.end());

					if (!animated) {
						times = {times.front(), times.back()};
						times.erase(std::unique(times.begin(), times.end()), times.end());
					}

					for (const float time : times) {

						if (time < minTime || time > maxTime) {
							continue;
						}

						Cmo::KeyframeRecord key {};
						key.boneIndex = static_cast<uint32_t>(b);
						key.time = static_cast<float>(static_cast<double>(time) - minTime);
						ToFloats(this->Mirror(this->SampleChain(chain, byNode, time)), key.transform);
						clip.keys.push_back(key);
					}
				}

				// Klíče podle času, při shodě podle kosti (jako je zapisuje Visual Studio)
				std::stable_sort(clip.keys.begin(), clip.keys.end(), [](const Cmo::KeyframeRecord &x, const Cmo::KeyframeRecord &y) { return x.time < y.time; });

				data.clips.push_back(std::move(clip));
			}
		}

		// ---------------------------------------------------------------------------------------------------
		// Kostry

		SkinData &Converter::EnsureSkin(size_t skinIndex)
		{
			SkinData &data = this->skins[skinIndex];

			if (data.built) {
				return data;
			}

			data.built = true;
			const Gltf::Skin &skin = this->scene.skins[skinIndex];

			if (skin.joints.empty()) {
				throw std::runtime_error("Skin '" + skin.name + "' has no joints");
			}

			if (skin.joints.size() > 65535) {
				throw std::runtime_error("Skin '" + skin.name + "' has " + std::to_string(skin.joints.size()) + " joints; at most 65535 are supported");
			}

			if (skin.joints.size() > 72) {
				this->WarnOnce("bones:" + std::to_string(skinIndex), "Skin '" + skin.name + "' has " + std::to_string(skin.joints.size()) + " joints; the DirectXTK skinned effects support at most 72 bones.");
			}

			// Jedinečné uzly kloubů (stejný uzel ve skinu dvakrát vznikne jako jedna kost)
			std::vector<int> nodes;
			std::vector<size_t> firstSlot;
			std::map<int, size_t> unique;
			std::vector<size_t> slotToUnique(skin.joints.size());

			for (size_t j = 0; j < skin.joints.size(); ++j) {
				const auto found = unique.find(skin.joints[j]);

				if (found == unique.end()) {
					unique[skin.joints[j]] = nodes.size();
					slotToUnique[j] = nodes.size();
					nodes.push_back(skin.joints[j]);
					firstSlot.push_back(j);
				}
				else {
					slotToUnique[j] = found->second;
				}
			}

			// Rodič kosti = nejbližší předek, který je také kloubem; uzly mezi nimi tvoří řetězec složený do lokální matice
			const size_t count = nodes.size();
			std::vector<int> parent(count, -1);
			std::vector<std::vector<int>> chains(count);

			for (size_t i = 0; i < count; ++i) {
				std::vector<int> chain {nodes[i]};
				int ancestor = this->scene.nodes[static_cast<size_t>(nodes[i])].parent;

				while (ancestor >= 0 && unique.find(ancestor) == unique.end()) {
					chain.push_back(ancestor);
					ancestor = this->scene.nodes[static_cast<size_t>(ancestor)].parent;
				}

				std::reverse(chain.begin(), chain.end());
				chains[i] = std::move(chain);
				parent[i] = ancestor >= 0 ? static_cast<int>(unique[ancestor]) : -1;
			}

			// Pořadí kostí: rodič před potomkem a první kost je kořen (to vyžaduje i Model::CreateFromCMO)
			std::vector<size_t> order;
			bool valid = parent[0] < 0;

			for (size_t i = 0; valid && i < count; ++i) {
				valid = parent[i] < static_cast<int>(i);
			}

			if (valid) {
				order.resize(count);

				for (size_t i = 0; i < count; ++i) {
					order[i] = i;
				}
			}
			else {
				std::vector<std::vector<size_t>> children(count);

				for (size_t i = 0; i < count; ++i) {

					if (parent[i] >= 0) {
						children[static_cast<size_t>(parent[i])].push_back(i);
					}
				}

				std::vector<size_t> stack;

				for (size_t i = count; i-- > 0;) {

					if (parent[i] < 0) {
						stack.push_back(i);
					}
				}

				while (!stack.empty()) {
					const size_t current = stack.back();
					stack.pop_back();
					order.push_back(current);

					for (size_t k = children[current].size(); k-- > 0;) {
						stack.push_back(children[current][k]);
					}
				}

				Log::Detail("Skin '" + skin.name + "': the bones were reordered so that every parent comes before its children.");
			}

			if (order.size() != count) {
				throw std::runtime_error("Skin '" + skin.name + "': the joints do not form a valid hierarchy");
			}

			std::vector<size_t> newIndex(count);

			for (size_t i = 0; i < count; ++i) {
				newIndex[order[i]] = i;
			}

			data.jointToBone.resize(skin.joints.size());

			for (size_t j = 0; j < skin.joints.size(); ++j) {
				data.jointToBone[j] = static_cast<int>(newIndex[slotToUnique[j]]);
			}

			// Záznamy kostí
			std::vector<Mat4> worldRest(count);
			data.bones.resize(count);
			data.boneNodes.resize(count);
			data.chains.resize(count);

			for (size_t i = 0; i < count; ++i) {
				const size_t old = order[i];
				Mat4 local = MathUtil::Identity();

				for (const int node : chains[old]) {
					local = MathUtil::Multiply(local, this->scene.nodes[static_cast<size_t>(node)].local);
				}

				local = this->Mirror(local);
				const int parentBone = parent[old] < 0 ? -1 : static_cast<int>(newIndex[static_cast<size_t>(parent[old])]);
				worldRest[i] = parentBone < 0 ? local : MathUtil::Multiply(worldRest[static_cast<size_t>(parentBone)], local);

				const Mat4 invBind = this->Mirror(skin.inverseBind[firstSlot[old]]);

				if (!MathUtil::IsFinite(invBind)) {
					throw std::runtime_error("Skin '" + skin.name + "' has an inverse bind matrix that is not finite");
				}

				Cmo::Bone &bone = data.bones[i];
				const std::string &nodeName = this->scene.nodes[static_cast<size_t>(nodes[old])].name;
				bone.name = nodeName.empty() ? "Bone_" + std::to_string(i) : nodeName;
				bone.record.parentIndex = parentBone;
				ToFloats(invBind, bone.record.invBindPos);
				ToFloats(worldRest[i], bone.record.bindPos);
				ToFloats(local, bone.record.localTransform);

				data.boneNodes[i] = nodes[old];
				data.chains[i] = chains[old];
			}

			this->BuildClips(skinIndex, data);

			return data;
		}

		// ---------------------------------------------------------------------------------------------------
		// Geometrie

		void Converter::AddInstance(int nodeIndex)
		{
			const Gltf::Node &node = this->scene.nodes[static_cast<size_t>(nodeIndex)];
			const Gltf::Mesh &mesh = this->scene.meshes[static_cast<size_t>(node.mesh)];

			if (mesh.primitives.empty()) {
				Log::Detail("Node '" + node.name + "': the mesh has no usable triangles - skipped.");

				return;
			}

			const bool skinned = node.skin >= 0;
			const std::string label = !node.name.empty() ? node.name : (!mesh.name.empty() ? mesh.name : "Mesh_" + std::to_string(this->meshCounter));
			++this->meshCounter;

			Cmo::Mesh out;
			// Název meshe se ve výstupu neopakuje (případně s příponou _2, _3, ...); CMO rozlišuje velikost písmen
			out.name = TextUtil::UniqueName(this->usedMeshNames, label, std::string(), std::string::npos, false);

			SkinData *skin = nullptr;

			if (skinned) {
				skin = &this->EnsureSkin(static_cast<size_t>(node.skin));
				out.hasSkeleton = true;
				out.bones = skin->bones;

				if (!skin->clipsGiven) {
					out.clips = skin->clips;
					skin->clipsGiven = true;
				}
			}
			else {
				// Statický mesh pod animovaným uzlem zůstane v klidové poloze
				for (int n = nodeIndex; n >= 0; n = this->scene.nodes[static_cast<size_t>(n)].parent) {

					if (this->nodeAnimated[static_cast<size_t>(n)]) {
						this->WarnOnce("rigid:" + out.name, "Mesh '" + out.name + "' is attached to an animated node, but CMO can animate only skinned meshes - it is exported in its rest pose.");
						break;
					}
				}
			}

			const Mat4 W = skinned ? MathUtil::Identity() : this->world[static_cast<size_t>(nodeIndex)];
			const Mat4 N = MathUtil::NormalMatrix(W);
			const double determinant = MathUtil::Determinant3(W);
			const bool flipsOrientation = determinant < 0.0;
			const bool swapIndices = !flipsOrientation; // viz popis vinutí na začátku souboru
			const bool mirror = this->options.leftHanded;

			if (!skinned && !std::isfinite(determinant)) {
				throw std::runtime_error("Node '" + node.name + "' has a transformation that is not finite");
			}

			std::map<int, uint32_t> materialSlots;
			bool missingSkin = false;
			ExportUtil::Bounds bounds;

			for (const Gltf::Primitive &primitive : mesh.primitives) {

				// Materiál (v rámci meshe jedinečné záznamy)
				const auto slot = materialSlots.find(primitive.material);
				uint32_t materialIndex;

				if (slot == materialSlots.end()) {
					const bool hasMaterial = primitive.material >= 0;
					const Gltf::Material &src = this->SourceMaterial(primitive.material);
					const std::string name = !src.name.empty() ? src.name : (hasMaterial ? "Material_" + std::to_string(primitive.material) : "Default");
					materialIndex = static_cast<uint32_t>(out.materials.size());
					materialSlots[primitive.material] = materialIndex;
					out.materials.push_back(this->MakeMaterial(src, name));
				}
				else {
					materialIndex = slot->second;
				}

				const Prepared p = this->Prepare(primitive);

				if (skinned && p.joints.empty()) {
					missingSkin = true;
				}

				/// Rozdělení na vertex buffery s nejvýš 65535 vrcholy (obvykle jediný, ve stejném pořadí vrcholů jako ve zdroji)
				struct Chunk {
					std::vector<uint32_t> vertices;
					std::vector<uint16_t> indices;
				};

				std::vector<Chunk> chunks;

				if (p.vertexCount <= MAX_BUFFER_VERTICES) {
					Chunk chunk;
					chunk.vertices.resize(p.vertexCount);

					for (size_t v = 0; v < p.vertexCount; ++v) {
						chunk.vertices[v] = static_cast<uint32_t>(v);
					}

					chunk.indices.reserve(p.triangles.size());

					for (size_t t = 0; t < p.triangles.size(); t += 3) {
						chunk.indices.push_back(static_cast<uint16_t>(p.triangles[t]));
						chunk.indices.push_back(static_cast<uint16_t>(p.triangles[t + (swapIndices ? 2 : 1)]));
						chunk.indices.push_back(static_cast<uint16_t>(p.triangles[t + (swapIndices ? 1 : 2)]));
					}

					chunks.push_back(std::move(chunk));
				}
				else {
					std::vector<int32_t> remap(p.vertexCount, -1);
					Chunk current;

					const auto flush = [&]() {
						if (!current.indices.empty()) {

							for (const uint32_t v : current.vertices) {
								remap[v] = -1;
							}

							chunks.push_back(std::move(current));
							current = Chunk();
						}
					};

					for (size_t t = 0; t < p.triangles.size(); t += 3) {
						const uint32_t tri[3] = {p.triangles[t], p.triangles[t + (swapIndices ? 2 : 1)], p.triangles[t + (swapIndices ? 1 : 2)]};
						size_t needed = 0;

						for (size_t c = 0; c < 3; ++c) {
							bool repeated = false;

							for (size_t d = 0; d < c; ++d) {
								repeated = repeated || tri[d] == tri[c];
							}

							if (remap[tri[c]] < 0 && !repeated) {
								++needed;
							}
						}

						if (current.vertices.size() + needed > MAX_BUFFER_VERTICES) {
							flush();
						}

						for (size_t c = 0; c < 3; ++c) {

							if (remap[tri[c]] < 0) {
								remap[tri[c]] = static_cast<int32_t>(current.vertices.size());
								current.vertices.push_back(tri[c]);
							}

							current.indices.push_back(static_cast<uint16_t>(remap[tri[c]]));
						}
					}

					flush();
					Log::Detail("Mesh '" + out.name + "': a primitive with " + std::to_string(p.vertexCount) + " vertices was split into " + std::to_string(chunks.size()) + " buffers (16-bit indices).");
				}

				for (const Chunk &chunk : chunks) {
					std::vector<Cmo::VertexRecord> vertices(chunk.vertices.size());
					std::vector<Cmo::SkinningVertexRecord> skinRecords;

					if (skinned) {
						skinRecords.resize(chunk.vertices.size());
					}

					for (size_t i = 0; i < chunk.vertices.size(); ++i) {
						const size_t v = chunk.vertices[i];
						Vec3 position = ExportUtil::Vec3At(p.positions, v, 3);
						Vec3 normal = ExportUtil::Vec3At(p.normals, v, 3);
						Vec3 tangent = ExportUtil::Vec3At(p.tangents, v, 4);
						double handedness = p.tangents[v * 4 + 3] < 0.0f ? -1.0 : 1.0;

						if (!skinned) {
							position = MathUtil::TransformPoint(W, position);
							normal = MathUtil::Normalize(MathUtil::TransformVector(N, normal));
							tangent = MathUtil::Normalize(MathUtil::TransformVector(W, tangent));

							if (flipsOrientation) {
								handedness = -handedness;
							}
						}

						if (mirror) {
							position = MathUtil::MirrorZ(position);
							normal = MathUtil::MirrorZ(normal);
							tangent = MathUtil::MirrorZ(tangent);
							handedness = -handedness;
						}

						Cmo::VertexRecord &record = vertices[i];
						ToFloats(position, record.position);
						ToFloats(normal, record.normal);
						ToFloats(tangent, record.tangent);
						record.tangent[3] = static_cast<float>(handedness);
						record.color = p.colors[v];
						record.texCoord[0] = p.texCoords.empty() ? 0.0f : p.texCoords[v * 2];
						record.texCoord[1] = p.texCoords.empty() ? 0.0f : p.texCoords[v * 2 + 1];

						if (!std::isfinite(record.position[0]) || !std::isfinite(record.position[1]) || !std::isfinite(record.position[2])) {
							throw std::runtime_error("Mesh '" + out.name + "' has a position that is not finite after the transformation");
						}

						bounds.Add(position);

						if (skinned) {
							Cmo::SkinningVertexRecord &skinRecord = skinRecords[i];

							for (size_t c = 0; c < 4; ++c) {

								if (p.joints.empty()) {
									skinRecord.boneIndex[c] = 0;
									skinRecord.boneWeight[c] = c == 0 ? 1.0f : 0.0f;
									continue;
								}

								const uint32_t slotIndex = p.joints[v * 4 + c];
								const float weight = p.weights[v * 4 + c];

								if (!(weight > 0.0f)) {
									skinRecord.boneIndex[c] = 0;
									skinRecord.boneWeight[c] = 0.0f;
									continue;
								}

								if (slotIndex >= skin->jointToBone.size()) {
									throw std::runtime_error(
										"Mesh '" + out.name + "': a vertex uses the joint " + std::to_string(slotIndex) + " but the skin has only " + std::to_string(skin->jointToBone.size()) + " joints"
									);
								}

								skinRecord.boneIndex[c] = static_cast<uint32_t>(skin->jointToBone[slotIndex]);
								skinRecord.boneWeight[c] = weight;
							}
						}
					}

					Cmo::SubMeshRecord sm {};
					sm.materialIndex = materialIndex;
					sm.indexBufferIndex = static_cast<uint32_t>(out.indexBuffers.size());
					sm.vertexBufferIndex = static_cast<uint32_t>(out.vertexBuffers.size());
					sm.startIndex = 0;
					sm.primCount = static_cast<uint32_t>(chunk.indices.size() / 3);
					out.subMeshes.push_back(sm);

					this->result.stats.vertices += vertices.size();
					this->result.stats.triangles += sm.primCount;
					out.indexBuffers.push_back(chunk.indices);
					out.vertexBuffers.push_back(std::move(vertices));

					if (skinned) {
						out.skinBuffers.push_back(std::move(skinRecords));
					}
				}
			}

			if (out.subMeshes.empty()) {
				return;
			}

			if (missingSkin) {
				this->WarnOnce("noskin:" + out.name, "Mesh '" + out.name + "': some primitives have a skin but no joints and weights - their vertices are bound to the first bone.");
			}

			// Obálka (v souřadnicích souboru): střed kvádru a poloměr koule kolem něj
			Cmo::MeshExtentsRecord &extents = out.extents;
			const Vec3 center = bounds.Center();
			double radius = 0.0;

			for (const auto &vb : out.vertexBuffers) {

				for (const Cmo::VertexRecord &v : vb) {
					radius = std::max(radius, MathUtil::Length(MathUtil::Sub({v.position[0], v.position[1], v.position[2]}, center)));
				}
			}

			ToFloats(center, &extents.values[0]);
			extents.values[3] = static_cast<float>(radius);
			ToFloats(bounds.min, &extents.values[4]);
			ToFloats(bounds.max, &extents.values[7]);

			this->result.stats.meshes += 1;
			this->result.stats.skinnedMeshes += skinned ? 1 : 0;
			this->result.stats.subMeshes += out.subMeshes.size();
			this->result.stats.materials += out.materials.size();
			this->result.model.meshes.push_back(std::move(out));
		}

		Result Converter::Run()
		{
			this->ComputeWorld();
			this->skins.assign(this->scene.skins.size(), SkinData());

			this->nodeAnimated = this->AnimatedNodes();

			// Průchod scénou do hloubky (pořadí uzlů dává pořadí meshů v souboru)
			std::vector<int> stack;

			for (size_t i = this->scene.roots.size(); i-- > 0;) {
				stack.push_back(this->scene.roots[i]);
			}

			std::vector<bool> visited(this->scene.nodes.size(), false);

			while (!stack.empty()) {
				const int current = stack.back();
				stack.pop_back();

				if (visited[static_cast<size_t>(current)]) {
					continue;
				}

				visited[static_cast<size_t>(current)] = true;
				const Gltf::Node &node = this->scene.nodes[static_cast<size_t>(current)];

				if (node.mesh >= 0) {
					this->AddInstance(current);
				}

				for (size_t k = node.children.size(); k-- > 0;) {
					stack.push_back(node.children[k]);
				}
			}

			if (this->result.model.meshes.empty()) {
				throw std::runtime_error("The scene contains no triangle geometry that can be exported");
			}

			// Animace, které CMO nemůže vyjádřit
			if (!this->options.noAnimations) {
				size_t skippedChannels = this->scene.skippedChannels;

				for (const Gltf::Animation &animation : this->scene.animations) {

					for (const Gltf::Channel &channel : animation.channels) {

						if (this->animatedChainNodes.find(channel.node) == this->animatedChainNodes.end()) {
							++skippedChannels;
						}
					}
				}

				if (skippedChannels > 0) {
					this->WarnOnce(
						"skipped-channels",
						std::to_string(skippedChannels) + " animation channels were not exported: CMO animates only the bones of skinned meshes (no animation of plain nodes or morph target weights)."
					);
				}
			}

			this->WarnGeometryLimits("CMO");

			for (const SkinData &skin : this->skins) {

				if (skin.clipsGiven) {
					this->result.stats.clips += skin.clips.size();

					for (const Cmo::Clip &clip : skin.clips) {
						this->result.stats.keys += clip.keys.size();
					}
				}

				this->result.stats.bones += skin.bones.size();
			}

			size_t skeletons = 0;

			for (const SkinData &skin : this->skins) {
				skeletons += skin.clipsGiven ? 1 : 0;
			}

			if (skeletons > 1) {
				Log::Warn("The scene has " + std::to_string(skeletons) + " different skeletons; DirectXTK keeps the bones and the animations of only one skeleton per model.");
			}

			this->result.stats.materials = 0;

			for (const Cmo::Mesh &mesh : this->result.model.meshes) {
				this->result.stats.materials += mesh.materials.size();
			}

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
