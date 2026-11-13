/**
 * @file      SdkMeshOutputFormat.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Převod scény glTF na soubory SDKMESH a SDKANIMATION (viz SdkMeshOutputFormat.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Adaptér mezi rozhraním Formats::OutputFormat a převodníkem SdkMeshConvert (SdkMeshConverter.h) se zapisovačem SdkMesh::Serialize*.
 * Pravidla převodu (souřadnice, vinutí, hierarchie snímků, skinning, animace) jsou popsána v SdkMeshConverter.cpp.
 */

// Hlavičkové soubory C++
#include <set>
#include <string>
#include <utility>
#include <vector>

// Hlavičkové soubory projektu
#include "SdkMeshConverter.h"
#include "SdkMeshOutputFormat.h"
#include "SdkMeshWriter.h"

#include "core/TextUtil.h"

namespace SdkMesh {

	namespace {

		/// Nejvyšší počet klíčů za sekundu, který lze zadat v --anim-fps
		constexpr unsigned long MAX_ANIMATION_FPS = 1000;

		/**
		 * Názvy souborů animací: jediná animace dostane název modelu s příponou .sdkmesh_anim (jak to čeká DXUT), víc animací
		 * "<model>_<název animace>.sdkmesh_anim".
		 */
		std::vector<std::string> AnimationFileNames(const std::string &modelStem, const std::vector<Animation> &animations)
		{
			std::vector<std::string> names;
			std::set<std::string> used;

			for (size_t i = 0; i < animations.size(); ++i) {
				std::string stem = modelStem;

				if (animations.size() > 1) {
					stem += "_" + TextUtil::SanitizeFileName(animations[i].name, "animation" + std::to_string(i));
				}

				std::string candidate = stem + ".sdkmesh_anim";

				for (size_t suffix = 2; !used.insert(TextUtil::ToLowerAscii(candidate)).second; ++suffix) {
					candidate = stem + "_" + std::to_string(suffix) + ".sdkmesh_anim";
				}

				names.push_back(candidate);
			}

			return names;
		}

		/**
		 * Přečte hodnotu --anim-fps.
		 *
		 * @param const Formats::OptionSet& options Přepínače formátu
		 *
		 * @return uint32_t Počet klíčů za sekundu (výchozí 30)
		 *
		 * @throws Formats::UsageError Pokud hodnota není číslo od 1 do 1000
		 */
		uint32_t ParseAnimationFps(const Formats::OptionSet &options)
		{
			if (!options.Has("anim-fps")) {
				return SdkMeshConvert::Options().animationFps;
			}

			const std::string text = options.Last("anim-fps");
			size_t used = 0;
			unsigned long fps = 0;

			try {
				fps = std::stoul(text, &used);
			}
			catch (const std::exception &) {
				used = 0;
			}

			if (used != text.size() || fps < 1 || fps > MAX_ANIMATION_FPS) {
				throw Formats::UsageError("--anim-fps must be a number from 1 to " + std::to_string(MAX_ANIMATION_FPS));
			}

			return static_cast<uint32_t>(fps);
		}

		/// Výstupní formát SDKMESH: adaptér převodníku SdkMeshConvert a zapisovače SdkMesh::Serialize* na rozhraní Formats::OutputFormat
		class SdkMeshOutputFormat : public Formats::OutputFormat {
		  public:
			/// Název formátu
			std::string Name() const override
			{
				return "SDKMESH";
			}

			/// Popis formátu pro nápovědu
			std::string Description() const override
			{
				return "DirectX SDK Mesh, versions 101 and 200 (animations in .sdkmesh_anim files)";
			}

			/// Přípona souboru formátu
			std::vector<std::string> Extensions() const override
			{
				return {".sdkmesh"};
			}

			/// Přepínače tohoto formátu: --anim-fps, --pbr a --bake-transforms
			std::vector<Formats::OptionSpec> Options() const override
			{
				return {
					{"anim-fps", '\0', "n", "Keys per second in the .sdkmesh_anim files (default: 30)", {}},
					{"pbr", '\0', "", "Write SDKMESH version 200 with PBR materials (albedo, normal, RMA and emissive\ntextures; load it with PBREffectFactory) instead of version 101 (Blinn-Phong)", {}},
					{"bake-transforms", '\0', "", "Bake the node transforms of static meshes into their vertices (world coordinates, mesh on an identity frame)\nfor viewers that ignore the frame matrices (DirectXTK without bones); animated nodes are not baked", {}},
				};
			}

