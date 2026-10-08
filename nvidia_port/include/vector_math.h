#pragma once
#ifndef __clang__
#error "This isolated port requires Clang (including Clang CUDA), not nvcc."
#endif
#include <cmath>
#include <cstdint>
#if defined(__CUDA__)
#define PT_FN __attribute__((host, device)) inline
#define PT_NOINLINE __attribute__((host, device, noinline)) inline
#else
#define PT_FN inline
#define PT_NOINLINE __attribute__((noinline)) inline
#endif
namespace pt {
using uint = unsigned;
using uchar = unsigned char;
typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float3 __attribute__((ext_vector_type(3)));
typedef float float4 __attribute__((ext_vector_type(4)));
typedef unsigned uint2 __attribute__((ext_vector_type(2)));
typedef unsigned uint3 __attribute__((ext_vector_type(3)));
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
typedef int int2 __attribute__((ext_vector_type(2)));
typedef int int3 __attribute__((ext_vector_type(3)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef unsigned char uchar4 __attribute__((ext_vector_type(4)));
using bool2 = int2;
using bool3 = int3;
using bool4 = int4;
PT_FN constexpr float2 f2(float a) { return (float2)(a); }
PT_FN constexpr float2 f2(float a0, float a1) { return (float2){a0, a1}; }
PT_FN constexpr float3 f3(float a) { return (float3)(a); }
PT_FN constexpr float3 f3(float a0, float a1, float a2) { return (float3){a0, a1, a2}; }
PT_FN constexpr float4 f4(float a) { return (float4)(a); }
PT_FN constexpr float4 f4(float a0, float a1, float a2, float a3) { return (float4){a0, a1, a2, a3}; }
PT_FN constexpr uint2 u2(unsigned a) { return (uint2)(a); }
PT_FN constexpr uint2 u2(unsigned a0, unsigned a1) { return (uint2){a0, a1}; }
PT_FN constexpr uint4 u4(unsigned a) { return (uint4)(a); }
PT_FN constexpr uint4 u4(unsigned a0, unsigned a1, unsigned a2, unsigned a3) { return (uint4){a0, a1, a2, a3}; }
PT_FN constexpr int2 i2(int a) { return (int2)(a); }
PT_FN constexpr int2 i2(int a0, int a1) { return (int2){a0, a1}; }
PT_FN constexpr uchar4 c4(unsigned char a) { return (uchar4)(a); }
PT_FN constexpr uchar4 c4(unsigned char a0, unsigned char a1, unsigned char a2, unsigned char a3) { return (uchar4){a0, a1, a2, a3}; }
PT_FN constexpr float4 f4(float2 a,float b,float c) { return (float4){a.x,a.y,b,c}; }
PT_FN constexpr float4 f4(float3 a,float b) { return (float4){a.x,a.y,a.z,b}; }
PT_FN constexpr float3 f3(float2 a,float b) { return (float3){a.x,a.y,b}; }
PT_FN constexpr bool3 b3(bool a) { return (int3)(a ? -1 : 0); }
PT_FN float2 f2(uint2 a) { return __builtin_convertvector(a,float2); }
PT_FN float2 f2(int2 a) { return __builtin_convertvector(a,float2); }
PT_FN int2 i2(uint2 a) { return __builtin_convertvector(a,int2); }
PT_FN int2 i2(float2 a) { return __builtin_convertvector(a,int2); }
template<class To,class From> PT_FN To as_type(From value) { static_assert(sizeof(To)==sizeof(From)); return __builtin_bit_cast(To,value); }
template<class T> PT_FN T min(T a,T b) { return a < b ? a : b; }
template<class T> PT_FN T max(T a,T b) { return a > b ? a : b; }
PT_FN float min(float a,float b) { return ::fminf(a,b); }
PT_FN float max(float a,float b) { return ::fmaxf(a,b); }
PT_FN float abs(float a) { return ::fabsf(a); }
PT_FN float sqrt(float a) { return ::sqrtf(a); }
PT_FN float pow(float a,float b) { return ::powf(a,b); }
PT_FN float sin(float a) { return ::sinf(a); }
PT_FN float cos(float a) { return ::cosf(a); }
PT_FN float atan(float a) { return ::atanf(a); }
PT_FN float floor(float a) { return ::floorf(a); }
PT_FN float ceil(float a) { return ::ceilf(a); }
PT_FN float exp(float a) { return ::expf(a); }
PT_FN float exp2(float a) { return ::exp2f(a); }
template<class T> PT_FN T clamp(T a,T lo,T hi) { return min(max(a,lo),hi); }
template<class T> PT_FN T mix(T a,T b,float t) { return a*(1-t)+b*t; }
PT_FN float smoothstep(float lo,float hi,float a) { float t=clamp((a-lo)/(hi-lo),0.f,1.f); return t*t*(3-2*t); }
PT_FN float2 min(float2 a,float2 b) { float2 out; for(int i=0;i<2;++i) out[i]=min(a[i],b[i]); return out; }
PT_FN float2 min(float2 a,float b) { return min(a,(float2)(b)); }
PT_FN float2 min(float a,float2 b) { return min((float2)(a),b); }
PT_FN float2 max(float2 a,float2 b) { float2 out; for(int i=0;i<2;++i) out[i]=max(a[i],b[i]); return out; }
PT_FN float2 max(float2 a,float b) { return max(a,(float2)(b)); }
PT_FN float2 max(float a,float2 b) { return max((float2)(a),b); }
PT_FN float2 clamp(float2 a,float lo,float hi) { return min(max(a,lo),hi); }
PT_FN float2 abs(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=abs(a[i]); return out; }
PT_FN float2 floor(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=floor(a[i]); return out; }
PT_FN float2 ceil(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=ceil(a[i]); return out; }
PT_FN float2 exp(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=exp(a[i]); return out; }
PT_FN float2 exp2(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=exp2(a[i]); return out; }
PT_FN float2 sqrt(float2 a) { float2 out; for(int i=0;i<2;++i) out[i]=sqrt(a[i]); return out; }
PT_FN float2 pow(float2 a,float2 b) { float2 out; for(int i=0;i<2;++i) out[i]=pow(a[i],b[i]); return out; }
PT_FN float2 pow(float2 a,float b) { return pow(a,(float2)(b)); }
PT_FN float dot(float2 a,float2 b) { float out=0; for(int i=0;i<2;++i) out+=a[i]*b[i]; return out; }
PT_FN float length(float2 a) { return sqrt(dot(a,a)); }
PT_FN float2 normalize(float2 a) { return a/length(a); }
PT_FN float2 fract(float2 a) { return a-floor(a); }
PT_FN float2 saturate(float2 a) { return clamp(a,0.f,1.f); }
PT_FN int2 isfinite(float2 a) { int2 out; for(int i=0;i<2;++i) out[i]=__builtin_isfinite(a[i]) ? -1 : 0; return out; }
PT_FN bool all(int2 a) { bool out=true; for(int i=0;i<2;++i) out=out && (a[i]!=0); return out; }
PT_FN bool any(int2 a) { bool out=false; for(int i=0;i<2;++i) out=out || (a[i]!=0); return out; }
PT_FN float2 select(float2 a,float2 b,int2 c) { float2 out; for(int i=0;i<2;++i) out[i]=c[i] ? b[i] : a[i]; return out; }
PT_FN float3 min(float3 a,float3 b) { float3 out; for(int i=0;i<3;++i) out[i]=min(a[i],b[i]); return out; }
PT_FN float3 min(float3 a,float b) { return min(a,(float3)(b)); }
PT_FN float3 min(float a,float3 b) { return min((float3)(a),b); }
PT_FN float3 max(float3 a,float3 b) { float3 out; for(int i=0;i<3;++i) out[i]=max(a[i],b[i]); return out; }
PT_FN float3 max(float3 a,float b) { return max(a,(float3)(b)); }
PT_FN float3 max(float a,float3 b) { return max((float3)(a),b); }
PT_FN float3 clamp(float3 a,float lo,float hi) { return min(max(a,lo),hi); }
PT_FN float3 abs(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=abs(a[i]); return out; }
PT_FN float3 floor(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=floor(a[i]); return out; }
PT_FN float3 ceil(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=ceil(a[i]); return out; }
PT_FN float3 exp(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=exp(a[i]); return out; }
PT_FN float3 exp2(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=exp2(a[i]); return out; }
PT_FN float3 sqrt(float3 a) { float3 out; for(int i=0;i<3;++i) out[i]=sqrt(a[i]); return out; }
PT_FN float3 pow(float3 a,float3 b) { float3 out; for(int i=0;i<3;++i) out[i]=pow(a[i],b[i]); return out; }
PT_FN float3 pow(float3 a,float b) { return pow(a,(float3)(b)); }
PT_FN float dot(float3 a,float3 b) { float out=0; for(int i=0;i<3;++i) out+=a[i]*b[i]; return out; }
PT_FN float length(float3 a) { return sqrt(dot(a,a)); }
PT_FN float3 normalize(float3 a) { return a/length(a); }
PT_FN float3 fract(float3 a) { return a-floor(a); }
PT_FN float3 saturate(float3 a) { return clamp(a,0.f,1.f); }
PT_FN int3 isfinite(float3 a) { int3 out; for(int i=0;i<3;++i) out[i]=__builtin_isfinite(a[i]) ? -1 : 0; return out; }
PT_FN bool all(int3 a) { bool out=true; for(int i=0;i<3;++i) out=out && (a[i]!=0); return out; }
PT_FN bool any(int3 a) { bool out=false; for(int i=0;i<3;++i) out=out || (a[i]!=0); return out; }
PT_FN float3 select(float3 a,float3 b,int3 c) { float3 out; for(int i=0;i<3;++i) out[i]=c[i] ? b[i] : a[i]; return out; }
PT_FN float4 min(float4 a,float4 b) { float4 out; for(int i=0;i<4;++i) out[i]=min(a[i],b[i]); return out; }
PT_FN float4 min(float4 a,float b) { return min(a,(float4)(b)); }
PT_FN float4 min(float a,float4 b) { return min((float4)(a),b); }
PT_FN float4 max(float4 a,float4 b) { float4 out; for(int i=0;i<4;++i) out[i]=max(a[i],b[i]); return out; }
PT_FN float4 max(float4 a,float b) { return max(a,(float4)(b)); }
PT_FN float4 max(float a,float4 b) { return max((float4)(a),b); }
PT_FN float4 clamp(float4 a,float lo,float hi) { return min(max(a,lo),hi); }
PT_FN float4 abs(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=abs(a[i]); return out; }
PT_FN float4 floor(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=floor(a[i]); return out; }
PT_FN float4 ceil(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=ceil(a[i]); return out; }
PT_FN float4 exp(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=exp(a[i]); return out; }
PT_FN float4 exp2(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=exp2(a[i]); return out; }
PT_FN float4 sqrt(float4 a) { float4 out; for(int i=0;i<4;++i) out[i]=sqrt(a[i]); return out; }
PT_FN float4 pow(float4 a,float4 b) { float4 out; for(int i=0;i<4;++i) out[i]=pow(a[i],b[i]); return out; }
PT_FN float4 pow(float4 a,float b) { return pow(a,(float4)(b)); }
PT_FN float dot(float4 a,float4 b) { float out=0; for(int i=0;i<4;++i) out+=a[i]*b[i]; return out; }
PT_FN float length(float4 a) { return sqrt(dot(a,a)); }
PT_FN float4 normalize(float4 a) { return a/length(a); }
PT_FN float4 fract(float4 a) { return a-floor(a); }
PT_FN float4 saturate(float4 a) { return clamp(a,0.f,1.f); }
PT_FN int4 isfinite(float4 a) { int4 out; for(int i=0;i<4;++i) out[i]=__builtin_isfinite(a[i]) ? -1 : 0; return out; }
PT_FN bool all(int4 a) { bool out=true; for(int i=0;i<4;++i) out=out && (a[i]!=0); return out; }
PT_FN bool any(int4 a) { bool out=false; for(int i=0;i<4;++i) out=out || (a[i]!=0); return out; }
PT_FN float4 select(float4 a,float4 b,int4 c) { float4 out; for(int i=0;i<4;++i) out[i]=c[i] ? b[i] : a[i]; return out; }
PT_FN float saturate(float a) { return clamp(a,0.f,1.f); }
PT_FN float3 cross(float3 a,float3 b) { return f3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
PT_FN float3 reflect(float3 d,float3 n) { return d-2*dot(d,n)*n; }
PT_FN int2 min(int2 a,int2 b) { return i2(min(a.x,b.x),min(a.y,b.y)); }
PT_FN int2 max(int2 a,int2 b) { return i2(max(a.x,b.x),max(a.y,b.y)); }
} // namespace pt
