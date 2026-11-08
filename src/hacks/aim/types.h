#pragma once
// Value types shared by everything in the aim tree: vectors, matrices, colours, angles.

#include <algorithm>
#include <vector>
#include <deque>
#include <string>
#include <format>

#define PI 3.14159265358979323846
#define M_RADPI 57.295779513082
#define DEG2RAD(x) ((float)(x) * (float)((float)(PI) / 180.f))
#define RAD2DEG(x) ((float)(x) * (float)(180.f / (float)(PI)))

#pragma warning (disable : 26495)

class Vec2
{
public:
	float x = 0.f, y = 0.f;

public:
	inline void Zero()
	{
		x = y = 0.f;
	}

	inline Vec2(float X = 0.f, float Y = 0.f)
	{
		x = X; y = Y;
	}

	inline Vec2(float* v)
	{
		x = v[0]; y = v[1];
	}

	inline Vec2(const float* v)
	{
		x = v[0]; y = v[1];
	}

	inline Vec2(const Vec2& v)
	{
		x = v.x; y = v.y;
	}

	inline Vec2& operator=(const Vec2& v)
	{
		x = v.x; y = v.y; return *this;
	}

	inline float& operator[](int i)
	{
		return ((float*)this)[i];
	}

	inline float operator[](int i) const
	{
		return ((float*)this)[i];
	}

	inline bool operator==(const Vec2& v) const
	{
		return x == v.x && y == v.y;
	}

	inline bool operator!=(const Vec2& v) const
	{
		return x != v.x || y != v.y;
	}

	explicit operator bool() const
	{
		return x || y;
	}

	inline Vec2& operator+=(const Vec2& v)
	{
		x += v.x; y += v.y; return *this;
	}

	inline Vec2& operator-=(const Vec2& v)
	{
		x -= v.x; y -= v.y; return *this;
	}

	inline Vec2& operator*=(const Vec2& v)
	{
		x *= v.x; y *= v.y; return *this;
	}

	inline Vec2& operator/=(const Vec2& v)
	{
		x /= v.x; y /= v.y; return *this;
	}

	inline Vec2& operator+=(float v)
	{
		x += v; y += v; return *this;
	}

	inline Vec2& operator-=(float v)
	{
		x -= v; y -= v; return *this;
	}

	inline Vec2& operator*=(float v)
	{
		x *= v; y *= v; return *this;
	}

	inline Vec2& operator/=(float v)
	{
		x /= v; y /= v; return *this;
	}

	inline Vec2 operator+(const Vec2& v) const
	{
		return Vec2(x + v.x, y + v.y);
	}

	inline Vec2 operator-(const Vec2& v) const
	{
		return Vec2(x - v.x, y - v.y);
	}

	inline Vec2 operator*(const Vec2& v) const
	{
		return Vec2(x * v.x, y * v.y);
	}

	inline Vec2 operator/(const Vec2& v) const
	{
		return Vec2(x / v.x, y / v.y);
	}

	inline Vec2 operator+(float v) const
	{
		return Vec2(x + v, y + v);
	}

	inline Vec2 operator-(float v) const
	{
		return Vec2(x - v, y - v);
	}

	inline Vec2 operator*(float v) const
	{
		return Vec2(x * v, y * v);
	}

	inline Vec2 operator/(float v) const
	{
		return Vec2(x / v, y / v);
	}

	inline void Set(float X = 0.f, float Y = 0.f)
	{
		x = X; y = Y;
	}

	inline float Min() const
	{
		return std::min<float>(x, y);
	}

	inline float Max() const
	{
		return std::max<float>(x, y);
	}

	inline Vec2 Min(const Vec2& v) const
	{
		return Vec2(std::min<float>(x, v.x), std::min<float>(y, v.y));
	}

	inline Vec2 Max(const Vec2& v) const
	{
		return Vec2(std::max<float>(x, v.x), std::max<float>(y, v.y));
	}

