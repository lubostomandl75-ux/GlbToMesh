/**
 * @file      FileUtil.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace zápisu souborů (viz FileUtil.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <fstream>
#include <stdexcept>

// Hlavičkové soubory projektu
#include "FileUtil.h"
#include "TextUtil.h"

namespace FileUtil {

	void WriteWholeFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);

		if (!stream) {
			throw std::runtime_error("Unable to create " + TextUtil::PathToUtf8(path));
		}

		if (!bytes.empty()) {
			stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}

		stream.close();

		if (!stream) {
			throw std::runtime_error("Unable to write " + TextUtil::PathToUtf8(path));
		}
	}
}
