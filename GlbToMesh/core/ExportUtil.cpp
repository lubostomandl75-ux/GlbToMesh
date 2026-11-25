/**
 * @file      ExportUtil.cpp
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Implementace společného základu převodníků (viz ExportUtil.h)
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 */

// Hlavičkové soubory C++
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

// Hlavičkové soubory projektu
#include "ExportUtil.h"
#include "Log.h"
#include "TextUtil.h"

namespace ExportUtil {

	using MathUtil::Mat4;
	using MathUtil::Quat;
	using MathUtil::Vec3;

	float LinearToSrgb(float c)
	{
		c = std::clamp(c, 0.0f, 1.0f);

		return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	}

	float Clamp01(float v)
	{
		return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
	}

	uint32_t PackColor(const float *rgba)
	{
		uint32_t packed = 0;

		for (size_t c = 0; c < 4; ++c) {
			packed |= static_cast<uint32_t>(std::lround(Clamp01(rgba[c]) * 255.0f)) << (8 * c);
		}

		return packed;
	}

	void SceneConverter::WarnOnce(const std::string &key, const std::string &message)
	{
		if (this->warned.insert(key).second) {
			Log::Warn(message);
		}
	}

	PhongMaterial MakePhongMaterial(const Gltf::Material &src, float alpha, bool srgbColors, float powerWithoutSpecular)
	{
		const auto color = [srgbColors](float c) { return srgbColors ? LinearToSrgb(c) : c; };

		float diffuse[3];

		for (size_t c = 0; c < 3; ++c) {
			diffuse[c] = Clamp01(src.baseColor[c]);
		}

		// Odlesk: z drsnosti (Blinn-Phong: roughness = sqrt(2 / (n + 2))) a základní odrazivosti; matné materiály bez odlesku
		float specular[3] = {0.0f, 0.0f, 0.0f};
		float power = powerWithoutSpecular;
		float roughness = src.roughness;

		if (src.specularGlossiness) {
			roughness = 1.0f - Clamp01(src.glossiness);

			for (size_t c = 0; c < 3; ++c) {
				specular[c] = Clamp01(src.specular[c]);
			}
		}
		else {
			const float metallic = Clamp01(src.metallic);

			for (size_t c = 0; c < 3; ++c) {
				specular[c] = 0.04f * (1.0f - metallic) + diffuse[c] * metallic;
			}
		}

		roughness = Clamp01(roughness);

		if (src.unlit || roughness >= 0.95f) {
			specular[0] = specular[1] = specular[2] = 0.0f;
		}
		else {
			power = std::clamp(2.0f / (std::max(roughness, 0.045f) * std::max(roughness, 0.045f)) - 2.0f, 1.0f, 1000.0f);
		}

		PhongMaterial m;

		for (size_t c = 0; c < 3; ++c) {
			m.diffuse[c] = color(diffuse[c]);
			m.ambient[c] = color(diffuse[c] * 0.2f);
			m.specular[c] = color(specular[c]);
			m.emissive[c] = std::isfinite(src.emissive[c]) ? color(std::max(src.emissive[c], 0.0f)) : 0.0f;
		}

		m.diffuse[3] = alpha;
		m.ambient[3] = 1.0f; // ambient.w = 1: blok barev není "nevyplněný"
		m.specular[3] = 1.0f;
		m.emissive[3] = 0.0f;
		m.power = power;

		return m;
	}

	float SceneConverter::MaterialAlpha(const Gltf::Material &src, const std::string &keyName, const std::string &displayName, const std::string &formatName)
	{
		if (src.alphaMode == Gltf::AlphaMode::Blend) {
			return Clamp01(src.baseColor[3]);
		}

		if (src.alphaMode == Gltf::AlphaMode::Mask) {
			this->WarnOnce("mask:" + keyName, "Material '" + displayName + "' uses alphaMode MASK, which " + formatName + " does not support - exported as opaque.");
		}

		return 1.0f;
	}

	UvTransform MakeUvTransform(const Gltf::TextureRef &ref)
	{
		UvTransform t;

		if (ref.hasTransform) {
			const double c = std::cos(ref.rotation), s = std::sin(ref.rotation);
			t.m[0] = c * ref.scale[0];
			t.m[1] = s * ref.scale[1];
			t.m[2] = ref.offset[0];
			t.m[3] = -s * ref.scale[0];
			t.m[4] = c * ref.scale[1];
			t.m[5] = ref.offset[1];
		}

		return t;
	}

