/**
 * @file      CmoWriter.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace zápisu CMO (viz CmoWriter.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

// Hlavičkové soubory projektu
#include "CmoWriter.h"

#include "core/TextUtil.h"

namespace CmoWriter {

	namespace {

		/// Výstupní buffer se zápisem čísel, textů a záznamů ve formátu CMO
		class Output
		{
		  public:
			std::vector<uint8_t> bytes;

			/// Připojí surová data
			void Raw(const void *data, size_t size)
			{
				const auto *p = static_cast<const uint8_t *>(data);
				this->bytes.insert(this->bytes.end(), p, p + size);
			}

			/// Připojí 32bitové číslo (hodnota nad rozsah 32 bitů je chyba)
			void U32(size_t value)
			{
				if (value > std::numeric_limits<uint32_t>::max()) {
					throw std::runtime_error("A count in the CMO file exceeds 32 bits");
				}

				const uint32_t v = static_cast<uint32_t>(value);
				this->Raw(&v, sizeof(v));
			}

			/// Připojí jeden bajt
			void Byte(uint8_t value)
			{
				this->bytes.push_back(value);
			}

			/// Text: délka v znacích včetně ukončující nuly a znaky UTF-16 (jako ve Visual Studiu, prázdný text má délku 1)
			void Text(const std::string &text)
			{
				const std::u16string wide = TextUtil::ToUtf16(text);
				this->U32(wide.size() + 1);

				for (const char16_t c : wide) {
					const uint16_t unit = static_cast<uint16_t>(c);
					this->Raw(&unit, sizeof(unit));
				}

				const uint16_t terminator = 0;
				this->Raw(&terminator, sizeof(terminator));
			}

			/// Připojí pole záznamů (struktur bez výplně) v jednom bloku
			template <typename T> void Records(const std::vector<T> &items)
			{
				if (!items.empty()) {
					this->Raw(items.data(), items.size() * sizeof(T));
				}
			}
		};

		/// Ověří, že počet prvků meshe nepřekračuje limit formátu
		void CheckCount(size_t count, const char *what, const std::string &mesh)
		{
			if (count > Cmo::MAX_COUNT) {
				throw std::runtime_error("Mesh '" + mesh + "' has too many " + what + " (" + std::to_string(count) + "); CMO allows at most " + std::to_string(Cmo::MAX_COUNT));
			}
		}
	}

	std::vector<uint8_t> Serialize(const Cmo::Model &model)
	{
		if (model.meshes.empty()) {
			throw std::runtime_error("The model has no meshes");
		}

		if (model.meshes.size() > Cmo::MAX_COUNT) {
			throw std::runtime_error("The model has too many meshes (" + std::to_string(model.meshes.size()) + "); CMO allows at most " + std::to_string(Cmo::MAX_COUNT));
		}

		Output out;
		out.U32(model.meshes.size());

		for (const Cmo::Mesh &mesh : model.meshes) {
			CheckCount(mesh.materials.size(), "materials", mesh.name);
			CheckCount(mesh.indexBuffers.size(), "index buffers", mesh.name);
			CheckCount(mesh.vertexBuffers.size(), "vertex buffers", mesh.name);

			if (mesh.subMeshes.empty() || mesh.indexBuffers.empty() || mesh.vertexBuffers.empty()) {
				throw std::runtime_error("Mesh '" + mesh.name + "' has no geometry");
			}

			if (!mesh.skinBuffers.empty() && mesh.skinBuffers.size() != mesh.vertexBuffers.size()) {
				throw std::runtime_error("Mesh '" + mesh.name + "': the number of skinning buffers differs from the number of vertex buffers");
			}

			if (mesh.hasSkeleton && mesh.bones.empty()) {
				throw std::runtime_error("Mesh '" + mesh.name + "' has a skeleton without bones");
			}

			out.Text(mesh.name);

			out.U32(mesh.materials.size());

			for (const Cmo::Material &material : mesh.materials) {
				out.Text(material.name);
				out.Raw(&material.record, sizeof(material.record));
				out.Text(material.pixelShader);

				for (const std::string &texture : material.textures) {
					out.Text(texture);
				}
			}

			out.Byte(mesh.hasSkeleton ? 1 : 0);

			out.U32(mesh.subMeshes.size());

			for (const Cmo::SubMeshRecord &sm : mesh.subMeshes) {

				if (sm.indexBufferIndex >= mesh.indexBuffers.size() || sm.vertexBufferIndex >= mesh.vertexBuffers.size() ||
					(!mesh.materials.empty() && sm.materialIndex >= mesh.materials.size())) {
					throw std::runtime_error("Mesh '" + mesh.name + "': a submesh refers to a buffer or a material that does not exist");
				}

				const auto &ib = mesh.indexBuffers[sm.indexBufferIndex];
				const auto &vb = mesh.vertexBuffers[sm.vertexBufferIndex];

				if (static_cast<uint64_t>(sm.startIndex) + static_cast<uint64_t>(sm.primCount) * 3 > ib.size()) {
					throw std::runtime_error("Mesh '" + mesh.name + "': a submesh reads indices outside of its index buffer");
				}

				for (uint64_t i = 0; i < static_cast<uint64_t>(sm.primCount) * 3; ++i) {

					if (ib[sm.startIndex + i] >= vb.size()) {
						throw std::runtime_error("Mesh '" + mesh.name + "': a submesh refers to a vertex that does not exist");
					}
				}
			}

			out.Records(mesh.subMeshes);

			out.U32(mesh.indexBuffers.size());

			for (const auto &ib : mesh.indexBuffers) {

				if (ib.empty()) {
					throw std::runtime_error("Mesh '" + mesh.name + "' has an empty index buffer");
				}

				out.U32(ib.size());
				out.Records(ib);
			}

			out.U32(mesh.vertexBuffers.size());

			for (const auto &vb : mesh.vertexBuffers) {

				if (vb.empty()) {
					throw std::runtime_error("Mesh '" + mesh.name + "' has an empty vertex buffer");
				}

				out.U32(vb.size());
				out.Records(vb);
			}

			out.U32(mesh.skinBuffers.size());

			for (size_t i = 0; i < mesh.skinBuffers.size(); ++i) {

				if (mesh.skinBuffers[i].size() != mesh.vertexBuffers[i].size()) {
					throw std::runtime_error("Mesh '" + mesh.name + "': a skinning buffer has a different number of vertices than its vertex buffer");
				}

				out.U32(mesh.skinBuffers[i].size());
				out.Records(mesh.skinBuffers[i]);
			}

			out.Raw(&mesh.extents, sizeof(mesh.extents));

			if (mesh.hasSkeleton) {
				out.U32(mesh.bones.size());

				for (const Cmo::Bone &bone : mesh.bones) {
					out.Text(bone.name);
					out.Raw(&bone.record, sizeof(bone.record));
				}

				out.U32(mesh.clips.size());

				for (const Cmo::Clip &clip : mesh.clips) {
					out.Text(clip.name);
					Cmo::ClipRecord record = clip.record;
					record.keys = static_cast<uint32_t>(clip.keys.size());
					out.Raw(&record, sizeof(record));
					out.Records(clip.keys);
				}
			}
		}

		return std::move(out.bytes);
	}
}
