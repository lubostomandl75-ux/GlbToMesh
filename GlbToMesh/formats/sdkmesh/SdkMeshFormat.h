/**
 * @file      SdkMeshFormat.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Binární struktury souborů SDKMESH (.sdkmesh) a SDKANIMATION (.sdkmesh_anim)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Struktury odpovídají hlavičce SDKMesh.h z knihovny DirectXTK (MIT licence, Microsoft), ale nezávisí na
 * DirectXMath ani Direct3D, takže se nástroj dá přeložit i mimo Windows. Velikosti struktur hlídají static_assert
 * stejně jako v originále.
 */

#pragma once

// Hlavičkové soubory C++
#include <cstdint>

namespace SdkMesh {

	/// Jednoduché vektorové typy místo DirectX::XMFLOAT*
	struct Float3 {
		float x, y, z;
	};

	/// Čtveřice čísel (místo DirectX::XMFLOAT4)
	struct Float4 {
		float x, y, z, w;
	};

	/// Matice tak, jak leží v souboru (Direct3D, řádkové vektory). Pamět je shodná s glTF maticí (sloupcové pole,
	/// sloupcové vektory), proto se při načtení nijak netransponuje: posun je vždy na indexech 12 - 14.
	struct Float4x4 {
		float m[16];
	};

	// Konstanty formátu
	constexpr uint32_t FILE_VERSION = 101;
	constexpr uint32_t FILE_VERSION_V2 = 200;

	constexpr uint32_t MAX_VERTEX_ELEMENTS = 32;
	constexpr uint32_t MAX_VERTEX_STREAMS = 16;
	constexpr uint32_t MAX_FRAME_NAME = 100;
	constexpr uint32_t MAX_MESH_NAME = 100;
	constexpr uint32_t MAX_SUBSET_NAME = 100;
	constexpr uint32_t MAX_MATERIAL_NAME = 100;
	constexpr uint32_t MAX_PATH_LENGTH = 260; // MAX_PATH ve Windows, v souboru je pevná hodnota
	constexpr uint32_t MAX_TEXTURE_NAME = MAX_PATH_LENGTH;
	constexpr uint32_t MAX_MATERIAL_PATH = MAX_PATH_LENGTH;
	constexpr uint32_t INVALID_FRAME = 0xFFFFFFFFu;
	constexpr uint32_t INVALID_MESH = 0xFFFFFFFFu;
	constexpr uint32_t INVALID_ANIMATION_DATA = 0xFFFFFFFFu;

	/// Direct3D 9 deklarace vrcholů (SDKMESH používá D3D9 deklarace i pro Direct3D 11)
	enum D3DDECLUSAGE : uint8_t {
		D3DDECLUSAGE_POSITION = 0,
		D3DDECLUSAGE_BLENDWEIGHT = 1,
		D3DDECLUSAGE_BLENDINDICES = 2,
		D3DDECLUSAGE_NORMAL = 3,
		D3DDECLUSAGE_TEXCOORD = 5,
		D3DDECLUSAGE_TANGENT = 6,
		D3DDECLUSAGE_BINORMAL = 7,
		D3DDECLUSAGE_COLOR = 10,
	};

	// Hodnoty DXGI_FORMAT, které SDKMESH používá jako rozšíření typu (typ = 32 + DXGI_FORMAT)
	constexpr uint8_t DXGI_R10G10B10A2_UNORM = 24;
	constexpr uint8_t DXGI_R11G11B10_FLOAT = 26;
	constexpr uint8_t DXGI_R8G8B8A8_SNORM = 31;
	constexpr uint8_t DXGI_R10G10B10_SNORM_A2_UNORM = 189; // jen Xbox One XDK

	/// Typ prvku deklarace vrcholu (Direct3D 9, rozšířený o formáty DXGI)
	enum D3DDECLTYPE : uint8_t {
		D3DDECLTYPE_FLOAT1 = 0,
		D3DDECLTYPE_FLOAT2 = 1,
		D3DDECLTYPE_FLOAT3 = 2,
		D3DDECLTYPE_FLOAT4 = 3,
		D3DDECLTYPE_D3DCOLOR = 4, // 4 bajty v pořadí B, G, R, A
		D3DDECLTYPE_UBYTE4 = 5,
		D3DDECLTYPE_UBYTE4N = 8,
		D3DDECLTYPE_SHORT4N = 10,
		D3DDECLTYPE_DEC3N = 14, // 3 x 10 bitů se znaménkem, normalizováno / 511
		D3DDECLTYPE_FLOAT16_2 = 15,
		D3DDECLTYPE_FLOAT16_4 = 16,
		D3DDECLTYPE_UNUSED = 17,
		D3DDECLTYPE_DXGI_R10G10B10A2_UNORM = 32 + DXGI_R10G10B10A2_UNORM,
		D3DDECLTYPE_DXGI_R11G11B10_FLOAT = 32 + DXGI_R11G11B10_FLOAT,
		D3DDECLTYPE_DXGI_R8G8B8A8_SNORM = 32 + DXGI_R8G8B8A8_SNORM,
		D3DDECLTYPE_DXGI_R10G10B10_SNORM_A2_UNORM = 32 + DXGI_R10G10B10_SNORM_A2_UNORM,
	};