	bool SameUvTransform(const Gltf::TextureRef &a, const Gltf::TextureRef &b)
	{
		return a.offset[0] == b.offset[0] && a.offset[1] == b.offset[1] && a.rotation == b.rotation && a.scale[0] == b.scale[0] && a.scale[1] == b.scale[1];
	}

	const Gltf::TextureRef *SceneConverter::PickUvTransform(
		std::initializer_list<const Gltf::TextureRef *> refs, const std::function<bool(const Gltf::TextureRef &)> &accept, const std::string &name, const std::string &reason
	)
	{
		const Gltf::TextureRef *source = nullptr;

		for (const Gltf::TextureRef *ref : refs) {

			if (ref->image < 0 || !ref->hasTransform || (accept && !accept(*ref))) {
				continue;
			}

			if (!source) {
				source = ref;
			}
			else if (!SameUvTransform(*ref, *source)) {
				this->WarnOnce("uvdiff:" + name, "Material '" + name + "': the textures have different UV transforms, but " + reason + " - the first one is used.");
			}
		}

		return source;
	}

	void SceneConverter::WarnGeometryLimits(const std::string &formatName)
	{
		if (this->scene.morphTargetPrimitives > 0) {
			this->WarnOnce("morph", std::to_string(this->scene.morphTargetPrimitives) + " primitives have morph targets, which " + formatName + " does not support - only the base shape is exported.");
		}

		size_t extraInfluences = 0;

		for (const Gltf::Mesh &mesh : this->scene.meshes) {

			for (const Gltf::Primitive &primitive : mesh.primitives) {
				extraInfluences += primitive.extraInfluences;
			}
		}

		if (extraInfluences > 0) {
			this->WarnOnce("influences", std::to_string(extraInfluences) + " vertices have more than 4 bone influences; " + formatName + " keeps the 4 strongest.");
		}
	}

	void SceneConverter::WarnUnsupportedPbrTextures(const Gltf::Material &src, const std::string &keyName, const std::string &displayName, const std::string &formatName)
	{
		if (src.metallicRoughnessTexture.image >= 0 || src.occlusionTexture.image >= 0 || src.specularGlossinessTexture.image >= 0) {
			this->WarnOnce(
				"pbrtex:" + keyName, "Material '" + displayName + "': the metallic-roughness, occlusion and specular-glossiness textures have no equivalent in " + formatName + " and are not exported."
			);
		}
	}

	void SceneConverter::WarnTexCoordSet(std::initializer_list<const Gltf::TextureRef *> refs, const std::string &keyName, const std::string &displayName, const std::string &reason)
	{
		for (const Gltf::TextureRef *ref : refs) {

			if (ref->image >= 0 && ref->texCoord != 0) {
				this->WarnOnce("uvset:" + keyName, "Material '" + displayName + "' uses a texture coordinate set other than 0, but " + reason + " - TEXCOORD_0 is used.");
			}
		}
	}

	void SceneConverter::NoteUnlit(const Gltf::Material &src, const std::string &displayName)
	{
		if (src.unlit) {
			Log::Detail("Material '" + displayName + "' is unlit (KHR_materials_unlit) - exported as a standard material without a specular highlight.");
		}
	}

	Mat4 SceneConverter::Mirror(const Mat4 &m) const
	{
		return this->leftHanded ? MathUtil::MirrorZ(m) : m;
	}

