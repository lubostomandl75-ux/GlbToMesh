/**
 * @file      CmoOutputFormat.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Převod scény glTF na soubor CMO (viz CmoOutputFormat.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Adaptér mezi rozhraním Formats::OutputFormat a převodníkem CmoConvert (CmoConverter.h) se zapisovačem CmoWriter. Pravidla převodu
 * (souřadnice, vinutí, skinning, animace) jsou popsána v CmoConverter.cpp.
 */

// Hlavičkové soubory C++
#include <string>
#include <utility>
#include <vector>

// Hlavičkové soubory projektu
#include "CmoConverter.h"
#include "CmoOutputFormat.h"
#include "CmoWriter.h"

namespace Cmo {

	namespace {

		/// Výstupní formát CMO: adaptér převodníku CmoConvert a zapisovače CmoWriter na rozhraní Formats::OutputFormat
		class CmoOutputFormat : public Formats::OutputFormat {
		  public:
			/// Název formátu
			std::string Name() const override
			{
				return "CMO";
			}

			/// Popis formátu pro nápovědu
			std::string Description() const override
			{
				return "Visual Studio Compiled Mesh Object (with skeletons and animations)";
			}

			/// Přípona souboru formátu
			std::vector<std::string> Extensions() const override
			{
				return {".cmo"};
			}

			/// Přepínač tohoto formátu: --pixel-shader
			std::vector<Formats::OptionSpec> Options() const override
			{
				return {
					{"pixel-shader", '\0', "name", "Pixel shader name stored in the materials (default: none, so DGSLEffectFactory\nuses its built-in material)", {}},
				};
			}

			/// Poznámky ke formátu pro nápovědu (soustava souřadnic, vinutí, textury)
			std::string HelpNotes() const override
			{
				return "CMO files made by Visual Studio are right-handed, like glTF, so by default the Z axis is not mirrored; use --left-handed to write\n"
					   "the left-handed Direct3D system. The triangle winding is written the way Visual Studio does it (clockwise). The CMO file refers\n"
					   "to the textures by their file names only; they are extracted from the glTF file as they are (PNG, JPEG, ...) and DirectXTK loads\n"
					   "them through WIC. --srgb-colors is for loading with ModelLoader_MaterialColorsSRGB.";
			}

			/**
			 * Převede scénu glTF na soubor CMO (včetně textur).
			 *
			 * @param const Formats::ExportContext& context Scéna, přepínače a název modelu
			 *
			 * @return Formats::ExportResult Obsah souboru CMO a souborů textur
			 *
			 * @throws std::runtime_error Pokud scéna neobsahuje žádnou použitelnou geometrii nebo porušuje limity formátu
			 */
			Formats::ExportResult Export(const Formats::ExportContext &context) const override
			{
				CmoConvert::Options options;
				options.SetCommon(context, false); // Auto = pravotočivá soustava jako ve Visual Studiu
				options.pixelShader = context.options.Last("pixel-shader");

				CmoConvert::Result result = CmoConvert::Convert(context.scene, options);
				const CmoConvert::Stats &stats = result.stats;

				Formats::ExportResult out;
				out.model = CmoWriter::Serialize(result.model);
				out.textures = std::move(result.textures);
				out.summary = std::to_string(stats.meshes) + " meshes (" + std::to_string(stats.skinnedMeshes) + " skinned), " + std::to_string(stats.subMeshes) + " submeshes, " +
							  std::to_string(stats.vertices) + " vertices, " + std::to_string(stats.triangles) + " triangles, " + std::to_string(stats.materials) + " materials, " +
							  std::to_string(out.textures.size()) + " textures, " + std::to_string(stats.bones) + " bones, " + std::to_string(stats.clips) + " animation clips (" +
							  std::to_string(stats.keys) + " keys)";

				return out;
			}
		};
	}

	std::unique_ptr<Formats::OutputFormat> CreateOutputFormat()
	{
		return std::make_unique<CmoOutputFormat>();
	}
}
