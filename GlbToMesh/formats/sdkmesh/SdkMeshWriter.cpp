/**
 * @file      SdkMeshWriter.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace zápisu souborů SDKMESH a SDKANIMATION (viz SdkMeshWriter.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Rozložení souboru .sdkmesh (stejné jako u souborů z DirectX SDK):
 *   hlavička | podhlavičky vertex bufferů | podhlavičky index bufferů |
 *   meshe | podmnožiny | snímky | materiály | pole indexů podmnožin a vlivů kostí každého meshe |
 *   data vertex bufferů | data index bufferů
 * Posuny v hlavičkách i v podhlavičkách jsou od začátku souboru. Struktury se zapisují v rozložení pro #pragma pack(8);
 * soubor je vždy little-endian (IsBigEndian = 0).
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

// Hlavičkové soubory projektu
#include "SdkMeshWriter.h"

#include "core/ByteWriter.h"

namespace SdkMesh {

	static_assert(std::endian::native == std::endian::little, "SDKMESH is little-endian; the writer assumes a little-endian host");

	namespace {

		/// Struktura s vynulovanou pamětí včetně výplně mezi členy (zapisují se celé bajty)
		template <typename T> T Zeroed()
		{
			T value;
			std::memset(&value, 0, sizeof(T));

			return value;
		}

		/// Zapíše text do pole pevné délky doplněného nulami; příliš dlouhý text nebo znak nula je chyba
		template <size_t N> void SetText(char (&target)[N], const std::string &text, const char *what)
		{
			if (text.size() >= N) {
				throw std::runtime_error(std::string(what) + " '" + text.substr(0, 40) + "...' is too long for SDKMESH (at most " + std::to_string(N - 1) + " bytes)");
			}

			if (text.find('\0') != std::string::npos) {
				throw std::runtime_error(std::string(what) + " contains a null character");
			}

			std::memset(target, 0, N);
			std::memcpy(target, text.data(), text.size());
		}

		/// Zaokrouhlí hodnotu nahoru na násobek zarovnání
		uint64_t AlignUp(uint64_t value, uint64_t alignment)
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		/// Druh použití prvku, který načítač DirectXTK zná (jiný by ukončil čtení deklarace)
		bool IsKnownUsage(uint8_t usage)
		{
			switch (usage) {
			case D3DDECLUSAGE_POSITION:
			case D3DDECLUSAGE_BLENDWEIGHT:
			case D3DDECLUSAGE_BLENDINDICES:
			case D3DDECLUSAGE_NORMAL:
			case D3DDECLUSAGE_TEXCOORD:
			case D3DDECLUSAGE_TANGENT:
			case D3DDECLUSAGE_BINORMAL:
			case D3DDECLUSAGE_COLOR:
				return true;
			default:
				return false;
			}
		}

		/// Ověří vertex buffer stejně přísně jako načítač DirectXTK (deklarace, velikosti, rozložení)
		void CheckVertexBuffer(const VertexBuffer &vb, size_t index)
		{
			const std::string where = "Vertex buffer " + std::to_string(index);

			if (vb.decl.empty() || vb.decl.size() >= MAX_VERTEX_ELEMENTS) {
				throw std::runtime_error(where + ": the vertex declaration must have 1 to " + std::to_string(MAX_VERTEX_ELEMENTS - 1) + " elements");
			}

			uint32_t offset = 0;
			bool position = false;

			for (const D3DVertexElement9 &element : vb.decl) {
				const uint32_t size = ElementSize(element.Type);

				if (size == 0 || !IsKnownUsage(element.Usage)) {
					throw std::runtime_error(where + ": unsupported vertex element (type " + std::to_string(element.Type) + ", usage " + std::to_string(element.Usage) + ")");
				}

				// DirectXTK čte deklaraci jen do prvního prvku, jehož posun nenavazuje na předchozí
				if (element.Stream != 0 || element.Offset != offset) {
					throw std::runtime_error(where + ": the vertex elements must be in one stream and follow each other without gaps");
				}

				if (element.Usage == D3DDECLUSAGE_POSITION) {
					if (element.Type != D3DDECLTYPE_FLOAT3) {
						throw std::runtime_error(where + ": the position must be FLOAT3");
					}

					position = true;
				}

				offset += size;
			}

			if (!position) {
				throw std::runtime_error(where + ": SV_Position is required");
			}

			if (vb.stride < offset || vb.stride % 4 != 0) {
				throw std::runtime_error(where + ": invalid vertex stride " + std::to_string(vb.stride));
			}

			if (vb.vertexCount == 0 || vb.data.size() != static_cast<uint64_t>(vb.stride) * vb.vertexCount) {
				throw std::runtime_error(where + ": the vertex data do not match the vertex count and stride");
			}

			if (vb.data.size() > std::numeric_limits<uint32_t>::max()) {
				throw std::runtime_error(where + " is larger than 4 GB");
			}
		}

		/// Vyplní deklaraci vrcholu v hlavičce a ukončí ji prvkem D3DDECL_END
		void FillDeclaration(D3DVertexElement9 (&target)[MAX_VERTEX_ELEMENTS], const std::vector<D3DVertexElement9> &decl)
		{
			std::memset(target, 0, sizeof(target));

			for (size_t i = 0; i < decl.size(); ++i) {
				target[i] = decl[i];
				target[i].Method = 0;
			}

			// D3DDECL_END()
			target[decl.size()].Stream = 0xFF;
			target[decl.size()].Offset = 0;
			target[decl.size()].Type = D3DDECLTYPE_UNUSED;
			target[decl.size()].Method = 0;
			target[decl.size()].Usage = 0;
			target[decl.size()].UsageIndex = 0;
		}

	}

	uint32_t ElementSize(uint8_t declType)
	{
		switch (declType) {
		case D3DDECLTYPE_FLOAT1:
			return 4;
		case D3DDECLTYPE_FLOAT2:
			return 8;
		case D3DDECLTYPE_FLOAT3:
			return 12;
		case D3DDECLTYPE_FLOAT4:
			return 16;
		case D3DDECLTYPE_D3DCOLOR:
		case D3DDECLTYPE_UBYTE4:
		case D3DDECLTYPE_UBYTE4N:
		case D3DDECLTYPE_FLOAT16_2:
		case D3DDECLTYPE_DEC3N:
		case D3DDECLTYPE_DXGI_R10G10B10A2_UNORM:
		case D3DDECLTYPE_DXGI_R11G11B10_FLOAT:
		case D3DDECLTYPE_DXGI_R8G8B8A8_SNORM:
			return 4;
		case D3DDECLTYPE_SHORT4N:
		case D3DDECLTYPE_FLOAT16_4:
			return 8;
		default:
			return 0;
		}
	}

	std::vector<uint8_t> SerializeModel(const Model &model)
	{
		if (model.version != FILE_VERSION && model.version != FILE_VERSION_V2) {
			throw std::runtime_error("Unsupported SDKMESH version " + std::to_string(model.version));
		}

		const size_t vbCount = model.vertexBuffers.size();
		const size_t ibCount = model.indexBuffers.size();
		const size_t meshCount = model.meshes.size();
		const size_t subsetCount = model.subsets.size();
		const size_t frameCount = model.frames.size();
		const size_t materialCount = model.materials.size();

		// Stejné požadavky jako CreateFromSDKMESH: aspoň jeden mesh, vertex buffer, index buffer, podmnožina a materiál
		if (meshCount == 0 || vbCount == 0 || ibCount == 0 || subsetCount == 0 || materialCount == 0) {
			throw std::runtime_error("An SDKMESH file needs at least one mesh, vertex buffer, index buffer, subset and material");
		}

		for (size_t i = 0; i < vbCount; ++i) {
			CheckVertexBuffer(model.vertexBuffers[i], i);
		}

		for (size_t i = 0; i < ibCount; ++i) {
			const IndexBuffer &ib = model.indexBuffers[i];
			const uint64_t bytes = static_cast<uint64_t>(ib.indices.size()) * (ib.wide ? 4 : 2);

			if (ib.indices.empty() || bytes > std::numeric_limits<uint32_t>::max()) {
				throw std::runtime_error("Index buffer " + std::to_string(i) + " is empty or larger than 4 GB");
			}

			if (!ib.wide) {
				for (const uint32_t index : ib.indices) {
					if (index > 0xFFFFu) {
						throw std::runtime_error("Index buffer " + std::to_string(i) + ": an index does not fit into 16 bits");
					}
				}
			}
		}

		for (size_t i = 0; i < subsetCount; ++i) {
			const Subset &subset = model.subsets[i];

			if (subset.materialId >= materialCount) {
				throw std::runtime_error("Subset " + std::to_string(i) + " refers to the missing material " + std::to_string(subset.materialId));
			}

			// Tessellační (patch) typy z Direct3D 9 DirectXTK odmítá
			if (subset.primitiveType > PT_LINE_STRIP_ADJ) {
				throw std::runtime_error("Subset " + std::to_string(i) + " has an unsupported primitive type");
			}
		}

		std::set<std::tuple<uint32_t, uint32_t, uint32_t>> checkedRanges;

		for (size_t i = 0; i < meshCount; ++i) {
			const Mesh &mesh = model.meshes[i];
			const std::string where = "Mesh " + std::to_string(i) + " ('" + mesh.name + "')";

			if (mesh.subsets.empty() || mesh.vertexBuffer >= vbCount || mesh.indexBuffer >= ibCount) {
				throw std::runtime_error(where + " is invalid (no subsets or a missing buffer)");
			}

			const VertexBuffer &vb = model.vertexBuffers[mesh.vertexBuffer];
			const IndexBuffer &ib = model.indexBuffers[mesh.indexBuffer];

			for (const uint32_t subsetIndex : mesh.subsets) {
				if (subsetIndex >= subsetCount) {
					throw std::runtime_error(where + " refers to the missing subset " + std::to_string(subsetIndex));
				}

				if (!checkedRanges.insert({subsetIndex, mesh.vertexBuffer, mesh.indexBuffer}).second) {
					continue;
				}

				const Subset &subset = model.subsets[subsetIndex];

				if (subset.indexCount == 0 || subset.indexStart > ib.indices.size() || subset.indexCount > ib.indices.size() - subset.indexStart) {
					throw std::runtime_error(where + ": the index range of subset " + std::to_string(subsetIndex) + " is outside of the index buffer");
				}

				if (subset.primitiveType == PT_TRIANGLE_LIST && subset.indexCount % 3 != 0) {
					throw std::runtime_error(where + ": the index count of subset " + std::to_string(subsetIndex) + " is not a multiple of 3");
				}

				for (uint64_t k = 0; k < subset.indexCount; ++k) {
					if (subset.vertexStart + ib.indices[subset.indexStart + k] >= vb.vertexCount) {
						throw std::runtime_error(where + ": an index of subset " + std::to_string(subsetIndex) + " points behind the vertex buffer");
					}
				}
			}

			for (const uint32_t influence : mesh.influences) {
				if (influence >= frameCount) {
					throw std::runtime_error(where + " has a bone influence on the missing frame " + std::to_string(influence));
				}
			}
		}

		for (size_t i = 0; i < frameCount; ++i) {
			const Frame &frame = model.frames[i];

			if ((frame.mesh != INVALID_MESH && frame.mesh >= meshCount) || (frame.parent != INVALID_FRAME && frame.parent >= frameCount) ||
				(frame.child != INVALID_FRAME && frame.child >= frameCount) || (frame.sibling != INVALID_FRAME && frame.sibling >= frameCount)) {
				throw std::runtime_error("Frame " + std::to_string(i) + " ('" + frame.name + "') has an invalid link");
			}

			// Cyklus v rodičích by DirectXTK při výpočtu světových matic nezvládl
			size_t steps = 0;

			for (uint32_t p = frame.parent; p != INVALID_FRAME; p = model.frames[p].parent) {
				if (++steps > frameCount) {
					throw std::runtime_error("The frame hierarchy contains a cycle at frame " + std::to_string(i));
				}
			}
		}

		// --- Rozložení souboru ------------------------------------------------------------------------------------
		const uint64_t headerSize = sizeof(Header) + vbCount * sizeof(VertexBufferHeader) + ibCount * sizeof(IndexBufferHeader);
		const uint64_t meshOffset = headerSize;
		const uint64_t subsetOffset = meshOffset + meshCount * sizeof(MeshRecord);
		const uint64_t frameOffset = subsetOffset + subsetCount * sizeof(SubsetRecord);
		const uint64_t materialOffset = frameOffset + frameCount * sizeof(FrameRecord);
		uint64_t cursor = materialOffset + materialCount * sizeof(MaterialRecord);

		/// Umístění polí podmnožin a ovlivňujících snímků jednoho meshe v souboru
		struct MeshArrays {
			uint64_t subsets;
			uint64_t influences;
		};

		std::vector<MeshArrays> arrays(meshCount);

		for (size_t i = 0; i < meshCount; ++i) {
			arrays[i].subsets = cursor;
			cursor += model.meshes[i].subsets.size() * sizeof(uint32_t);
			arrays[i].influences = cursor;
			cursor += model.meshes[i].influences.size() * sizeof(uint32_t);
		}

		cursor = AlignUp(cursor, 8);
		const uint64_t nonBufferSize = cursor - headerSize;
		const uint64_t bufferStart = cursor;

		std::vector<uint64_t> vbOffsets(vbCount), ibOffsets(ibCount);

		for (size_t i = 0; i < vbCount; ++i) {
			cursor = AlignUp(cursor, 8);
			vbOffsets[i] = cursor;
			cursor += model.vertexBuffers[i].data.size();
		}

		for (size_t i = 0; i < ibCount; ++i) {
			const IndexBuffer &ib = model.indexBuffers[i];
			cursor = AlignUp(cursor, 8);
			ibOffsets[i] = cursor;
			cursor += static_cast<uint64_t>(ib.indices.size()) * (ib.wide ? 4 : 2);
		}

		const uint64_t bufferSize = cursor - bufferStart;

		// --- Zápis --------------------------------------------------------------------------------------------------
		ExportUtil::ByteWriter out;

		Header header = Zeroed<Header>();
		header.Version = model.version;
		header.IsBigEndian = 0;
		header.HeaderSize = headerSize;
		header.NonBufferDataSize = nonBufferSize;
		header.BufferDataSize = bufferSize;
		header.NumVertexBuffers = static_cast<uint32_t>(vbCount);
		header.NumIndexBuffers = static_cast<uint32_t>(ibCount);
		header.NumMeshes = static_cast<uint32_t>(meshCount);
		header.NumTotalSubsets = static_cast<uint32_t>(subsetCount);
		header.NumFrames = static_cast<uint32_t>(frameCount);
		header.NumMaterials = static_cast<uint32_t>(materialCount);
		header.VertexStreamHeadersOffset = sizeof(Header);
		header.IndexStreamHeadersOffset = sizeof(Header) + vbCount * sizeof(VertexBufferHeader);
		header.MeshDataOffset = meshOffset;
		header.SubsetDataOffset = subsetOffset;
		header.FrameDataOffset = frameOffset;
		header.MaterialDataOffset = materialOffset;
		out.Append(header);

		for (size_t i = 0; i < vbCount; ++i) {
			const VertexBuffer &vb = model.vertexBuffers[i];
			VertexBufferHeader vh = Zeroed<VertexBufferHeader>();
			vh.NumVertices = vb.vertexCount;
			vh.SizeBytes = vb.data.size();
			vh.StrideBytes = vb.stride;
			FillDeclaration(vh.Decl, vb.decl);
			vh.DataOffset = vbOffsets[i];
			out.Append(vh);
		}

		for (size_t i = 0; i < ibCount; ++i) {
			const IndexBuffer &ib = model.indexBuffers[i];
			IndexBufferHeader ih = Zeroed<IndexBufferHeader>();
			ih.NumIndices = ib.indices.size();
			ih.SizeBytes = static_cast<uint64_t>(ib.indices.size()) * (ib.wide ? 4 : 2);
			ih.IndexType = ib.wide ? IT_32BIT : IT_16BIT;
			ih.DataOffset = ibOffsets[i];
			out.Append(ih);
		}

		for (size_t i = 0; i < meshCount; ++i) {
			const Mesh &mesh = model.meshes[i];
			MeshRecord record = Zeroed<MeshRecord>();
			SetText(record.Name, mesh.name, "Mesh name");
			record.NumVertexBuffers = 1;
			record.VertexBuffers[0] = mesh.vertexBuffer;
			record.IndexBuffer = mesh.indexBuffer;
			record.NumSubsets = static_cast<uint32_t>(mesh.subsets.size());
			record.NumFrameInfluences = static_cast<uint32_t>(mesh.influences.size());
			record.BoundingBoxCenter = mesh.boundingBoxCenter;
			record.BoundingBoxExtents = mesh.boundingBoxExtents;
			record.SubsetOffset = arrays[i].subsets;
			record.FrameInfluenceOffset = arrays[i].influences;
			out.Append(record);
		}

		for (const Subset &subset : model.subsets) {
			SubsetRecord record = Zeroed<SubsetRecord>();
			SetText(record.Name, subset.name, "Subset name");
			record.MaterialID = subset.materialId;
			record.PrimitiveType = subset.primitiveType;
			record.IndexStart = subset.indexStart;
			record.IndexCount = subset.indexCount;
			record.VertexStart = subset.vertexStart;
			record.VertexCount = subset.vertexCount;
			out.Append(record);
		}

		for (const Frame &frame : model.frames) {
			FrameRecord record = Zeroed<FrameRecord>();
			SetText(record.Name, frame.name, "Frame name");
			record.Mesh = frame.mesh;
			record.ParentFrame = frame.parent;
			record.ChildFrame = frame.child;
			record.SiblingFrame = frame.sibling;
			record.Matrix = frame.matrix;
			record.AnimationDataIndex = INVALID_ANIMATION_DATA;
			out.Append(record);
		}

		for (const Material &material : model.materials) {

			if (model.version == FILE_VERSION_V2) {
				MaterialRecordV2 record = Zeroed<MaterialRecordV2>();
				SetText(record.Name, material.name, "Material name");
				SetText(record.RMATexture, material.rmaTexture, "Texture name");
				SetText(record.AlbedoTexture, material.albedoTexture, "Texture name");
				SetText(record.NormalTexture, material.normalTexture, "Texture name");
				SetText(record.EmissiveTexture, material.emissiveTexture, "Texture name");
				record.Alpha = material.alpha;
				out.Append(record);
			}
			else {
				MaterialRecord record = Zeroed<MaterialRecord>();
				SetText(record.Name, material.name, "Material name");
				SetText(record.DiffuseTexture, material.diffuseTexture, "Texture name");
				SetText(record.NormalTexture, material.normalTexture, "Texture name");
				SetText(record.SpecularTexture, material.specularTexture, "Texture name");
				record.Diffuse = material.diffuse;
				record.Ambient = material.ambient;
				record.Specular = material.specular;
				record.Emissive = material.emissive;
				record.Power = material.power;
				out.Append(record);
			}
		}

		for (const Mesh &mesh : model.meshes) {
			out.Records(mesh.subsets);
			out.Records(mesh.influences);
		}

		out.PadTo(8);

		if (out.Size() != bufferStart) {
			throw std::runtime_error("Internal error: the SDKMESH layout does not match (" + std::to_string(out.Size()) + " != " + std::to_string(bufferStart) + ")");
		}

		for (size_t i = 0; i < vbCount; ++i) {
			out.PadTo(8);

			if (out.Size() != vbOffsets[i]) {
				throw std::runtime_error("Internal error: the vertex buffer offset does not match");
			}

			out.Records(model.vertexBuffers[i].data);
		}

		for (size_t i = 0; i < ibCount; ++i) {
			const IndexBuffer &ib = model.indexBuffers[i];
			out.PadTo(8);

			if (out.Size() != ibOffsets[i]) {
				throw std::runtime_error("Internal error: the index buffer offset does not match");
			}

			if (ib.wide) {
				out.Records(ib.indices);
			}
			else {
				std::vector<uint16_t> narrow(ib.indices.size());

				for (size_t k = 0; k < narrow.size(); ++k) {
					narrow[k] = static_cast<uint16_t>(ib.indices[k]);
				}

				out.Records(narrow);
			}
		}

		return out.Take();
	}

	std::vector<uint8_t> SerializeAnimation(const Animation &animation)
	{
		if (animation.tracks.empty()) {
			throw std::runtime_error("Animation '" + animation.name + "' has no tracks");
		}

		// DXUT přehrává klíče 1 .. N-1 a bere zbytek po dělení (N - 1), proto jsou potřeba aspoň dva klíče
		if (animation.keyCount < 2 || animation.fps == 0) {
			throw std::runtime_error("Animation '" + animation.name + "' needs at least 2 keys and a positive FPS");
		}

		for (const AnimationTrack &track : animation.tracks) {
			if (track.keys.size() != animation.keyCount) {
				throw std::runtime_error("Animation '" + animation.name + "': the track of the frame '" + track.frameName + "' has a different number of keys");
			}
		}

		const uint64_t tableBytes = static_cast<uint64_t>(animation.tracks.size()) * sizeof(AnimationFrameData);
		const uint64_t keyBytes = static_cast<uint64_t>(animation.keyCount) * sizeof(AnimationKeyRecord);
		const uint64_t dataSize = tableBytes + keyBytes * animation.tracks.size();

		if (dataSize > std::numeric_limits<uint32_t>::max()) {
			throw std::runtime_error("Animation '" + animation.name + "' is larger than 4 GB");
		}

		ExportUtil::ByteWriter out;

		AnimationFileHeader header = Zeroed<AnimationFileHeader>();
		header.Version = FILE_VERSION;
		header.IsBigEndian = 0;
		header.FrameTransformType = FTT_RELATIVE;
		header.NumFrames = static_cast<uint32_t>(animation.tracks.size());
		header.NumAnimationKeys = animation.keyCount;
		header.AnimationFPS = animation.fps;
		header.AnimationDataSize = dataSize;
		header.AnimationDataOffset = sizeof(AnimationFileHeader);
		out.Append(header);

		for (size_t i = 0; i < animation.tracks.size(); ++i) {
			AnimationFrameData record = Zeroed<AnimationFrameData>();
			SetText(record.FrameName, animation.tracks[i].frameName, "Frame name");
			record.DataOffset = tableBytes + keyBytes * i;
			out.Append(record);
		}

		for (const AnimationTrack &track : animation.tracks) {
			for (const AnimationKeyRecord &key : track.keys) {
				out.Append(key);
			}
		}

		return out.Take();
	}
}
