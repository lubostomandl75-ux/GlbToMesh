/**
 * @file      OutputFormat.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Rozhraní výstupního formátu a registr formátů
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Každý výstupní formát (CMO, SDKMESH, ...) je třída odvozená od OutputFormat, která umí:
 *  - nabídnout vlastní přepínače příkazové řádky (Options),
 *  - převést načtenou scénu glTF na obsah souborů svého formátu (Export).
 * Soubory na disk zapisuje program sám (model, textury a doprovodné soubory), formát vrací jen jejich obsah v paměti. Formát se přidá
 * jediným voláním registry.Register(...) v souboru formats/FormatList.cpp; zbytek programu (příkazová řádka, nápověda, výběr formátu
 * podle přípony, načtení glTF, zápis souborů) se o novém formátu dozví z tohoto rozhraní. Postup popisuje README.
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Hlavičkové soubory projektu
#include "GltfScene.h"

namespace Formats {

	/// Chybné použití programu (neplatné přepínače); program ji ohlásí s návratovým kódem 2, ostatní chyby s kódem 1
	class UsageError : public std::runtime_error {
	  public:
		using std::runtime_error::runtime_error;
	};

	/// Soustava souřadnic zapisovaných dat
	enum class Handedness {
		/// Rozhodne formát podle své zvyklosti (viz nápověda formátu)
		Auto,

		/// Levotočivá soustava Direct3D: zrcadlí se osa Z
		Left,

		/// Pravotočivá soustava jako v glTF: bez zrcadlení
		Right,
	};

	/// Přepínače společné všem formátům (každý formát je vykládá podle svého, pokud pro něj dávají smysl)
	struct CommonOptions {
		Handedness handedness = Handedness::Auto;

		/// Barvy materiálů zapsat jako sRGB (glTF má barvy lineární)
		bool srgbColors = false;

		/// Neexportovat animace (kostra a skinning zůstanou)
		bool noAnimations = false;

		/// Neexportovat textury (materiály zůstanou bez nich)
		bool noTextures = false;
	};

	/// Popis jednoho přepínače příkazové řádky, který přidává formát
	struct OptionSpec {
		std::string name;		  // dlouhý název bez pomlček, např. "anim-scale" (zadává se --anim-scale)
		char shortName = '\0';	  // jednopísmenná zkratka (zadává se -a) nebo '\0'
		std::string valueName;	  // název hodnoty v nápovědě (např. "file"); prázdný řetězec = přepínač bez hodnoty
		std::string help;		  // text nápovědy; víc řádků se odděluje znakem '\n'
		std::vector<std::string> aliases; // další dlouhé názvy bez pomlček

		/// Přepínač očekává hodnotu (má zadaný název hodnoty)
		bool TakesValue() const
		{
			return !this->valueName.empty();
		}
	};

	/// Hodnoty přepínačů formátu zadané uživatelem (klíčem je hlavní název přepínače bez pomlček)
	class OptionSet {
	  public:
		/// Přidá hodnotu přepínače; přepínač lze zadat opakovaně, hodnoty se uchovávají v pořadí zadání
		void Add(const std::string &name, const std::string &value)
		{
			this->values[name].push_back(value);
		}

		/// Přepínač byl zadán
		bool Has(const std::string &name) const
		{
			return this->values.count(name) > 0;
		}

		/// Všechny zadané hodnoty v pořadí zadání (prázdný seznam, pokud přepínač nebyl zadán)
		const std::vector<std::string> &All(const std::string &name) const
		{
			static const std::vector<std::string> none;
			const auto it = this->values.find(name);

			return it == this->values.end() ? none : it->second;
		}

		/// Poslední zadaná hodnota nebo výchozí řetězec
		std::string Last(const std::string &name, const std::string &fallback = std::string()) const
		{
			const std::vector<std::string> &all = this->All(name);

			return all.empty() ? fallback : all.back();
		}

	  private:
		std::map<std::string, std::vector<std::string>> values;
	};

	/// Soubor, který vznikne při exportu (obsah v paměti; zapisuje ho program)
	struct OutputFile {
		std::string fileName; // UTF-8, bez adresáře
		std::vector<uint8_t> data;
		std::string summary; // co soubor obsahuje (pro závěrečný výpis); prázdný řetězec = bez popisu
	};

	/// Vše, co exportér potřebuje k vytvoření souborů
	struct ExportContext {
		const Gltf::Scene &scene;
		std::string outputStem;		  // název výstupního souboru bez přípony (UTF-8, bez úprav)
		std::string modelName;		  // outputStem upravený na bezpečný název souboru (základ názvů textur bez vlastního názvu)
		const CommonOptions &common;
		const OptionSet &options;	  // přepínače, které přidal tento formát
	};

	/// Výsledek exportu: obsah všech souborů, které se mají zapsat
	struct ExportResult {
		std::vector<uint8_t> model;	 // obsah hlavního souboru (jeho cestu zadal uživatel)
		std::string modelNote;		 // doplněk do závorky ve výpisu za velikostí, např. "SDKMESH 101"; prázdný řetězec = bez doplňku
		std::string summary;		 // co hlavní soubor obsahuje (pro závěrečný výpis)
		std::vector<OutputFile> textures;	// zapíší se do adresáře textur (--texture-dir, jinak vedle modelu)
		std::vector<OutputFile> companions; // zapíšou se vedle modelu (např. soubory animací)
	};

	/// Výstupní formát
	class OutputFormat {
	  public:
		/// Virtuální destruktor: formáty se uvolňují přes ukazatel na rozhraní
		virtual ~OutputFormat() = default;

		/// Název formátu, jak se zadává v --format a jak se zobrazuje (např. "CMO"); porovnává se bez ohledu na velikost písmen
		virtual std::string Name() const = 0;

		/// Jednořádkový popis formátu pro nápovědu
		virtual std::string Description() const = 0;

		/// Přípony souborů formátu malými písmeny včetně tečky (např. {".cmo"}); první je výchozí (přidá se k názvu bez přípony)
		virtual std::vector<std::string> Extensions() const = 0;

		/// Vlastní přepínače formátu (výchozí: žádné)
		virtual std::vector<OptionSpec> Options() const
		{
			return {};
		}

		/// Doplňující odstavec nápovědy o formátu (výchozí: žádný)
		virtual std::string HelpNotes() const
		{
			return {};
		}

		/**
		 * Zkontroluje kombinaci přepínačů ještě před čtením souboru.
		 *
		 * @param const CommonOptions& common Společné přepínače
		 * @param const OptionSet& options Přepínače tohoto formátu
		 *
		 * @throws UsageError Pokud se přepínače vylučují nebo mají neplatnou hodnotu
		 */
		virtual void ValidateOptions(const CommonOptions &common, const OptionSet &options) const
		{
			(void)common;
			(void)options;
		}

		/**
		 * Převede scénu glTF na obsah souborů formátu (nic nezapisuje na disk).
		 *
		 * @param const ExportContext& context Scéna, přepínače a název modelu
		 *
		 * @return ExportResult Obsah modelu, textur a doprovodných souborů
		 *
		 * @throws std::runtime_error Pokud scéna neobsahuje nic k exportu nebo porušuje limity formátu
		 */
		virtual ExportResult Export(const ExportContext &context) const = 0;
	};

	/// Seznam podporovaných formátů
	class FormatRegistry {
	  public:
		/**
		 * Přidá formát.
		 *
		 * @param std::unique_ptr<OutputFormat> format Formát
		 *
		 * @throws std::logic_error Pokud název, přípona nebo přepínač formátu koliduje s dříve přidaným (chyba programátora)
		 */
		void Register(std::unique_ptr<OutputFormat> format);

		/// Všechny registrované formáty v pořadí registrace
		const std::vector<std::unique_ptr<OutputFormat>> &Formats() const
		{
			return this->formats;
		}

		/// Najde formát podle názvu (bez ohledu na velikost písmen; přijímá i příponu s tečkou nebo bez ní); nullptr, pokud není
		const OutputFormat *FindByName(const std::string &name) const;

		/// Najde formát podle přípony souboru (např. ".cmo", bez ohledu na velikost písmen); nullptr, pokud ji žádný formát nemá
		const OutputFormat *FindByExtension(const std::string &extension) const;

		/// Vlastníci přepínače: formáty, které přepínač s daným dlouhým názvem (nebo aliasem) přidávají
		std::vector<const OutputFormat *> OwnersOfLong(const std::string &name) const;

		/// Formáty, které přepínač s danou zkratkou přidávají (viz OwnersOfLong)
		std::vector<const OutputFormat *> OwnersOfShort(char shortName) const;

		/// Popis přepínače z prvního formátu, který ho přidává (pro zjištění, zda bere hodnotu); nullptr, pokud ho nikdo nepřidává
		const OptionSpec *FindLong(const std::string &name) const;

		/// Popis přepínače s danou zkratkou (viz FindLong); nullptr, pokud ho nikdo nepřidává
		const OptionSpec *FindShort(char shortName) const;

	  private:
		std::vector<std::unique_ptr<OutputFormat>> formats;
		std::deque<OptionSpec> optionCache; // deque: ukazatele na prvky zůstávají platné při přidávání
	};

	/**
	 * Zaregistruje všechny formáty programu. Jediné místo, které je třeba upravit při přidání nového formátu
	 * (definice je ve formats/FormatList.cpp).
	 */
	void RegisterAllFormats(FormatRegistry &registry);
}