	/// Druh primitiv podmnožiny
	enum PrimitiveType : uint32_t {
		PT_TRIANGLE_LIST = 0,
		PT_TRIANGLE_STRIP,
		PT_LINE_LIST,
		PT_LINE_STRIP,
		PT_POINT_LIST,
		PT_TRIANGLE_LIST_ADJ,
		PT_TRIANGLE_STRIP_ADJ,
		PT_LINE_LIST_ADJ,
		PT_LINE_STRIP_ADJ,
		PT_QUAD_PATCH_LIST,
		PT_TRIANGLE_PATCH_LIST,
	};

	/// Šířka indexů index bufferu
	enum IndexType : uint32_t {
		IT_16BIT = 0,
		IT_32BIT,
	};

	/// Způsob uložení transformací snímků v animaci
	enum FrameTransformType : uint32_t {
		FTT_RELATIVE = 0,
		FTT_ABSOLUTE, // ve formátu rezervováno pro budoucnost, DXUT ho nepodporuje
	};

#pragma pack(push, 4)
	/// Prvek deklarace vrcholu (D3DVERTEXELEMENT9)
	struct D3DVertexElement9 {
		uint16_t Stream;	 // Index streamu
		uint16_t Offset;	 // Posun v bajtech od začátku vrcholu
		uint8_t Type;		 // D3DDECLTYPE
		uint8_t Method;		 // Způsob zpracování (nepoužito)
		uint8_t Usage;		 // D3DDECLUSAGE
		uint8_t UsageIndex;	 // Index sémantiky
	};
#pragma pack(pop)

#pragma pack(push, 8)

	/// Hlavička souboru .sdkmesh
	struct Header {
		uint32_t Version;
		uint8_t IsBigEndian;
		uint64_t HeaderSize;
		uint64_t NonBufferDataSize;
		uint64_t BufferDataSize;

		uint32_t NumVertexBuffers;
		uint32_t NumIndexBuffers;
		uint32_t NumMeshes;
		uint32_t NumTotalSubsets;
		uint32_t NumFrames;
		uint32_t NumMaterials;

		uint64_t VertexStreamHeadersOffset;
		uint64_t IndexStreamHeadersOffset;
		uint64_t MeshDataOffset;
		uint64_t SubsetDataOffset;
		uint64_t FrameDataOffset;
		uint64_t MaterialDataOffset;
	};

	/// Hlavička vertex bufferu: počet vrcholů, velikost, krok, deklarace a umístění dat
	struct VertexBufferHeader {
		uint64_t NumVertices;
		uint64_t SizeBytes;
		uint64_t StrideBytes;
		D3DVertexElement9 Decl[MAX_VERTEX_ELEMENTS];
		uint64_t DataOffset;
	};

	/// Hlavička index bufferu: počet indexů, velikost, šířka a umístění dat
	struct IndexBufferHeader {
		uint64_t NumIndices;
		uint64_t SizeBytes;
		uint32_t IndexType;
		uint64_t DataOffset;
	};

	/// Záznam meshe: buffery, podmnožiny, ovlivňující snímky a ohraničující kvádr
	struct MeshRecord {
		char Name[MAX_MESH_NAME];
		uint8_t NumVertexBuffers;
		uint32_t VertexBuffers[MAX_VERTEX_STREAMS];
		uint32_t IndexBuffer;
		uint32_t NumSubsets;
		uint32_t NumFrameInfluences; // počet kostí, které mesh ovlivňují

		Float3 BoundingBoxCenter;
		Float3 BoundingBoxExtents;

		uint64_t SubsetOffset;		   // pole uint32_t: indexy podmnožin
		uint64_t FrameInfluenceOffset; // pole uint32_t: indexy snímků (kostí)
	};