	inline Vec2 Clamp(const Vec2& v1, const Vec2& v2) const
	{
		return Max(v1).Min(v2);
	}

	inline Vec2 Min(float v) const
	{
		return Vec2(std::min<float>(x, v), std::min<float>(y, v));
	}

	inline Vec2 Max(float v) const
	{
		return Vec2(std::max<float>(x, v), std::max<float>(y, v));
	}

	inline Vec2 Clamp(float v1, float v2) const
	{
		return Max(v1).Min(v2);
	}

	inline Vec2 Lerp(const Vec2& v, float t) const
	{
		return { x + (v.x - x) * t, y + (v.y - y) * t };
	}

	inline Vec2 Lerp(float v, float t) const
	{
		return { x + (v - x) * t, y + (v - y) * t };
	}

	inline Vec2 DeltaAngle(const Vec2& v) const
	{
		auto deltaAngle = [](const float flAngleA, const float flAngleB)
			{
				float flOut = fmodf((flAngleA - flAngleB) + 180.f, 360.f);
				return flOut += flOut < 0 ? 180.f : -180.f;
			};

		return { deltaAngle(x, v.x), deltaAngle(y, v.y) };
	}

	inline Vec2 DeltaAngle(float v) const
	{
		auto deltaAngle = [](const float flAngleA, const float flAngleB)
			{
				float flOut = fmodf((flAngleA - flAngleB) + 180.f, 360.f);
				return flOut += flOut < 0 ? 180.f : -180.f;
			};

		return { deltaAngle(x, v), deltaAngle(y, v) };
	}

	inline Vec2 LerpAngle(const Vec2& v, float t) const
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
			{
				const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
				return fmodf(2 * flDelta, 360.f) - flDelta;
			};
		return { x - shortDist(x, v.x) * t, y - shortDist(y, v.y) * t };
	}

	inline Vec2 LerpAngle(float v, float t) const
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
			{
				const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
				return fmodf(2 * flDelta, 360.f) - flDelta;
			};
		return { x - shortDist(x, v) * t, y - shortDist(y, v) * t };
	}

	inline float Length(void) const
	{
		return sqrtf(x * x + y * y);
	}

	inline float LengthSqr(void) const
	{
		return (x * x + y * y);
	}

	inline float DistTo(const Vec2& v) const
	{
		return (*this - v).Length();
	}

	inline float DistToSqr(const Vec2& v) const
	{
		return (*this - v).LengthSqr();
	}

	inline float Dot(const Vec2& v) const
	{
		return x * v.x + y * v.y;
	}

	inline bool IsZero(float flEpsilon = 0.001f) const
	{
		return fabsf(x) < flEpsilon &&
			   fabsf(y) < flEpsilon;
	}
};
using Vector2D = Vec2;

class Vec3
{
public:
	float x = 0.f, y = 0.f, z = 0.f;

public:
	inline void Zero()
	{
		x = y = z = 0.f;
	}

	inline Vec3(float X = 0.f, float Y = 0.f, float Z = 0.f)
	{
		x = X; y = Y; z = Z;
	}