			/// Poznámky ke formátu pro nápovědu (soustava souřadnic, animace, textury)
			std::string HelpNotes() const override
			{
				return "SDKMESH is written right-handed by default, like glTF and DirectXTK; use --left-handed for a left-handed viewer (DXUT style,\n"
					   "such as soldier.sdkmesh). Every animation of the glTF file becomes one .sdkmesh_anim file next to the model. The SDKMESH file\n"
					   "refers to the textures by their file names only; they are extracted from the glTF file as they are (PNG, JPEG, ...).\n"
					   "--srgb-colors applies to the colors of materials of version 101 (for ModelLoader_MaterialColorsSRGB).";
			}

			/**
			 * Zkontroluje hodnotu --anim-fps ještě před čtením souboru.
			 *
			 * @param const Formats::CommonOptions& common Společné přepínače
			 * @param const Formats::OptionSet& options Přepínače tohoto formátu
			 *
			 * @throws Formats::UsageError Pokud --anim-fps není číslo od 1 do 1000
			 */
			void ValidateOptions(const Formats::CommonOptions &common, const Formats::OptionSet &options) const override
			{
				(void)common;
				ParseAnimationFps(options);
			}

			/**
			 * Převede scénu glTF na soubor SDKMESH, soubory animací SDKANIMATION a textury.
			 *
			 * @param const Formats::ExportContext& context Scéna, přepínače a název modelu
			 *
			 * @return Formats::ExportResult Obsah souboru SDKMESH, souborů animací a textur
			 *
			 * @throws std::runtime_error Pokud scéna neobsahuje žádnou použitelnou geometrii nebo porušuje limity formátu
			 */
			Formats::ExportResult Export(const Formats::ExportContext &context) const override
			{
				SdkMeshConvert::Options options;
				options.leftHanded = context.common.handedness == Formats::Handedness::Left; // Auto = pravotočivá soustava jako v glTF a DirectXTK
				options.srgbColors = context.common.srgbColors;
				options.noAnimations = context.common.noAnimations;
				options.noTextures = context.common.noTextures;
				options.pbr = context.options.Has("pbr");
				options.bakeTransforms = context.options.Has("bake-transforms");
				options.animationFps = ParseAnimationFps(context.options);
				options.modelName = context.modelName;

				SdkMeshConvert::Result result = SdkMeshConvert::Convert(context.scene, options);
				const SdkMeshConvert::Stats &stats = result.stats;

				Formats::ExportResult out;
				out.model = SerializeModel(result.model);
				out.modelNote = "SDKMESH " + std::to_string(result.model.version);
				out.textures = std::move(result.textures);
				out.summary = std::to_string(stats.frames) + " frames, " + std::to_string(stats.meshes) + " meshes (" + std::to_string(stats.skinnedMeshes) + " skinned), " +
							  std::to_string(stats.vertexBuffers) + " vertex buffers, " + std::to_string(stats.subsets) + " subsets, " + std::to_string(stats.vertices) + " vertices, " +
							  std::to_string(stats.triangles) + " triangles, " + std::to_string(stats.materials) + " materials, " + std::to_string(out.textures.size()) + " textures, " +
							  std::to_string(stats.animations) + " animations (" + std::to_string(stats.keys) + " keys)";

				if (options.bakeTransforms) {
					out.summary += ", " + std::to_string(stats.bakedMeshes) + " baked into world coordinates";
				}

				// Každá animace je samostatný soubor vedle modelu; název vychází z názvu výstupního souboru bez úprav
				const std::vector<std::string> names = AnimationFileNames(context.outputStem, result.animations);

				for (size_t i = 0; i < result.animations.size(); ++i) {
					const Animation &animation = result.animations[i];

					Formats::OutputFile file;
					file.fileName = names[i];
					file.data = SerializeAnimation(animation);
					file.summary = "animation '" + animation.name + "', " + std::to_string(animation.tracks.size()) + " tracks, " + std::to_string(animation.keyCount) + " keys at " +
								   std::to_string(animation.fps) + " per second";
					out.companions.push_back(std::move(file));
				}

				return out;
			}
		};
	}

	std::unique_ptr<Formats::OutputFormat> CreateOutputFormat()
	{
		return std::make_unique<SdkMeshOutputFormat>();
	}
}
