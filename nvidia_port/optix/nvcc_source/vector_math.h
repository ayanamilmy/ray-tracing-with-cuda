#pragma once
#include <cmath>
#include <cstdint>
#include <type_traits>
#if defined(__CUDACC__)
#define PT_FN __host__ __device__ __forceinline__
#define PT_NOINLINE __host__ __device__ __noinline__ inline
#else
#define PT_FN inline
#define PT_NOINLINE __attribute__((noinline)) inline
#endif
namespace pt {
using uint=unsigned; using uchar=unsigned char;
template<class T,int N> struct Fields;
template<class T> struct Fields<T,2> {union {T data[2];struct {T x,y;};struct {T r,g;};};};
template<class T> struct Fields<T,3> {union {T data[4];struct {T x,y,z,pad;};struct {T r,g,b,pad2;};};};
template<class T> struct Fields<T,4> {union {T data[4];struct {T x,y,z,w;};struct {T r,g,b,a;};};};
template<class T,int N> struct alignas(sizeof(T)*(N==3?4:N)) Vec:Fields<T,N> {
 PT_FN constexpr Vec() = default;
 PT_FN constexpr Vec(T a):Fields<T,N>{} {for(int i=0;i<N;++i)this->data[i]=a;}
 template<class... A> requires(sizeof...(A)==N) PT_FN constexpr Vec(A... a):Fields<T,N>{} {T v[N]={T(a)...};for(int i=0;i<N;++i)this->data[i]=v[i];}
 PT_FN constexpr T& operator[](int i) {return this->data[i];}
 PT_FN constexpr T operator[](int i) const {return this->data[i];}
 PT_FN constexpr Vec<T,2> xy() const {return Vec<T,2>(this->data[0],this->data[1]);}
 PT_FN void set_xy(Vec<T,2> v) {this->data[0]=v[0];this->data[1]=v[1];}
 PT_FN void set_xy(T v) {set_xy(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,2> zw() const {return Vec<T,2>(this->data[2],this->data[3]);}
 PT_FN void set_zw(Vec<T,2> v) {this->data[2]=v[0];this->data[3]=v[1];}
 PT_FN void set_zw(T v) {set_zw(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,3> xyz() const {return Vec<T,3>(this->data[0],this->data[1],this->data[2]);}
 PT_FN void set_xyz(Vec<T,3> v) {this->data[0]=v[0];this->data[1]=v[1];this->data[2]=v[2];}
 PT_FN void set_xyz(T v) {set_xyz(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,4> xyzw() const {return Vec<T,4>(this->data[0],this->data[1],this->data[2],this->data[3]);}
 PT_FN void set_xyzw(Vec<T,4> v) {this->data[0]=v[0];this->data[1]=v[1];this->data[2]=v[2];this->data[3]=v[3];}
 PT_FN void set_xyzw(T v) {set_xyzw(Vec<T,4>(v));}
 PT_FN constexpr Vec<T,3> xxx() const {return Vec<T,3>(this->data[0],this->data[0],this->data[0]);}
 PT_FN void set_xxx(Vec<T,3> v) {this->data[0]=v[0];this->data[0]=v[1];this->data[0]=v[2];}
 PT_FN void set_xxx(T v) {set_xxx(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,4> xxxx() const {return Vec<T,4>(this->data[0],this->data[0],this->data[0],this->data[0]);}
 PT_FN void set_xxxx(Vec<T,4> v) {this->data[0]=v[0];this->data[0]=v[1];this->data[0]=v[2];this->data[0]=v[3];}
 PT_FN void set_xxxx(T v) {set_xxxx(Vec<T,4>(v));}
 PT_FN constexpr Vec<T,3> www() const {return Vec<T,3>(this->data[3],this->data[3],this->data[3]);}
 PT_FN void set_www(Vec<T,3> v) {this->data[3]=v[0];this->data[3]=v[1];this->data[3]=v[2];}
 PT_FN void set_www(T v) {set_www(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,3> xyw() const {return Vec<T,3>(this->data[0],this->data[1],this->data[3]);}
 PT_FN void set_xyw(Vec<T,3> v) {this->data[0]=v[0];this->data[1]=v[1];this->data[3]=v[2];}
 PT_FN void set_xyw(T v) {set_xyw(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,2> xz() const {return Vec<T,2>(this->data[0],this->data[2]);}
 PT_FN void set_xz(Vec<T,2> v) {this->data[0]=v[0];this->data[2]=v[1];}
 PT_FN void set_xz(T v) {set_xz(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,3> xzw() const {return Vec<T,3>(this->data[0],this->data[2],this->data[3]);}
 PT_FN void set_xzw(Vec<T,3> v) {this->data[0]=v[0];this->data[2]=v[1];this->data[3]=v[2];}
 PT_FN void set_xzw(T v) {set_xzw(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,2> yx() const {return Vec<T,2>(this->data[1],this->data[0]);}
 PT_FN void set_yx(Vec<T,2> v) {this->data[1]=v[0];this->data[0]=v[1];}
 PT_FN void set_yx(T v) {set_yx(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,3> yyy() const {return Vec<T,3>(this->data[1],this->data[1],this->data[1]);}
 PT_FN void set_yyy(Vec<T,3> v) {this->data[1]=v[0];this->data[1]=v[1];this->data[1]=v[2];}
 PT_FN void set_yyy(T v) {set_yyy(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,2> yz() const {return Vec<T,2>(this->data[1],this->data[2]);}
 PT_FN void set_yz(Vec<T,2> v) {this->data[1]=v[0];this->data[2]=v[1];}
 PT_FN void set_yz(T v) {set_yz(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,3> yzw() const {return Vec<T,3>(this->data[1],this->data[2],this->data[3]);}
 PT_FN void set_yzw(Vec<T,3> v) {this->data[1]=v[0];this->data[2]=v[1];this->data[3]=v[2];}
 PT_FN void set_yzw(T v) {set_yzw(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,3> zyw() const {return Vec<T,3>(this->data[2],this->data[1],this->data[3]);}
 PT_FN void set_zyw(Vec<T,3> v) {this->data[2]=v[0];this->data[1]=v[1];this->data[3]=v[2];}
 PT_FN void set_zyw(T v) {set_zyw(Vec<T,3>(v));}
 PT_FN constexpr Vec<T,2> zz() const {return Vec<T,2>(this->data[2],this->data[2]);}
 PT_FN void set_zz(Vec<T,2> v) {this->data[2]=v[0];this->data[2]=v[1];}
 PT_FN void set_zz(T v) {set_zz(Vec<T,2>(v));}
 PT_FN constexpr Vec<T,3> zzz() const {return Vec<T,3>(this->data[2],this->data[2],this->data[2]);}
 PT_FN void set_zzz(Vec<T,3> v) {this->data[2]=v[0];this->data[2]=v[1];this->data[2]=v[2];}
 PT_FN void set_zzz(T v) {set_zzz(Vec<T,3>(v));}
};
template<class T,int N> PT_FN Vec<T,N> operator+(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]+b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator+(Vec<T,N> a,S b) {return a + Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator+(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) + b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator+=(Vec<T,N>& a,S b) {a=a+b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator-(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]-b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator-(Vec<T,N> a,S b) {return a - Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator-(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) - b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator-=(Vec<T,N>& a,S b) {a=a-b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator*(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]*b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator*(Vec<T,N> a,S b) {return a * Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator*(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) * b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator*=(Vec<T,N>& a,S b) {a=a*b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator/(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]/b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator/(Vec<T,N> a,S b) {return a / Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator/(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) / b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator/=(Vec<T,N>& a,S b) {a=a/b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator&(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]&b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator&(Vec<T,N> a,S b) {return a & Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator&(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) & b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator&=(Vec<T,N>& a,S b) {a=a&b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator|(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]|b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator|(Vec<T,N> a,S b) {return a | Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator|(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) | b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator|=(Vec<T,N>& a,S b) {a=a|b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator^(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]^b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator^(Vec<T,N> a,S b) {return a ^ Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator^(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) ^ b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator^=(Vec<T,N>& a,S b) {a=a^b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator<<(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]<<b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator<<(Vec<T,N> a,S b) {return a << Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator<<(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) << b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator<<=(Vec<T,N>& a,S b) {a=a<<b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator>>(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]>>b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator>>(Vec<T,N> a,S b) {return a >> Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator>>(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) >> b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator>>=(Vec<T,N>& a,S b) {a=a>>b;return a;}
template<class T,int N> PT_FN Vec<T,N> operator%(Vec<T,N> a,Vec<T,N> b) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=a[i]%b[i];return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator%(Vec<T,N> a,S b) {return a % Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<T,N> operator%(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) % b;}
template<class T,int N,class S> PT_FN Vec<T,N>& operator%=(Vec<T,N>& a,S b) {a=a%b;return a;}
template<class T,int N> PT_FN Vec<int,N> operator<(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]<b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator<(Vec<T,N> a,S b) {return a < Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator<(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) < b;}
template<class T,int N> PT_FN Vec<int,N> operator>(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]>b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator>(Vec<T,N> a,S b) {return a > Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator>(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) > b;}
template<class T,int N> PT_FN Vec<int,N> operator<=(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]<=b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator<=(Vec<T,N> a,S b) {return a <= Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator<=(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) <= b;}
template<class T,int N> PT_FN Vec<int,N> operator>=(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]>=b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator>=(Vec<T,N> a,S b) {return a >= Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator>=(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) >= b;}
template<class T,int N> PT_FN Vec<int,N> operator==(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]==b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator==(Vec<T,N> a,S b) {return a == Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator==(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) == b;}
template<class T,int N> PT_FN Vec<int,N> operator!=(Vec<T,N> a,Vec<T,N> b) {Vec<int,N> o;for(int i=0;i<N;++i)o[i]=a[i]!=b[i]?-1:0;return o;}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator!=(Vec<T,N> a,S b) {return a != Vec<T,N>(T(b));}
template<class T,int N,class S> requires(std::is_arithmetic_v<S>) PT_FN Vec<int,N> operator!=(S a,Vec<T,N> b) {return Vec<T,N>(T(a)) != b;}
template<class T,int N> PT_FN Vec<T,N> operator-(Vec<T,N> a) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=-a[i];return o;}
template<class T,int N> PT_FN Vec<T,N> operator+(Vec<T,N> a) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=+a[i];return o;}
template<class T,int N> PT_FN Vec<T,N> operator~(Vec<T,N> a) {Vec<T,N> o;for(int i=0;i<N;++i)o[i]=~a[i];return o;}
using float2=Vec<float,2>;
using float3=Vec<float,3>;
using float4=Vec<float,4>;
using uint2=Vec<unsigned,2>;
using uint3=Vec<unsigned,3>;
using uint4=Vec<unsigned,4>;
using int2=Vec<int,2>;
using int3=Vec<int,3>;
using int4=Vec<int,4>;
using uchar4=Vec<unsigned char,4>; using bool2=int2;using bool3=int3;using bool4=int4;
template<class To,class From> PT_FN To convert(From a) {To out;for(int i=0;i<int(sizeof(To)/sizeof(out[0]));++i)out[i]=a[i];return out;}
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
PT_FN float2 f2(uint2 a) { return convert<float2>(a); }
PT_FN float2 f2(int2 a) { return convert<float2>(a); }
PT_FN int2 i2(uint2 a) { return convert<int2>(a); }
PT_FN int2 i2(float2 a) { return convert<int2>(a); }
template<class To,class From> PT_FN To as_type(From value) { static_assert(sizeof(To)==sizeof(From)); union {From from;To to;} u={value};return u.to; }
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