	inline Vec3(float* v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	inline Vec3(const float* v)
	{
		x = v[0]; y = v[1]; z = v[2];
	}

	inline Vec3(const Vec3& v)
	{
		x = v.x; y = v.y; z = v.z;
	}

	inline Vec3(const Vec2& v)
	{
		x = v.x; y = v.y; z = 0.f;
	}

	inline Vec3& operator=(const Vec3& v)
	{
		x = v.x; y = v.y; z = v.z; return *this;
	}

	inline float& operator[](int i)
	{
		return ((float*)this)[i];
	}

	inline float operator[](int i) const
	{
		return ((float*)this)[i];
	}

	inline bool operator==(const Vec3& v) const
	{
		return x == v.x && y == v.y && z == v.z;
	}

	inline bool operator!=(const Vec3& v) const
	{
		return x != v.x || y != v.y || z != v.z;
	}

	explicit operator bool() const
	{
		return x || y || z;
	}

	inline Vec3& operator+=(const Vec3& v)
	{
		x += v.x; y += v.y; z += v.z; return *this;
	}

	inline Vec3& operator-=(const Vec3& v)
	{
		x -= v.x; y -= v.y; z -= v.z; return *this;
	}

	inline Vec3& operator*=(const Vec3& v)
	{
		x *= v.x; y *= v.y; z *= v.z; return *this;
	}

	inline Vec3& operator/=(const Vec3& v)
	{
		x /= v.x; y /= v.y; z /= v.z; return *this;
	}

	inline Vec3& operator+=(float v)
	{
		x += v; y += v; z += v; return *this;
	}

	inline Vec3& operator-=(float v)
	{
		x -= v; y -= v; z -= v; return *this;
	}

	inline Vec3& operator*=(float v)
	{
		x *= v; y *= v; z *= v; return *this;
	}

	inline Vec3& operator/=(float v)
	{
		x /= v; y /= v; z /= v; return *this;
	}

	inline Vec3 operator+(const Vec3& v) const
	{
		return Vec3(x + v.x, y + v.y, z + v.z);
	}

	inline Vec3 operator-(const Vec3& v) const
	{
		return Vec3(x - v.x, y - v.y, z - v.z);
	}

	inline Vec3 operator*(const Vec3& v) const
	{
		return Vec3(x * v.x, y * v.y, z * v.z);
	}

	inline Vec3 operator/(const Vec3& v) const
	{
		return Vec3(x / v.x, y / v.y, z / v.z);
	}

	inline Vec3 operator+(float v) const
	{
		return Vec3(x + v, y + v, z + v);
	}

	inline Vec3 operator-(float v) const
	{
		return Vec3(x - v, y - v, z - v);
	}

	inline Vec3 operator*(float v) const
	{
		return Vec3(x * v, y * v, z * v);
	}

	inline Vec3 operator/(float v) const
	{
		return Vec3(x / v, y / v, z / v);
	}

	inline void Set(float X = 0.f, float Y = 0.f, float Z = 0.f)
	{
		x = X; y = Y; z = Z;
	}

	inline Vec3 To2D() const
	{
		return { x, y };
	}

	inline Vec3 Min(const Vec3& v) const
	{
		return Vec3(std::min<float>(x, v.x), std::min<float>(y, v.y), std::min<float>(z, v.z));
	}

	inline Vec3 Max(const Vec3& v) const
	{
		return Vec3(std::max<float>(x, v.x), std::max<float>(y, v.y), std::max<float>(z, v.z));
	}

	inline Vec3 Clamp(const Vec3& v1, const Vec3& v2) const
	{
		return Max(v1).Min(v2);
	}

	inline float Min() const
	{
		return std::min<float>(x, std::min<float>(y, z));
	}

	inline float Max() const
	{
		return std::max<float>(x, std::max<float>(y, z));
	}

	inline Vec3 Min(float v) const
	{
		return Vec3(std::min<float>(x, v), std::min<float>(y, v), std::min<float>(z, v));
	}

	inline Vec3 Max(float v) const
	{
		return Vec3(std::max<float>(x, v), std::max<float>(y, v), std::max<float>(z, v));
	}

	inline Vec3 Clamp(float v1, float v2) const
	{
		return Max(v1).Min(v2);
	}

	inline Vec3 Lerp(const Vec3& v, float t) const
	{
		return { x + (v.x - x) * t, y + (v.y - y) * t, z + (v.z - z) * t };
	}

	inline Vec3 Lerp(float v, float t) const
	{
		return { x + (v - x) * t, y + (v - y) * t, z + (v - z) * t };
	}

	inline Vec3 DeltaAngle(const Vec3& v) const
	{
		auto deltaAngle = [](const float flAngleA, const float flAngleB)
			{
				float flOut = fmodf((flAngleA - flAngleB) + 180.f, 360.f);
				return flOut += flOut < 0 ? 180.f : -180.f;
			};

		return { deltaAngle(x, v.x), deltaAngle(y, v.y), deltaAngle(z, v.z) };
	}

	inline Vec3 DeltaAngle(float v) const
	{
		auto deltaAngle = [](const float flAngleA, const float flAngleB)
			{
				float flOut = fmodf((flAngleA - flAngleB) + 180.f, 360.f);
				return flOut += flOut < 0 ? 180.f : -180.f;
			};

		return { deltaAngle(x, v), deltaAngle(y, v), deltaAngle(z, v) };
	}

	inline Vec3 LerpAngle(const Vec3& v, float t) const
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
			{
				const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
				return fmodf(2 * flDelta, 360.f) - flDelta;
			};
		return { x - shortDist(x, v.x) * t, y - shortDist(y, v.y) * t, z - shortDist(z, v.z) * t };
	}

