/**
 * @file      FormatRegistry.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Registr výstupních formátů (viz OutputFormat.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <stdexcept>

// Hlavičkové soubory projektu
#include "OutputFormat.h"
#include "TextUtil.h"

namespace Formats {

	void FormatRegistry::Register(std::unique_ptr<OutputFormat> format)
	{
		if (!format) {
			throw std::logic_error("Cannot register an empty output format");
		}

		const std::string name = TextUtil::ToLowerAscii(format->Name());

		if (name.empty()) {
			throw std::logic_error("An output format must have a name");
		}

		if (this->FindByName(name) != nullptr) {
			throw std::logic_error("The output format '" + format->Name() + "' is already registered (or its name clashes with an extension of another format)");
		}

		if (format->Extensions().empty()) {
			throw std::logic_error("The output format '" + format->Name() + "' must have at least one file extension");
		}

		for (const std::string &extension : format->Extensions()) {

			if (extension.size() < 2 || extension.front() != '.' || extension != TextUtil::ToLowerAscii(extension)) {
				throw std::logic_error("The extension '" + extension + "' of the output format '" + format->Name() + "' must be lowercase and start with a dot");
			}

			if (this->FindByExtension(extension) != nullptr) {
				throw std::logic_error("The extension " + extension + " of the output format '" + format->Name() + "' is already used by another format");
			}
		}

		// Přepínače: stejný název u dvou formátů je v pořádku (sdílí se), ale musí znamenat totéž
		for (const OptionSpec &spec : format->Options()) {

			if (spec.name.empty()) {
				throw std::logic_error("An option of the format '" + format->Name() + "' has no name");
			}

			std::vector<std::string> names = spec.aliases;
			names.push_back(spec.name);

			for (const std::string &optionName : names) {
				const OptionSpec *other = this->FindLong(optionName);

				if (other != nullptr && (other->TakesValue() != spec.TakesValue() || other->name != spec.name)) {
					throw std::logic_error("The option --" + optionName + " of the format '" + format->Name() + "' clashes with an option of another format");
				}
			}

			if (spec.shortName != '\0') {
				const OptionSpec *other = this->FindShort(spec.shortName);

				if (other != nullptr && (other->TakesValue() != spec.TakesValue() || other->name != spec.name)) {
					throw std::logic_error(std::string("The option -") + spec.shortName + " of the format '" + format->Name() + "' clashes with an option of another format");
				}
			}
		}

		// Popisy přepínačů se uchovávají v registru (formát vrací Options() hodnotou, ale FindLong / FindShort vracejí ukazatele)
		for (const OptionSpec &spec : format->Options()) {

			if (this->FindLong(spec.name) == nullptr) {
				this->optionCache.push_back(spec);
			}
		}

		this->formats.push_back(std::move(format));
	}

	const OutputFormat *FormatRegistry::FindByName(const std::string &name) const
	{
		std::string wanted = TextUtil::ToLowerAscii(name);

		if (wanted.empty()) {
			return nullptr;
		}

		for (const auto &format : this->formats) {

			if (TextUtil::ToLowerAscii(format->Name()) == wanted) {
				return format.get();
			}
		}

		// Přípona s tečkou i bez ní
		if (wanted.front() != '.') {
			wanted.insert(wanted.begin(), '.');
		}

		for (const auto &format : this->formats) {

			for (const std::string &extension : format->Extensions()) {

				if (TextUtil::ToLowerAscii(extension) == wanted) {
					return format.get();
				}
			}
		}

		return nullptr;
	}

	const OutputFormat *FormatRegistry::FindByExtension(const std::string &extension) const
	{
		const std::string wanted = TextUtil::ToLowerAscii(extension);

		if (wanted.empty()) {
			return nullptr;
		}

		for (const auto &format : this->formats) {

			for (const std::string &candidate : format->Extensions()) {

				if (TextUtil::ToLowerAscii(candidate) == wanted) {
					return format.get();
				}
			}
		}

		return nullptr;
	}

	std::vector<const OutputFormat *> FormatRegistry::OwnersOfLong(const std::string &name) const
	{
		std::vector<const OutputFormat *> owners;

		for (const auto &format : this->formats) {

			for (const OptionSpec &spec : format->Options()) {

				if (spec.name == name || std::find(spec.aliases.begin(), spec.aliases.end(), name) != spec.aliases.end()) {
					owners.push_back(format.get());

					break;
				}
			}
		}

		return owners;
	}

	std::vector<const OutputFormat *> FormatRegistry::OwnersOfShort(char shortName) const
	{
		std::vector<const OutputFormat *> owners;

		for (const auto &format : this->formats) {

			for (const OptionSpec &spec : format->Options()) {

				if (spec.shortName != '\0' && spec.shortName == shortName) {
					owners.push_back(format.get());

					break;
				}
			}
		}

		return owners;
	}

	const OptionSpec *FormatRegistry::FindLong(const std::string &name) const
	{
		for (const OptionSpec &spec : this->optionCache) {

			if (spec.name == name || std::find(spec.aliases.begin(), spec.aliases.end(), name) != spec.aliases.end()) {
				return &spec;
			}
		}

		return nullptr;
	}

	const OptionSpec *FormatRegistry::FindShort(char shortName) const
	{
		for (const OptionSpec &spec : this->optionCache) {

			if (spec.shortName != '\0' && spec.shortName == shortName) {
				return &spec;
			}
		}

		return nullptr;
	}
}
