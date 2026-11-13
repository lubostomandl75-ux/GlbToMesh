/**
 * @file      CommandLine.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Zpracování příkazové řádky: přepínače společné všem formátům a přepínače, které přidávají jednotlivé formáty
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

#pragma once

// Hlavičkové soubory C++
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Hlavičkové soubory projektu
#include "core/OutputFormat.h"

namespace CommandLine {

	/// Nastavení načtené z příkazové řádky
	struct Settings {
		std::filesystem::path input;
		std::filesystem::path output;
		std::filesystem::path textureDirectory;
		std::string formatName;		// hodnota --format; prázdná = rozpoznat podle přípony výstupního souboru
		bool help = false;
		bool listFormats = false;
		Formats::CommonOptions common;

		/// Přepínače formátů zadané uživatelem, ještě nepřiřazené k formátu (Resolve je ověří a předá vybranému formátu)
		std::vector<std::pair<std::string, std::string>> formatOptionsGiven;

		/// Po Resolve: vybraný formát a jeho přepínače
		const Formats::OutputFormat *format = nullptr;
		Formats::OptionSet formatOptions;
	};

	/**
	 * Zpracuje argumenty příkazové řádky (první průchod: ověří společné přepínače a zapamatuje přepínače formátů).
	 *
	 * @param const std::vector<std::string>& args Argumenty v UTF-8 (bez názvu programu)
	 * @param const Formats::FormatRegistry& registry Registr formátů (zná přepínače formátů)
	 * @param Settings& settings Výsledné nastavení
	 *
	 * @throws Formats::UsageError Při neplatném nebo neúplném přepínači
	 * @throws std::logic_error Pokud formát přidává přepínač, který patří společným přepínačům (chyba programátora)
	 */
	void Parse(const std::vector<std::string> &args, const Formats::FormatRegistry &registry, Settings &settings);

	/**
	 * Vybere výstupní formát (z --format, jinak podle přípony výstupního souboru), doplní příponu a výchozí název výstupu,
	 * ověří, že vybranému formátu patří všechny zadané přepínače formátů, a zkontroluje jejich kombinaci (OutputFormat::ValidateOptions).
	 *
	 * @param const Formats::FormatRegistry& registry Registr formátů
	 * @param Settings& settings Nastavení (doplní se format, formatOptions a případně output)
	 *
	 * @throws Formats::UsageError Při neznámém formátu, nerozpoznatelném formátu nebo přepínači, který vybraný formát nemá
	 */
	void Resolve(const Formats::FormatRegistry &registry, Settings &settings);

	/// Vypíše nápovědu včetně přepínačů a poznámek všech formátů
	void PrintUsage(const Formats::FormatRegistry &registry);

	/// Vypíše seznam podporovaných formátů
	void PrintFormats(const Formats::FormatRegistry &registry);
}