	inline Vec3 LerpAngle(float v, float t) const
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
			{
				const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
				return fmodf(2 * flDelta, 360.f) - flDelta;
			};
		return { x - shortDist(x, v) * t, y - shortDist(y, v) * t, z - shortDist(z, v) * t };
	}

	inline float Length(void) const
	{
		return sqrtf(x * x + y * y + z * z);
	}

	inline float LengthSqr(void) const
	{
		return (x * x + y * y + z * z);
	}

	inline float Normalize()
	{
		float flLength = Length();
		float flLengthNormal = 1.f / (FLT_EPSILON + flLength);

		x *= flLengthNormal;
		y *= flLengthNormal;
		z *= flLengthNormal;

		return flLength;
	}

	inline float Normalize2D()
	{
		float flLength = Length2D();
		float flLengthNormal = 1.f / (FLT_EPSILON + flLength);

		x *= flLengthNormal;
		y *= flLengthNormal;
		z = 0;

		return flLength;
	}

	inline Vec3 Normalized()
	{
		float flLengthNormal = 1.f / (FLT_EPSILON + Length());
		return Vec3(x * flLengthNormal, y * flLengthNormal, z * flLengthNormal);
	}

	inline Vec3 Normalized2D()
	{
		float flLengthNormal = 1.f / (FLT_EPSILON + Length2D());
		return Vec3(x * flLengthNormal, y * flLengthNormal);
	}

	inline float Length2D(void) const
	{
		return sqrtf(x * x + y * y);
	}

	inline float Length2DSqr(void) const
	{
		return (x * x + y * y);
	}

	inline float DistTo(const Vec3& v) const
	{
		return (*this - v).Length();
	}

	inline float DistToSqr(const Vec3& v) const
	{
		return (*this - v).LengthSqr();
	}

	inline float Dot(const Vec3& v) const
	{
		return x * v.x + y * v.y + z * v.z;
	}

	inline bool IsZero(float flEpsilon = 0.001f) const
	{
		return fabsf(x) < flEpsilon &&
			   fabsf(y) < flEpsilon &&
			   fabsf(z) < flEpsilon;
	}

};
using Vector = Vec3;
using QAngle = Vec3;

class Vector4D
{
public:
	float x, y, z, w;
};
using Quaternion = Vector4D;

using matrix3x4 = float[3][4];
class VMatrix
{
public:
	float m[4][4];

	inline const float* operator[](int i) const
	{
		return m[i];
	}

private:
	static inline void SinCos(float flRadians, float* pSin, float* pCos)
	{
		*pSin = std::sin(flRadians);
		*pCos = std::cos(flRadians);
	}

	static inline void Vector3DMultiplyPosition(const VMatrix& src1, const Vector src2, Vector& dst)
	{
		dst[0] = src1[0][0] * src2.x + src1[0][1] * src2.y + src1[0][2] * src2.z + src1[0][3];
		dst[1] = src1[1][0] * src2.x + src1[1][1] * src2.y + src1[1][2] * src2.z + src1[1][3];
		dst[2] = src1[2][0] * src2.x + src1[2][1] * src2.y + src1[2][2] * src2.z + src1[2][3];
	}

public:
	inline const matrix3x4& As3x4() const
	{
		return *((const matrix3x4*)this);
	}