	/// Záznam podmnožiny: rozsah indexů a vrcholů s jedním materiálem
	struct SubsetRecord {
		char Name[MAX_SUBSET_NAME];
		uint32_t MaterialID;
		uint32_t PrimitiveType;
		uint64_t IndexStart;
		uint64_t IndexCount;
		uint64_t VertexStart; // základní vrchol (base vertex), přičítá se k indexům
		uint64_t VertexCount;
	};

	/// Záznam snímku (uzlu hierarchie): mesh, rodič, potomek, sourozenec a matice
	struct FrameRecord {
		char Name[MAX_FRAME_NAME];
		uint32_t Mesh;
		uint32_t ParentFrame;
		uint32_t ChildFrame;
		uint32_t SiblingFrame;
		Float4x4 Matrix;
		uint32_t AnimationDataIndex;
	};

	/// Materiál verze 101 (Blinn-Phong)
	struct MaterialRecord {
		char Name[MAX_MATERIAL_NAME];
		char MaterialInstancePath[MAX_MATERIAL_PATH];
		char DiffuseTexture[MAX_TEXTURE_NAME];
		char NormalTexture[MAX_TEXTURE_NAME];
		char SpecularTexture[MAX_TEXTURE_NAME];

		Float4 Diffuse;
		Float4 Ambient;
		Float4 Specular;
		Float4 Emissive;
		float Power;

		uint64_t Force64_1;
		uint64_t Force64_2;
		uint64_t Force64_3;
		uint64_t Force64_4;
		uint64_t Force64_5;
		uint64_t Force64_6;
	};

	/// Materiál verze 200 (PBR). Textura RMA má kanály už ve vrstvení glTF: R = okluze, G = drsnost, B = kovovost.
	struct MaterialRecordV2 {
		char Name[MAX_MATERIAL_NAME];
		char RMATexture[MAX_TEXTURE_NAME];
		char AlbedoTexture[MAX_TEXTURE_NAME];
		char NormalTexture[MAX_TEXTURE_NAME];
		char EmissiveTexture[MAX_TEXTURE_NAME];

		float Alpha;

		char Reserved[60];

		uint64_t Force64_1;
		uint64_t Force64_2;
		uint64_t Force64_3;
		uint64_t Force64_4;
		uint64_t Force64_5;
		uint64_t Force64_6;
	};

	/// Soubor .sdkmesh_anim: AnimationFileHeader, za ním AnimationFrameData[NumFrames] na AnimationDataOffset
	/// (od začátku souboru) a pak pole klíčů; DataOffset každého snímku se počítá od konce hlavičky.
	struct AnimationFileHeader {
		uint32_t Version;
		uint8_t IsBigEndian;
		uint32_t FrameTransformType;
		uint32_t NumFrames;
		uint32_t NumAnimationKeys;
		uint32_t AnimationFPS;
		uint64_t AnimationDataSize;
		uint64_t AnimationDataOffset;
	};

	/// Klíč animace: posun, rotace (kvaternion) a měřítko
	struct AnimationKeyRecord {
		Float3 Translation;
		Float4 Orientation; // kvaternion (x, y, z, w)
		Float3 Scaling;
	};

	/// Odkaz na klíče animace jednoho snímku
	struct AnimationFrameData {
		char FrameName[MAX_FRAME_NAME];
		uint64_t DataOffset;
	};

#pragma pack(pop)

	static_assert(sizeof(D3DVertexElement9) == 8, "D3D9 decl structure size incorrect");
	static_assert(sizeof(Header) == 104, "SDKMESH header size incorrect");
	static_assert(sizeof(VertexBufferHeader) == 288, "SDKMESH vertex buffer header size incorrect");
	static_assert(sizeof(IndexBufferHeader) == 32, "SDKMESH index buffer header size incorrect");
	static_assert(sizeof(MeshRecord) == 224, "SDKMESH mesh size incorrect");
	static_assert(sizeof(SubsetRecord) == 144, "SDKMESH subset size incorrect");
	static_assert(sizeof(FrameRecord) == 184, "SDKMESH frame size incorrect");
	static_assert(sizeof(MaterialRecord) == 1256, "SDKMESH material size incorrect");
	static_assert(sizeof(MaterialRecordV2) == sizeof(MaterialRecord), "SDKMESH material v2 size incorrect");
	static_assert(sizeof(AnimationFileHeader) == 40, "SDKANIMATION header size incorrect");
	static_assert(sizeof(AnimationKeyRecord) == 40, "SDKANIMATION key size incorrect");
	static_assert(sizeof(AnimationFrameData) == 112, "SDKANIMATION frame data size incorrect");
}
