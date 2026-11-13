/**
 * @file      OutputWriter.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace zápisu výsledku exportu (viz OutputWriter.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <string>
#include <system_error>

// Hlavičkové soubory projektu
#include "FileUtil.h"
#include "Log.h"
#include "OutputWriter.h"
#include "TextUtil.h"

namespace OutputWriter {

	namespace fs = std::filesystem;

	namespace {

		/// "Written <soubor> (<velikost> bytes[, <poznámka>])[: <popis>]"
		std::string WrittenMessage(const fs::path &path, size_t size, const std::string &note, const std::string &summary)
		{
			std::string message = "Written " + TextUtil::PathToUtf8(path) + " (" + std::to_string(size) + " bytes" + (note.empty() ? std::string() : ", " + note) + ")";

			if (!summary.empty()) {
				message += ": " + summary;
			}

			return message;
		}
	}

	void Write(const Formats::ExportResult &result, const fs::path &output, const fs::path &textureDirectory)
	{
		const fs::path outputDirectory = output.has_parent_path() ? output.parent_path() : fs::path(".");
		const fs::path texturesTarget = textureDirectory.empty() ? outputDirectory : textureDirectory;
		std::error_code ec;

		fs::create_directories(outputDirectory, ec);

		if (!result.textures.empty()) {
			fs::create_directories(texturesTarget, ec);
		}

		FileUtil::WriteWholeFile(output, result.model);

		for (const Formats::OutputFile &texture : result.textures) {
			const fs::path target = texturesTarget / TextUtil::PathFromUtf8(texture.fileName);
			FileUtil::WriteWholeFile(target, texture.data);
			Log::Detail("Texture written: " + TextUtil::PathToUtf8(target) + " (" + std::to_string(texture.data.size()) + " bytes)");
		}

		for (const Formats::OutputFile &file : result.companions) {
			const fs::path target = outputDirectory / TextUtil::PathFromUtf8(file.fileName);
			FileUtil::WriteWholeFile(target, file.data);
			Log::Info(WrittenMessage(target, file.data.size(), std::string(), file.summary));
		}

		Log::Info(WrittenMessage(output, result.model.size(), result.modelNote, result.summary));
	}
}