	inline Vector VMul4x3(const Vector& vVec) const
	{
		Vector vResult;
		Vector3DMultiplyPosition(*this, vVec, vResult);
		return vResult;
	}

	inline Vector VMul4x3Transpose(const Vector& vVec) const
	{
		Vector tmp = vVec;
		tmp.x -= m[0][3];
		tmp.y -= m[1][3];
		tmp.z -= m[2][3];

		return Vector(
			m[0][0] * tmp.x + m[1][0] * tmp.y + m[2][0] * tmp.z,
			m[0][1] * tmp.x + m[1][1] * tmp.y + m[2][1] * tmp.z,
			m[0][2] * tmp.x + m[1][2] * tmp.y + m[2][2] * tmp.z
		);
	}

	inline Vector LocalToWorld(const Vector& vVec) const
	{
		return VMul4x3(vVec);
	}

	inline Vector WorldToLocal(const Vector& vVec) const
	{
		return VMul4x3Transpose(vVec);
	}

};

struct FloatRange_t
{
	float Min = 0, Max = 0;

	inline bool operator==(const FloatRange_t& t) const
	{
		return Min == t.Min && Max == t.Max;
	}

	inline bool operator!=(const FloatRange_t& t) const
	{
		return Min != t.Min || Max != t.Max;
	}
};

namespace LerpEnum {
	enum LerpEnum {
		All, NoAlpha, Alpha
	};
};
using byte = unsigned char;
struct Color_t
{
	byte r = 255, g = 255, b = 255, a = 255;

	inline bool operator==(Color_t other) const
	{
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}

	inline bool operator!=(Color_t other) const
	{
		return r != other.r || g != other.g || b != other.b || a != other.a;
	}

	inline std::string ToHex() const
	{
		return std::format("\x7{:02x}{:02x}{:02x}", r, g, b);
	}

	inline std::string ToHexA() const
	{
		return std::format("\x8{:02x}{:02x}{:02x}{:02x}", r, g, b, a);
	}

	inline Color_t Lerp(Color_t to, float t, LerpEnum::LerpEnum eLerp = LerpEnum::All) const
	{
		//a + (b - a) * t
		switch (eLerp)
		{
		default:
			return {
				byte(r + (to.r - r) * t),
				byte(g + (to.g - g) * t),
				byte(b + (to.b - b) * t),
				byte(a + (to.a - a) * t)
			};
		case LerpEnum::NoAlpha:
			return {
				byte(r + (to.r - r) * t),
				byte(g + (to.g - g) * t),
				byte(b + (to.b - b) * t),
				byte(a)
			};
		case LerpEnum::Alpha:
			return {
				r,
				g,
				b,
				byte(a + (to.a - a) * t)
			};
		}
	}

	inline Color_t Alpha(byte to) const
	{
		return { r, g, b, to };
	}

	inline float Brightness(float flRed = 0.299f, float flGreen = 0.587f, float flBlue = 0.114f) const
	{
		return r * flRed + g * flGreen + b * flBlue;
	}

	inline bool IsColorBright(float flValue = 175, float flRed = 0.299f, float flGreen = 0.587f, float flBlue = 0.114f) const
	{
		return Brightness(flRed, flGreen, flBlue) > flValue;
	}

	inline bool IsColorDark(float flValue = 80, float flRed = 0.299f, float flGreen = 0.587f, float flBlue = 0.114f)const
	{
		return Brightness(flRed, flGreen, flBlue) < flValue;
	}
};

struct DragBox_t
{
	int x = 150;
	int y = 100;

	inline bool operator==(const DragBox_t& t) const
	{
		return x == t.x && y == t.y;
	}

	inline bool operator!=(const DragBox_t& t) const
	{
		return x != t.x || y != t.y;
	}
};
