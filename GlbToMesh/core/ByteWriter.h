/**
 * @file      ByteWriter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Rostoucí paměťový buffer pro sestavení binárních souborů (společný pro zapisovače formátů)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Hodnoty se zapisují svým paměťovým obrazem (bez převodu pořadí bajtů); zapisovač formátu ověřuje, že to formátu odpovídá.
 */

#pragma once

// Hlavičkové soubory C++
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace ExportUtil {

	/// Rostoucí paměťový buffer se zápisem hodnot, polí záznamů a zarovnáním
	class ByteWriter
	{
	  public:
		/// Aktuální velikost bufferu v bajtech
		size_t Size() const
		{
			return this->bytes.size();
		}

		/// Připojí blok bajtů
		void Raw(const void *data, size_t size)
		{
			if (size == 0) {
				return;
			}

			const auto *p = static_cast<const uint8_t *>(data);
			this->bytes.insert(this->bytes.end(), p, p + size);
		}

		/// Připojí hodnotu (její paměťový obraz)
		template <typename T> void Append(const T &value)
		{
			static_assert(std::is_trivially_copyable_v<T>, "ByteWriter writes the memory image of the value");
			this->Raw(&value, sizeof(T));
		}

		/// Připojí pole záznamů v jednom bloku
		template <typename T> void Records(const std::vector<T> &items)
		{
			static_assert(std::is_trivially_copyable_v<T>, "ByteWriter writes the memory image of the records");
			this->Raw(items.data(), items.size() * sizeof(T));
		}

		/// Doplní nulové bajty tak, aby velikost byla násobkem zarovnání
		void PadTo(size_t alignment)
		{
			const size_t rest = this->bytes.size() % alignment;

			if (rest != 0) {
				this->bytes.resize(this->bytes.size() + (alignment - rest), 0);
			}
		}

		/// Vydá obsah bufferu (buffer je potom prázdný)
		std::vector<uint8_t> Take()
		{
			return std::move(this->bytes);
		}

	  private:
		std::vector<uint8_t> bytes;
	};
}
