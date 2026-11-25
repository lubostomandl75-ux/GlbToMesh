/**
 * @file      MathUtil.h
 * @author    Lubomír Tomandl (info@web-data.cz)
 * @brief     Malá maticová matematika pro převod souřadnic a rozklad transformací uzlů
 * @version   1.0
 *
 * @copyright (c) Lubomír Tomandl - Angry Developer Studio
 *
 * Všechny matice jsou 4x4 uložené po sloupcích (index = sloupec * 4 + řádek), tedy ve stejném rozložení jako matice
 * v glTF. Počítá se s double, aby se při skládání hierarchie kostí nehromadily chyby.
 */

#pragma once

// Hlavičkové soubory C++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace MathUtil {

	/// Matice 4x4 po sloupcích (index = sloupec * 4 + řádek), stejně jako v glTF
	using Mat4 = std::array<double, 16>;

	/// Kvaternion rotace (x, y, z, w)
	struct Quat {
		double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
	};

	/// Trojrozměrný vektor nebo bod
	struct Vec3 {
		double x = 0.0, y = 0.0, z = 0.0;
	};

	/// Jednotková matice
	inline Mat4 Identity()
	{
		return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	}

	/// Všechny prvky matice jsou konečná čísla (ne NaN ani nekonečno)
	inline bool IsFinite(const Mat4 &m)
	{
		for (const double v : m) {

			if (!std::isfinite(v)) {
				return false;
			}
		}

		return true;
	}

	/// Zkopíruje matici (16 čísel double) do pole 16 floatů
	inline void ToFloats(const Mat4 &m, float *out)
	{
		for (size_t i = 0; i < 16; ++i) {
			out[i] = static_cast<float>(m[i]);
		}
	}

	/// Zkopíruje vektor (3 čísla double) do pole 3 floatů
	inline void ToFloats(const Vec3 &v, float *out)
	{
		out[0] = static_cast<float>(v.x);
		out[1] = static_cast<float>(v.y);
		out[2] = static_cast<float>(v.z);
	}

	/// Determinant levého horního bloku 3x3
	inline double Determinant3(const Mat4 &m)
	{
		return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
	}

	/// Skalární součin kvaternionů (kosinus úhlu mezi jednotkovými kvaterniony)
	inline double Dot(const Quat &a, const Quat &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	}

	/// Jednotkový kvaternion z posloupnosti x, y, z, w; nulový kvaternion nahradí jednotkovým
	inline Quat NormalizeQuat(Quat q)
	{
		const double len = std::sqrt(Dot(q, q));

		if (!(len > 0.0) || !std::isfinite(len)) {
			return Quat {};
		}

		q.x /= len;
		q.y /= len;
		q.z /= len;
		q.w /= len;

		return q;
	}

	/// Součin a * b (sloupcové vektory: nejdřív se použije b, potom a)
	inline Mat4 Multiply(const Mat4 &a, const Mat4 &b)
	{
		Mat4 r {};

		for (size_t col = 0; col < 4; ++col) {

			for (size_t row = 0; row < 4; ++row) {
				double sum = 0.0;

				for (size_t k = 0; k < 4; ++k) {
					sum += a[k * 4 + row] * b[col * 4 + k];
				}

				r[col * 4 + row] = sum;
			}
		}

		return r;
	}

	/**
	 * Obecná inverze matice 4x4 pomocí kofaktorů.
	 *
	 * @param const Mat4& m Vstupní matice
	 * @param Mat4& out Inverzní matice (platná jen při návratové hodnotě true)
	 *
	 * @return bool false = matice je singulární
	 */
	inline bool Inverse(const Mat4 &m, Mat4 &out)
	{
		const double *a = m.data();
		Mat4 o {};

		o[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
		o[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
		o[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
		o[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
		o[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
		o[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
		o[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
		o[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
		o[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
		o[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
		o[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
		o[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
		o[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
		o[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		o[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		o[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

		const double det = a[0] * o[0] + a[1] * o[4] + a[2] * o[8] + a[3] * o[12];

		if (std::fabs(det) < 1e-30) {
			return false;
		}

		const double invDet = 1.0 / det;

		for (int i = 0; i < 16; ++i) {
			out[static_cast<size_t>(i)] = o[static_cast<size_t>(i)] * invDet;
		}

		return true;
	}

	/**
	 * Převod matice z levotočivé do pravotočivé soustavy (a naopak) zrcadlením osy Z: S * M * S, kde S = diag(1, 1, -1).
	 * Změní znaménko prvků, ve kterých se osa Z objeví právě jednou (rotační část a posun v Z).
	 *
	 * @param const Mat4& m Matice ve zdrojové soustavě
	 *
	 * @return Mat4 Matice v cílové soustavě
	 */
	inline Mat4 MirrorZ(Mat4 m)
	{
		m[2] = -m[2];
		m[6] = -m[6];
		m[8] = -m[8];
		m[9] = -m[9];
		m[14] = -m[14];

		return m;
	}

	/// Bod nebo vektor zrcadlený podél osy Z (z -> -z)
	inline Vec3 MirrorZ(Vec3 v)
	{
		v.z = -v.z;

		return v;
	}

	/// Kvaternion z rotační matice 3x3 (levý horní roh matice po sloupcích, sloupce musí být ortonormální)
	inline Quat QuaternionFromRotation(const double r00, const double r01, const double r02, const double r10,
									   const double r11, const double r12, const double r20, const double r21,
									   const double r22)
	{
		Quat q;
		const double trace = r00 + r11 + r22;

		if (trace > 0.0) {
			const double s = 0.5 / std::sqrt(trace + 1.0);
			q.w = 0.25 / s;
			q.x = (r21 - r12) * s;
			q.y = (r02 - r20) * s;
			q.z = (r10 - r01) * s;
		}
		else if (r00 > r11 && r00 > r22) {
			const double s = 2.0 * std::sqrt(1.0 + r00 - r11 - r22);
			q.w = (r21 - r12) / s;
			q.x = 0.25 * s;
			q.y = (r01 + r10) / s;
			q.z = (r02 + r20) / s;
		}
		else if (r11 > r22) {
			const double s = 2.0 * std::sqrt(1.0 + r11 - r00 - r22);
			q.w = (r02 - r20) / s;
			q.x = (r01 + r10) / s;
			q.y = 0.25 * s;
			q.z = (r12 + r21) / s;
		}
		else {
			const double s = 2.0 * std::sqrt(1.0 + r22 - r00 - r11);
			q.w = (r10 - r01) / s;
			q.x = (r02 + r20) / s;
			q.y = (r12 + r21) / s;
			q.z = 0.25 * s;
		}

		return NormalizeQuat(q);
	}

	/// Matice 4x4 složená z posunu, rotace a měřítka (M = T * R * S)
	inline Mat4 Compose(const Vec3 &t, const Quat &q, const Vec3 &s)
	{
		const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
		const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

		Mat4 m = Identity();
		m[0] = (1.0 - 2.0 * (yy + zz)) * s.x;
		m[1] = (2.0 * (xy + wz)) * s.x;
		m[2] = (2.0 * (xz - wy)) * s.x;
		m[4] = (2.0 * (xy - wz)) * s.y;
		m[5] = (1.0 - 2.0 * (xx + zz)) * s.y;
		m[6] = (2.0 * (yz + wx)) * s.y;
		m[8] = (2.0 * (xz + wy)) * s.z;
		m[9] = (2.0 * (yz - wx)) * s.z;
		m[10] = (1.0 - 2.0 * (xx + yy)) * s.z;
		m[12] = t.x;
		m[13] = t.y;
		m[14] = t.z;

		return m;
	}

	/**
	 * Rozloží matici na posun, rotaci a měřítko. Záporný determinant se vyjádří záporným měřítkem v ose X.
	 *
	 * @param const Mat4& m Rozkládaná matice
	 * @param Vec3& t Posun
	 * @param Quat& q Rotace
	 * @param Vec3& s Měřítko
	 *
	 * @return bool true = rozklad matici přesně popisuje; false = matice obsahuje zkos nebo není afinní, TRS je jen
	 *              přiblížení
	 */
	inline bool Decompose(const Mat4 &m, Vec3 &t, Quat &q, Vec3 &s)
	{
		t = {m[12], m[13], m[14]};

		s.x = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
		s.y = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
		s.z = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);

		if (Determinant3(m) < 0.0) {
			s.x = -s.x;
		}

		if (s.x == 0.0 || s.y == 0.0 || s.z == 0.0) {
			q = Quat {};

			return false;
		}

		// Rotační matice R(řádek, sloupec) = m[sloupec * 4 + řádek] / měřítko sloupce
		q = QuaternionFromRotation(
			m[0] / s.x, m[4] / s.y, m[8] / s.z, //
			m[1] / s.x, m[5] / s.y, m[9] / s.z, //
			m[2] / s.x, m[6] / s.y, m[10] / s.z
		);

		// Kontrola: složení musí dát původní matici (jinak je v matici zkos nebo projekce)
		const Mat4 back = Compose(t, q, s);
		double maxScale = 1.0;

		for (const double v : m) {
			maxScale = std::max(maxScale, std::fabs(v));
		}

		for (size_t i = 0; i < 16; ++i) {

			if (std::fabs(back[i] - m[i]) > 1e-4 * maxScale) {
				return false;
			}
		}

		return true;
	}

	/// Skalární součin vektorů
	inline double Dot(const Vec3 &a, const Vec3 &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	/// Vektorový součin
	inline Vec3 Cross(const Vec3 &a, const Vec3 &b)
	{
		return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	}

	/// Rozdíl vektorů a - b
	inline Vec3 Sub(const Vec3 &a, const Vec3 &b)
	{
		return {a.x - b.x, a.y - b.y, a.z - b.z};
	}

	/// Délka vektoru
	inline double Length(const Vec3 &a)
	{
		return std::sqrt(Dot(a, a));
	}

	/// Jednotkový vektor; nulový nebo nekonečný vektor vrátí jako nulový
	inline Vec3 Normalize(const Vec3 &a)
	{
		const double len = Length(a);

		if (!(len > 0.0) || !std::isfinite(len)) {
			return {0.0, 0.0, 0.0};
		}

		return {a.x / len, a.y / len, a.z / len};
	}

	/// Bod transformovaný maticí (w = 1)
	inline Vec3 TransformPoint(const Mat4 &m, const Vec3 &p)
	{
		return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
	}

	/// Vektor transformovaný levým horním blokem 3x3 (bez posunu)
	inline Vec3 TransformVector(const Mat4 &m, const Vec3 &v)
	{
		return {m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z, m[2] * v.x + m[6] * v.y + m[10] * v.z};
	}

	/**
	 * Matice pro transformaci normál: inverzní transponovaná matice 3x3 (v levém horním bloku, zbytek jednotkový). Pro matici
	 * s degenerovaným blokem 3x3 vrací jednotkovou matici.
	 */
	inline Mat4 NormalMatrix(const Mat4 &m)
	{
		const double det = Determinant3(m);

		if (!(std::fabs(det) > 1e-30) || !std::isfinite(det)) {
			return Identity();
		}

		// Inverzní transponovaná matice = matice kofaktorů dělená determinantem; prvek (řádek, sloupec) leží na indexu sloupec * 4 + řádek
		Mat4 r = Identity();
		const double inv = 1.0 / det;

		r[0] = (m[5] * m[10] - m[9] * m[6]) * inv;	 // kofaktor (0, 0)
		r[1] = (m[8] * m[6] - m[4] * m[10]) * inv;	 // kofaktor (1, 0)
		r[2] = (m[4] * m[9] - m[8] * m[5]) * inv;	 // kofaktor (2, 0)
		r[4] = (m[9] * m[2] - m[1] * m[10]) * inv;	 // kofaktor (0, 1)
		r[5] = (m[0] * m[10] - m[8] * m[2]) * inv;	 // kofaktor (1, 1)
		r[6] = (m[8] * m[1] - m[0] * m[9]) * inv;	 // kofaktor (2, 1)
		r[8] = (m[1] * m[6] - m[5] * m[2]) * inv;	 // kofaktor (0, 2)
		r[9] = (m[4] * m[2] - m[0] * m[6]) * inv;	 // kofaktor (1, 2)
		r[10] = (m[0] * m[5] - m[4] * m[1]) * inv; // kofaktor (2, 2)

		return r;
	}

	/// Sférická interpolace kvaternionů (kratší cestou)
	inline Quat Slerp(const Quat &a, Quat b, double t)
	{
		double cosine = Dot(a, b);

		if (cosine < 0.0) {
			cosine = -cosine;
			b = {-b.x, -b.y, -b.z, -b.w};
		}

		double wa, wb;

		if (cosine > 0.9995) {
			wa = 1.0 - t;
			wb = t;
		}
		else {
			const double angle = std::acos(cosine);
			const double sine = std::sin(angle);
			wa = std::sin((1.0 - t) * angle) / sine;
			wb = std::sin(t * angle) / sine;
		}

		return NormalizeQuat({wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w});
	}
}
