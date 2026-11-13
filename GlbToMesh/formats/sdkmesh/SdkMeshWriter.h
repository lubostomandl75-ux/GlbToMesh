/**
 * @file      SdkMeshWriter.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Zápis souborů SDKMESH (.sdkmesh) a SDKANIMATION (.sdkmesh_anim) do paměti
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Zapisovač dodržuje rozložení souborů z DirectX SDK (DXUT) a kontroluje stejné podmínky jako načítač DirectXTK
 * (Model::CreateFromSDKMESH v ModelLoadSDKMESH.cpp), takže nevznikne soubor, který by načítač odmítl.
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>
#include <vector>

// Hlavičkové soubory projektu
#include "SdkMeshModel.h"

namespace SdkMesh {

	/**
	 * Zapíše model do podoby souboru .sdkmesh.
	 *
	 * @param const Model& model Model
	 *
	 * @return std::vector<uint8_t> Obsah souboru
	 *
	 * @throws std::runtime_error Pokud model porušuje limity nebo pravidla formátu (neplatné indexy, příliš dlouhé názvy, ...)
	 */
	std::vector<uint8_t> SerializeModel(const Model &model);

	/**
	 * Zapíše animaci do podoby souboru .sdkmesh_anim.
	 *
	 * @param const Animation& animation Animace
	 *
	 * @return std::vector<uint8_t> Obsah souboru
	 *
	 * @throws std::runtime_error Pokud animace porušuje pravidla formátu (různý počet klíčů, příliš dlouhý název snímku, ...)
	 */
	std::vector<uint8_t> SerializeAnimation(const Animation &animation);

	/// Velikost prvku deklarace vrcholu v bajtech (0 = typ, který zapisovač nezná)
	uint32_t ElementSize(uint8_t declType);
}
