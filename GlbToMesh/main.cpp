/**
 * @file      main.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Konzolová aplikace GlbToMesh: převod modelů glTF/GLB (včetně skinningu a animací) do formátů Direct3D (CMO, SDKMESH, ...)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <exception>
#include <iostream>
#include <string>
#include <vector>

// Hlavičkové soubory projektu
#include "CommandLine.h"

#include "core/GltfLoader.h"
#include "core/GltfScene.h"
#include "core/Log.h"
#include "core/OutputFormat.h"
#include "core/OutputWriter.h"
#include "core/TextUtil.h"

// Windows.h jako poslední, aby jeho makra (min, max, near, far, ...) neovlivnila ostatní hlavičky
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

	/// Jedním řádkem popíše načtenou scénu (pro výpis po načtení)
	std::string DescribeScene(const Gltf::Scene &scene)
	{
		size_t primitives = 0;

		for (const Gltf::Mesh &mesh : scene.meshes) {
			primitives += mesh.primitives.size();
		}

		return "  glTF: " + std::to_string(scene.nodes.size()) + " nodes, " + std::to_string(scene.meshes.size()) + " meshes (" + std::to_string(primitives) + " primitives), " +
			   std::to_string(scene.materials.size()) + " materials, " + std::to_string(scene.images.size()) + " images, " + std::to_string(scene.skins.size()) + " skins, " +
			   std::to_string(scene.animations.size()) + " animations";
	}

	/**
	 * Provede celý převod podle argumentů příkazové řádky.
	 *
	 * @param const std::vector<std::string>& args Argumenty bez názvu programu (v UTF-8)
	 *
	 * @return int Kód ukončení: 0 = hotovo, 1 = chyba při převodu, 2 = chybné použití programu
	 */
	int Run(const std::vector<std::string> &args)
	{
		Formats::FormatRegistry registry;
		CommandLine::Settings settings;

		try {
			Formats::RegisterAllFormats(registry);
			CommandLine::Parse(args, registry, settings);
		}
		catch (const Formats::UsageError &ex) {
			std::cerr << "Error: " << ex.what() << std::endl;

			return 2;
		}
		catch (const std::exception &ex) {
			std::cerr << "Error: " << ex.what() << std::endl;

			return 1;
		}

		if (settings.help) {
			CommandLine::PrintUsage(registry);

			return 0;
		}

		if (settings.listFormats) {
			CommandLine::PrintFormats(registry);

			return 0;
		}

		try {
			CommandLine::Resolve(registry, settings);

			Log::Info("Reading " + TextUtil::PathToUtf8(settings.input));
			const Gltf::Scene scene = Gltf::Load(settings.input, settings.format->Name());
			Log::Info(DescribeScene(scene));

			const std::string outputStem = TextUtil::PathToUtf8(settings.output.stem());
			const Formats::ExportContext context {scene, outputStem, TextUtil::SanitizeFileName(outputStem, "model"), settings.common, settings.formatOptions};
			const Formats::ExportResult result = settings.format->Export(context);

			OutputWriter::Write(result, settings.output, settings.textureDirectory);

			if (Log::WarningCount() > 0) {
				Log::Info(std::to_string(Log::WarningCount()) + " warning(s) - see above.");
			}

			return 0;
		}
		catch (const Formats::UsageError &ex) {
			std::cerr << "Error: " << ex.what() << std::endl;

			return 2;
		}
		catch (const std::exception &ex) {
			std::cerr << "Error: " << ex.what() << std::endl;

			return 1;
		}
	}
}

#ifdef _WIN32

/// Vstup programu ve Windows: argumenty se převedou z UTF-16 na UTF-8 a konzole se přepne na UTF-8
int wmain(int argc, wchar_t **wargv)
{
	// Konzole i argumenty v UTF-8, aby fungovaly cesty s českou diakritikou
	SetConsoleOutputCP(CP_UTF8);

	std::vector<std::string> args;

	for (int i = 1; i < argc; ++i) {
		const int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
		std::string utf8(size > 0 ? static_cast<size_t>(size - 1) : 0, '\0');

		if (size > 1) {
			WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8.data(), size, nullptr, nullptr);
		}

		args.push_back(std::move(utf8));
	}

	return Run(args);
}

#else

/// Vstup programu mimo Windows; argumenty jsou už v UTF-8
int main(int argc, char **argv)
{
	std::vector<std::string> args;

	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}

	return Run(args);
}

#endif