	void SceneConverter::ComputeWorld()
	{
		const size_t count = this->scene.nodes.size();
		this->world.assign(count, MathUtil::Identity());
		std::vector<bool> done(count, false);

		for (size_t start = 0; start < count; ++start) {
			std::vector<size_t> chain;
			int current = static_cast<int>(start);

			while (current >= 0 && !done[static_cast<size_t>(current)]) {
				chain.push_back(static_cast<size_t>(current));
				current = this->scene.nodes[static_cast<size_t>(current)].parent;
			}

			for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
				const Gltf::Node &node = this->scene.nodes[*it];
				this->world[*it] = node.parent < 0 ? node.local : MathUtil::Multiply(this->world[static_cast<size_t>(node.parent)], node.local);
				done[*it] = true;
			}
		}
	}

	std::vector<bool> SceneConverter::AnimatedNodes() const
	{
		const size_t nodeCount = this->scene.nodes.size();
		std::vector<bool> animated(nodeCount, false);

		for (const Gltf::Animation &animation : this->scene.animations) {

			for (const Gltf::Channel &channel : animation.channels) {

				if (channel.node >= 0 && static_cast<size_t>(channel.node) < nodeCount && !channel.times.empty()) {
					animated[static_cast<size_t>(channel.node)] = true;
				}
			}
		}

		return animated;
	}

	const Gltf::Material &SceneConverter::SourceMaterial(int index) const
	{
		return index >= 0 ? this->scene.materials[static_cast<size_t>(index)] : this->defaultMaterial;
	}

	// ---------------------------------------------------------------------------------------------------
	// Textury

	std::string SceneConverter::TextureFile(int image, const std::string &materialName)
	{
		if (image < 0 || this->noTextures) {
			return std::string();
		}

		const auto cached = this->textureFiles.find(image);

		if (cached != this->textureFiles.end()) {
			return cached->second;
		}

		const Gltf::Image &src = this->scene.images[static_cast<size_t>(image)];
		std::string &fileName = this->textureFiles[image];

		if (src.extension.empty()) {
			Log::Warn("Material '" + materialName + "': the texture has an unknown image format - exported without it.");

			return fileName;
		}

		if (src.extension == ".webp" || src.extension == ".ktx2") {
			this->WarnOnce("format" + src.extension, "Textures in the format " + src.extension + " are written as they are, but DirectXTK cannot load them - convert them to PNG or DDS.");
		}

		// Název: z názvu obrázku bez přípony, jinak z názvu modelu a pořadí obrázku
		std::string stem = src.name;
		const std::string lower = TextUtil::ToLowerAscii(stem);

		if (lower.size() > src.extension.size() && lower.compare(lower.size() - src.extension.size(), src.extension.size(), src.extension) == 0) {
			stem.erase(stem.size() - src.extension.size());
		}

		stem = TextUtil::SanitizeFileName(stem, this->modelName + "_texture" + std::to_string(image));
		const std::string candidate = TextUtil::UniqueName(this->usedFileNames, stem, src.extension);

		this->CheckTextureFileName(candidate);

		Formats::OutputFile file;
		file.fileName = candidate;
		file.data = src.data;
		this->textures.push_back(std::move(file));
		fileName = candidate;

		return fileName;
	}

	void SceneConverter::CheckTextureFileName(const std::string &fileName) const
	{
		(void)fileName;
	}

	// ---------------------------------------------------------------------------------------------------
	// Animace a geometrie

	/// Hodnota kanálu v čase; mimo rozsah klíčů platí první a poslední hodnota. Výstup: 3 (posun, měřítko) nebo 4 (rotace) čísla
	void SceneConverter::SampleChannel(const Gltf::Channel &channel, double time, double *out) const
	{
		const size_t stride = channel.path == Gltf::Path::Rotation ? 4 : 3;
		const bool cubic = channel.interpolation == Gltf::Interpolation::CubicSpline;
		const size_t count = channel.times.size();

		// which: 0 = vstupní tečna, 1 = hodnota, 2 = výstupní tečna (jen kubická interpolace)
		const auto at = [&](size_t key, size_t which, size_t component) -> double {
			return channel.values[(cubic ? key * 3 + which : key) * stride + component];
		};

		const auto copy = [&](size_t key) {
			for (size_t c = 0; c < stride; ++c) {
				out[c] = at(key, 1, c);
			}
		};

		if (time <= channel.times.front()) {
			copy(0);

			return;
		}

		if (time >= channel.times.back()) {
			copy(count - 1);

			return;
		}

		const size_t k = static_cast<size_t>(std::upper_bound(channel.times.begin(), channel.times.end(), static_cast<float>(time)) - channel.times.begin()) - 1;
		const size_t next = std::min(k + 1, count - 1);
		const double t0 = channel.times[k], t1 = channel.times[next];
		const double dt = t1 - t0;

		if (!(dt > 0.0) || channel.interpolation == Gltf::Interpolation::Step) {
			copy(k);

			return;
		}

		const double s = std::clamp((time - t0) / dt, 0.0, 1.0);

		if (channel.interpolation == Gltf::Interpolation::Linear) {

			if (channel.path == Gltf::Path::Rotation) {
				const Quat a = MathUtil::NormalizeQuat({at(k, 1, 0), at(k, 1, 1), at(k, 1, 2), at(k, 1, 3)});
				const Quat b = MathUtil::NormalizeQuat({at(next, 1, 0), at(next, 1, 1), at(next, 1, 2), at(next, 1, 3)});
				const Quat q = MathUtil::Slerp(a, b, s);
				out[0] = q.x;
				out[1] = q.y;
				out[2] = q.z;
				out[3] = q.w;
			}
			else {

				for (size_t c = 0; c < stride; ++c) {
					out[c] = at(k, 1, c) * (1.0 - s) + at(next, 1, c) * s;
				}
			}

			return;
		}

		// Kubický Hermitův spline podle specifikace glTF
		const double s2 = s * s, s3 = s2 * s;
		const double h00 = 2.0 * s3 - 3.0 * s2 + 1.0, h10 = s3 - 2.0 * s2 + s, h01 = -2.0 * s3 + 3.0 * s2, h11 = s3 - s2;

		for (size_t c = 0; c < stride; ++c) {
			out[c] = h00 * at(k, 1, c) + h10 * dt * at(k, 2, c) + h01 * at(next, 1, c) + h11 * dt * at(next, 0, c);
		}

		if (channel.path == Gltf::Path::Rotation) {
			const Quat q = MathUtil::NormalizeQuat({out[0], out[1], out[2], out[3]});
			out[0] = q.x;
			out[1] = q.y;
			out[2] = q.z;
			out[3] = q.w;
		}
	}

	Trs SceneConverter::SampleNode(int node, const std::unordered_map<int, NodeAnimation> &animation, double time) const
	{
		const Gltf::Node &src = this->scene.nodes[static_cast<size_t>(node)];
		Trs trs;
		trs.t = src.translation;
		trs.q = src.rotation;
		trs.s = src.scale;

		const auto found = animation.find(node);

		if (found == animation.end()) {
			return trs;
		}

		double v[4];

		if (found->second.translation) {
			this->SampleChannel(*found->second.translation, time, v);
			trs.t = {v[0], v[1], v[2]};
		}

		if (found->second.rotation) {
			this->SampleChannel(*found->second.rotation, time, v);
			trs.q = MathUtil::NormalizeQuat({v[0], v[1], v[2], v[3]});
		}

		if (found->second.scale) {
			this->SampleChannel(*found->second.scale, time, v);
			trs.s = {v[0], v[1], v[2]};
		}

		return trs;
	}

	/// Doplní normály (plošné), tangenty a barvy a převede data do podoby, kterou potřebuje zápis vertexů
	Prepared SceneConverter::Prepare(const Gltf::Primitive &src) const
	{
		Prepared p;
		p.vertexCount = src.vertexCount;
		p.triangles = src.triangles;

		const std::vector<float> *colors = &src.colors; // barvy RGBA 0 - 1 v pořadí vrcholů p (nebo prázdné)
		std::vector<float> expandedColors;

		if (src.normals.empty()) {
			// Bez normál glTF předepisuje plošné stínování: vrcholy se rozdělí a každý trojúhelník dostane vlastní normálu
			p.vertexCount = src.triangles.size() / 3 * 3;

			// Atribut rozepsaný do vrcholů jednotlivých trojúhelníků (prázdný atribut zůstane prázdný)
			const auto expand = [&](const auto &from, auto &to, size_t components) {
				if (from.empty()) {
					return;
				}

				to.resize(p.vertexCount * components);

				for (size_t v = 0; v < p.vertexCount; ++v) {
					std::copy_n(&from[src.triangles[v] * components], components, &to[v * components]);
				}
			};

			expand(src.positions, p.positions, 3);
			expand(src.texCoords, p.texCoords, 2);
			expand(src.tangents, p.tangents, 4);
			expand(src.colors, expandedColors, 4);
			expand(src.joints, p.joints, 4);
			expand(src.weights, p.weights, 4);
			colors = &expandedColors;

			p.triangles.resize(p.vertexCount);
			p.normals.resize(p.vertexCount * 3);

			for (size_t v = 0; v < p.vertexCount; ++v) {
				p.triangles[v] = static_cast<uint32_t>(v);
			}

			for (size_t t = 0; t < p.vertexCount / 3; ++t) {
				const Vec3 a = Vec3At(p.positions, t * 3, 3);
				const Vec3 b = Vec3At(p.positions, t * 3 + 1, 3);
				const Vec3 d = Vec3At(p.positions, t * 3 + 2, 3);
				Vec3 n = MathUtil::Normalize(MathUtil::Cross(MathUtil::Sub(b, a), MathUtil::Sub(d, a)));

				if (MathUtil::Length(n) == 0.0) {
					n = {0.0, 1.0, 0.0};
				}

				for (size_t c = 0; c < 3; ++c) {
					MathUtil::ToFloats(n, &p.normals[(t * 3 + c) * 3]);
				}
			}
		}
		else {
			p.positions = src.positions;
			p.normals = src.normals;
			p.tangents = src.tangents;
			p.texCoords = src.texCoords;
			p.joints = src.joints;
			p.weights = src.weights;
		}

		p.colors.assign(p.vertexCount, 0xFFFFFFFFu);

		if (!colors->empty()) {

			for (size_t v = 0; v < p.vertexCount; ++v) {
				p.colors[v] = PackColor(&(*colors)[v * 4]);
			}
		}

		// Normály jednotkové; nulová normála se nahradí osou Y (aby shadery nedostaly NaN)
		for (size_t v = 0; v < p.vertexCount; ++v) {
			const Vec3 n = MathUtil::Normalize(Vec3At(p.normals, v, 3));
			const bool zero = MathUtil::Length(n) == 0.0;
			p.normals[v * 3] = static_cast<float>(zero ? 0.0 : n.x);
			p.normals[v * 3 + 1] = static_cast<float>(zero ? 1.0 : n.y);
			p.normals[v * 3 + 2] = static_cast<float>(zero ? 0.0 : n.z);
		}

		if (p.tangents.empty()) {
			// Tangenty z texturových souřadnic: T = dP/du; w určuje, na kterou stranu míří "nahoru" v textuře (V roste dolů)
			p.tangents.assign(p.vertexCount * 4, 0.0f);
			std::vector<Vec3> sumT(p.vertexCount), sumB(p.vertexCount);

			if (!p.texCoords.empty()) {

				for (size_t t = 0; t + 2 < p.triangles.size(); t += 3) {
					const uint32_t i0 = p.triangles[t], i1 = p.triangles[t + 1], i2 = p.triangles[t + 2];
					const Vec3 p0 = Vec3At(p.positions, i0, 3);
					const Vec3 e1 = MathUtil::Sub(Vec3At(p.positions, i1, 3), p0);
					const Vec3 e2 = MathUtil::Sub(Vec3At(p.positions, i2, 3), p0);
					const double du1 = p.texCoords[i1 * 2] - p.texCoords[i0 * 2], dv1 = p.texCoords[i1 * 2 + 1] - p.texCoords[i0 * 2 + 1];
					const double du2 = p.texCoords[i2 * 2] - p.texCoords[i0 * 2], dv2 = p.texCoords[i2 * 2 + 1] - p.texCoords[i0 * 2 + 1];
					const double det = du1 * dv2 - du2 * dv1;

					if (!(std::fabs(det) > 1e-20)) {
						continue;
					}

					const Vec3 tangent {(e1.x * dv2 - e2.x * dv1) / det, (e1.y * dv2 - e2.y * dv1) / det, (e1.z * dv2 - e2.z * dv1) / det};
					const Vec3 bitangent {(e2.x * du1 - e1.x * du2) / det, (e2.y * du1 - e1.y * du2) / det, (e2.z * du1 - e1.z * du2) / det};

					for (const uint32_t i : {i0, i1, i2}) {
						sumT[i] = {sumT[i].x + tangent.x, sumT[i].y + tangent.y, sumT[i].z + tangent.z};
						sumB[i] = {sumB[i].x + bitangent.x, sumB[i].y + bitangent.y, sumB[i].z + bitangent.z};
					}
				}
			}

			for (size_t v = 0; v < p.vertexCount; ++v) {
				const Vec3 n = Vec3At(p.normals, v, 3);
				Vec3 t = MathUtil::Sub(sumT[v], {n.x * MathUtil::Dot(n, sumT[v]), n.y * MathUtil::Dot(n, sumT[v]), n.z * MathUtil::Dot(n, sumT[v])});
				t = MathUtil::Normalize(t);

				if (MathUtil::Length(t) == 0.0) {
					// Bez použitelných souřadnic UV: libovolný vektor kolmý na normálu
					t = MathUtil::Normalize(std::fabs(n.x) < 0.9 ? MathUtil::Cross(n, {1.0, 0.0, 0.0}) : MathUtil::Cross(n, {0.0, 1.0, 0.0}));
				}

				const double handedness = MathUtil::Dot(MathUtil::Cross(n, t), {-sumB[v].x, -sumB[v].y, -sumB[v].z});
				p.tangents[v * 4] = static_cast<float>(t.x);
				p.tangents[v * 4 + 1] = static_cast<float>(t.y);
				p.tangents[v * 4 + 2] = static_cast<float>(t.z);
				p.tangents[v * 4 + 3] = handedness < 0.0 ? -1.0f : 1.0f;
			}
		}

		return p;
	}
}
