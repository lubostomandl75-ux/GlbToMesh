/**
 * @file      CommandLine.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace zpracování příkazové řádky (viz CommandLine.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Hlavičkové soubory projektu
#include "CommandLine.h"

#include "core/Log.h"
#include "core/TextUtil.h"

namespace CommandLine {

	namespace {

		/// Společný přepínač programu: popis (název, zkratka, nápověda) a jeho účinek na nastavení
		struct CommonOption {
			Formats::OptionSpec spec;
			std::function<void(Settings &, const std::string &)> apply; // hodnota je prázdná u přepínače bez hodnoty
		};

		/// Společné přepínače v pořadí nápovědy; řídí parsování, nápovědu i názvy, které formáty nesmí použít
		const std::vector<CommonOption> &CommonOptions()
		{
			static const std::vector<CommonOption> options = {
				{{"output", 'o', "file", "Output file (default: the input name with the extension of the format; needs --format)", {}},
				 [](Settings &s, const std::string &v) { s.output = TextUtil::PathFromUtf8(v); }},
				{{"format", 'f', "name", "Output format (default: from the extension of the output file)", {}},
				 [](Settings &s, const std::string &v) { s.formatName = v; }},
				{{"list-formats", '\0', "", "List the supported output formats", {}},
				 [](Settings &s, const std::string &) { s.listFormats = true; }},
				{{"no-anim", '\0', "", "Export no animations (skeleton and skinning are kept)", {}},
				 [](Settings &s, const std::string &) { s.common.noAnimations = true; }},
				{{"no-textures", '\0', "", "Do not write texture files; materials are exported without textures", {}},
				 [](Settings &s, const std::string &) { s.common.noTextures = true; }},
				{{"texture-dir", '\0', "dir", "Directory for the texture files (default: the directory of the output file).\nThe model refers to the textures by their file names only.", {}},
				 [](Settings &s, const std::string &v) { s.textureDirectory = TextUtil::PathFromUtf8(v); }},
				{{"left-handed", '\0', "", "Write the model in the left-handed Direct3D system: mirror the Z axis", {}},
				 [](Settings &s, const std::string &) { s.common.handedness = Formats::Handedness::Left; }},
				{{"right-handed", '\0', "",
				  "Write the model right-handed, like glTF: do not mirror. Without either option the\nformat decides (see the notes of the formats below). The triangle winding is always\nwritten the way the format expects.",
				  {}},
				 [](Settings &s, const std::string &) { s.common.handedness = Formats::Handedness::Right; }},
				{{"srgb-colors", '\0', "", "Write the material colors as sRGB (glTF has them linear)", {}},
				 [](Settings &s, const std::string &) { s.common.srgbColors = true; }},
				{{"verbose", 'v', "", "Print details", {}},
				 [](Settings &, const std::string &) { Log::Verbose() = true; }},
				{{"help", 'h', "", "Show this help", {}},
				 [](Settings &s, const std::string &) { s.help = true; }},
			};

			return options;
		}

		/// Společný přepínač zadaný argumentem ("--název" nebo "-x"), jinak nullptr
		const CommonOption *FindCommon(const std::string &arg)
		{
			for (const CommonOption &option : CommonOptions()) {

				if ((arg.size() > 2 && arg.compare(0, 2, "--") == 0 && option.spec.MatchesLong(arg.substr(2))) ||
					(arg.size() == 2 && arg[0] == '-' && option.spec.MatchesShort(arg[1]))) {
					return &option;
				}
			}

			return nullptr;
		}

		/// Dlouhý název patří společnému přepínači programu; formát ho nesmí použít
		bool IsReservedLong(const std::string &name)
		{
			return std::any_of(CommonOptions().begin(), CommonOptions().end(), [&](const CommonOption &option) { return option.spec.MatchesLong(name); });
		}

		/// Zkratka patří společnému přepínači programu; formát ji nesmí použít
		bool IsReservedShort(char c)
		{
			return std::any_of(CommonOptions().begin(), CommonOptions().end(), [&](const CommonOption &option) { return option.spec.MatchesShort(c); });
		}

		/// Vypíše řádek nápovědy přepínače; víceřádkový text se zarovná pod první řádek
		void PrintOption(const Formats::OptionSpec &spec)
		{
			std::string label = spec.shortName != '\0' ? std::string("-") + spec.shortName + ", " : std::string("    ");
			label += "--" + spec.name + (spec.TakesValue() ? " <" + spec.valueName + ">" : std::string());
			label.resize(std::max<size_t>(label.size() + 1, 25), ' ');

			std::string help;

			for (const char c : spec.help) {
				help += c;

				if (c == '\n') {
					help += std::string(27, ' ');
				}
			}

			std::cout << "  " << label << help << "\n";
		}

		/**
		 * Zkontroluje, že žádný formát nedeklaruje přepínač, který patří společným přepínačům programu.
		 *
		 * @param const Formats::FormatRegistry& registry Registr formátů
		 *
		 * @throws std::logic_error Pokud formát deklaruje vyhrazený dlouhý název nebo zkratku (chyba programátora formátu)
		 */
		void ValidateRegistry(const Formats::FormatRegistry &registry)
		{
			for (const auto &format : registry.Formats()) {

				for (const Formats::OptionSpec &spec : format->Options()) {
					for (const std::string &name : spec.AllNames()) {

						if (IsReservedLong(name)) {
							throw std::logic_error("The format '" + format->Name() + "' declares the option --" + name + ", which is a common option of the program");
						}
					}

					if (spec.shortName != '\0' && IsReservedShort(spec.shortName)) {
						throw std::logic_error("The format '" + format->Name() + "' declares the option -" + spec.shortName + ", which is a common option of the program");
					}
				}
			}
		}

		/// Názvy všech registrovaných formátů oddělené čárkou (pro hlášení chyb a nápovědu)
		std::string FormatNames(const Formats::FormatRegistry &registry)
		{
			std::vector<std::string> names;

			for (const auto &format : registry.Formats()) {
				names.push_back(format->Name());
			}

			return TextUtil::Join(names, ", ");
		}

		/// "Option --x" (nebo "-x") pro hlášení
		std::string OptionLabel(const Formats::OptionSpec &spec)
		{
			return "--" + spec.name;
		}
	}

	void Parse(const std::vector<std::string> &args, const Formats::FormatRegistry &registry, Settings &settings)
	{
		ValidateRegistry(registry);

		std::vector<std::string> positional;

		for (size_t i = 0; i < args.size(); ++i) {
			const std::string &arg = args[i];

			const auto value = [&](const std::string &option) -> const std::string & {
				if (i + 1 >= args.size()) {
					throw Formats::UsageError("Missing value for " + option);
				}

				return args[++i];
			};

			if (arg == "/?") {
				settings.help = true;
			}
			else if (const CommonOption *common = FindCommon(arg)) {
				common->apply(settings, common->spec.TakesValue() ? value(OptionLabel(common->spec)) : std::string());
			}
			else if (arg.size() > 1 && arg[0] == '-') {
				// Přepínač formátu: dlouhý (--název) nebo jednopísmenný (-a)
				const Formats::OptionSpec *spec = nullptr;

				if (arg.size() > 2 && arg[1] == '-') {
					spec = registry.FindLong(arg.substr(2));
				}
				else if (arg.size() == 2) {
					spec = registry.FindShort(arg[1]);
				}

				if (spec == nullptr) {
					throw Formats::UsageError("Unknown option: " + arg);
				}

				settings.formatOptionsGiven.emplace_back(spec->name, spec->TakesValue() ? value(OptionLabel(*spec)) : std::string());
			}
			else {
				positional.push_back(arg);
			}
		}

		if (settings.help || settings.listFormats) {
			return;
		}

		if (positional.empty()) {
			throw Formats::UsageError("No input file given (use --help)");
		}

		if (positional.size() > 2) {
			throw Formats::UsageError("Too many arguments: " + positional[2]);
		}

		settings.input = TextUtil::PathFromUtf8(positional[0]);

		if (positional.size() == 2) {

			if (!settings.output.empty()) {
				throw Formats::UsageError("The output file is given twice");
			}

			settings.output = TextUtil::PathFromUtf8(positional[1]);
		}

	}

	void Resolve(const Formats::FormatRegistry &registry, Settings &settings)
	{
		if (!settings.formatName.empty()) {
			settings.format = registry.FindByName(settings.formatName);

			if (settings.format == nullptr) {
				throw Formats::UsageError("Unknown format '" + settings.formatName + "'. Supported formats: " + FormatNames(registry));
			}
		}
		else if (!settings.output.empty()) {
			const std::string extension = TextUtil::PathToUtf8(settings.output.extension());
			settings.format = registry.FindByExtension(extension);

			if (settings.format == nullptr) {
				throw Formats::UsageError(
					(extension.empty() ? "Cannot tell the output format: the output file '" + TextUtil::PathToUtf8(settings.output) + "' has no extension"
									   : "Cannot tell the output format from the extension '" + extension + "' of the output file") +
					"; use --format (supported formats: " + FormatNames(registry) + ")"
				);
			}
		}
		else {
			throw Formats::UsageError("Cannot tell the output format: give the output file with a known extension or use --format (supported formats: " + FormatNames(registry) + ")");
		}

		// Výstupní soubor: výchozí název z názvu vstupu, doplnění přípony formátu
		const std::string defaultExtension = settings.format->Extensions().front();

		if (settings.output.empty()) {
			settings.output = settings.input;
			settings.output.replace_extension(defaultExtension);
		}
		else if (!settings.output.has_extension()) {
			settings.output.replace_extension(defaultExtension);
		}
		else if (!settings.formatName.empty()) {
			// Formát zadaný výslovně má přednost, ale přípona jiného formátu bývá překlep
			const Formats::OutputFormat *byExtension = registry.FindByExtension(TextUtil::PathToUtf8(settings.output.extension()));

			if (byExtension != nullptr && byExtension != settings.format) {
				Log::Warn("The output file has the extension of the format " + byExtension->Name() + ", but the format " + settings.format->Name() + " was chosen (--format).");
			}
		}

		// Přepínače formátů musí patřit vybranému formátu
		for (const auto &[name, value] : settings.formatOptionsGiven) {
			const std::vector<const Formats::OutputFormat *> owners = registry.OwnersOfLong(name);

			if (std::find(owners.begin(), owners.end(), settings.format) == owners.end()) {
				std::vector<std::string> others;

				for (const Formats::OutputFormat *owner : owners) {
					others.push_back(owner->Name());
				}

				throw Formats::UsageError("The option --" + name + " is not supported by the format " + settings.format->Name() + " (it belongs to " + TextUtil::Join(others, ", ") + ")");
			}

			settings.formatOptions.Add(name, value);
		}

		settings.format->ValidateOptions(settings.common, settings.formatOptions);
	}

	void PrintFormats(const Formats::FormatRegistry &registry)
	{
		for (const auto &format : registry.Formats()) {
			const std::string extensions = TextUtil::Join(format->Extensions(), ", ");
			std::string name = format->Name();
			name.resize(std::max<size_t>(name.size(), 10), ' ');
			std::cout << "  " << name << format->Description() << (extensions.empty() ? "" : " (" + extensions + ")") << "\n";
		}
	}

	void PrintUsage(const Formats::FormatRegistry &registry)
	{
		std::cout << "GlbToMesh - converts glTF/GLB models (with skinning and animations) to Direct3D mesh formats (" << FormatNames(registry) << ")\n"
					 "\n"
					 "Usage:\n"
					 "  GlbToMesh <input.glb|input.gltf> [output] [options]\n"
					 "\n"
					 "The output format is chosen by the extension of the output file; use --format to choose it explicitly.\n"
					 "Supported output formats:\n";

		PrintFormats(registry);

		std::cout << "\n"
					 "Options:\n";

		for (const CommonOption &option : CommonOptions()) {
			PrintOption(option.spec);
		}

		for (const auto &format : registry.Formats()) {
			const std::vector<Formats::OptionSpec> options = format->Options();

			if (!options.empty()) {
				std::cout << "\nOptions of the format " << format->Name() << ":\n";
			}

			for (const Formats::OptionSpec &spec : options) {
				PrintOption(spec);
			}
		}

		for (const auto &format : registry.Formats()) {
			const std::string notes = format->HelpNotes();

			if (!notes.empty()) {
				std::cout << "\n"
						  << format->Name() << ": " << notes << "\n";
			}
		}
	}
}
