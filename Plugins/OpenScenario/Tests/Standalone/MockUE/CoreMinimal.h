// Minimal stand-in for the subset of Unreal Engine used by the plugin's runtime code (test only).
#pragma once
#include <string>
#include <random>
#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <type_traits>

typedef char TCHAR;
#define TEXT(x) x
#define OPENSCENARIO_API
typedef int32_t int32; typedef int64_t int64; typedef uint8_t uint8; typedef uint32_t uint32;
#define INDEX_NONE (-1)
#define MAX_int32 INT32_MAX
#define UE_DOUBLE_PI 3.14159265358979323846264338327950288
#define WITH_EDITOR 0
#define WITH_EDITORONLY_DATA 0
#define UCLASS(...)
#define USTRUCT(...)
#define UPROPERTY(...)
#define UENUM(...)
#define UFUNCTION(...)
struct UClass {};
#define GENERATED_BODY() public: typedef UObject Super; static UClass* StaticClass(){ static UClass c; return &c; }
#define DECLARE_LOG_CATEGORY_EXTERN(Name, a, b) extern int Name;
#define DEFINE_LOG_CATEGORY(Name) int Name = 0;
#define IMPLEMENT_MODULE(a, b)
#define UE_LOG(Cat, Verb, Fmt, ...) do { std::printf("[" #Verb "] " Fmt "\n", ##__VA_ARGS__); } while (0)
template <class T> T&& MoveTemp(T& t) { return std::move(t); }

enum class ESearchCase { CaseSensitive, IgnoreCase };

class FString {
public:
	std::string S;
	FString() {}
	FString(const char* c) : S(c ? c : "") {}
	FString(const std::string& s) : S(s) {}
	int32 Len() const { return (int32)S.size(); }
	bool IsEmpty() const { return S.empty(); }
	const char* operator*() const { return S.c_str(); }
	char operator[](int32 i) const { return S[i]; }
	void Reset() { S.clear(); }
	static bool EqI(const std::string& a, const std::string& b) {
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
		return true;
	}
	bool operator==(const FString& o) const { return EqI(S, o.S); }
	bool operator==(const char* o) const { return EqI(S, o); }
	bool operator!=(const FString& o) const { return !(*this == o); }
	bool operator!=(const char* o) const { return !(*this == o); }
	bool operator<(const FString& o) const { return S < o.S; }
	bool Equals(const FString& o, ESearchCase c = ESearchCase::IgnoreCase) const { return c == ESearchCase::CaseSensitive ? S == o.S : EqI(S, o.S); }
	bool StartsWith(const FString& p) const { return S.rfind(p.S, 0) == 0; }
	bool EndsWith(const FString& p) const { return S.size() >= p.S.size() && S.compare(S.size() - p.S.size(), p.S.size(), p.S) == 0; }
	FString Mid(int32 start, int32 count = INT32_MAX) const { if (start >= Len()) return FString(); return FString(S.substr(start, count)); }
	FString TrimStartAndEnd() const { size_t a = S.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return FString(); size_t b = S.find_last_not_of(" \t\r\n"); return FString(S.substr(a, b - a + 1)); }
	FString ToLower() const { std::string r = S; for (auto& c : r) c = (char)std::tolower((unsigned char)c); return FString(r); }
	FString Replace(const char* from, const char* to) const { std::string r = S, f = from, t = to; size_t p = 0; while ((p = r.find(f, p)) != std::string::npos) { r.replace(p, f.size(), t); p += t.size(); } return FString(r); }
	FString operator+(const FString& o) const { return FString(S + o.S); }
	FString& operator+=(const FString& o) { S += o.S; return *this; }
	FString operator/(const FString& o) const { return FString(S + "/" + o.S); }
	static FString Printf(const char* fmt, ...) { char buf[4096]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a); return FString(buf); }
	static FString SanitizeFloat(double d) { char b[64]; snprintf(b, sizeof b, "%.6f", d); std::string s = b; while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back(); return FString(s); }
};
inline FString operator+(const char* a, const FString& b) { return FString(a) + b; }

template <class F> using TFunction = std::function<F>;
template <class K, class V> struct TPair { K Key; V Value; TPair() {} TPair(const K& k, const V& v) : Key(k), Value(v) {} };

template <class T> class TArray {
public:
	std::deque<T> V;
	int32 Num() const { return (int32)V.size(); }
	int32 Add(const T& t) { V.push_back(t); return Num() - 1; }
	int32 Add(T&& t) { V.push_back(std::move(t)); return Num() - 1; }
	template <class... A> int32 Emplace(A&&... a) { V.emplace_back(std::forward<A>(a)...); return Num() - 1; }
	void Reset() { V.clear(); }
	void Reserve(int32) {}
	void Init(const T& t, int32 n) { V.assign(n, t); }
	void Append(const TArray& o) { for (auto& x : o.V) V.push_back(x); }
	T& Last() { return V.back(); }
	const T& Last() const { return V.back(); }
	bool IsValidIndex(int32 i) const { return i >= 0 && i < Num(); }
	T& operator[](int32 i) { return V[i]; }
	const T& operator[](int32 i) const { return V[i]; }
	auto begin() { return V.begin(); } auto end() { return V.end(); }
	auto begin() const { return V.begin(); } auto end() const { return V.end(); }
	// Real UE dereferences pointer elements before calling the predicate (TDereferenceWrapper in
	// Templates/Sorting.h), so a TArray<T*>::Sort predicate must take T&, not T*. Mirror that here so a
	// predicate written for the wrong (pointer) signature fails in this harness too, not only in a real
	// UE build.
	template <class P> void Sort(P p)
	{
		if constexpr (std::is_pointer<T>::value)
		{
			std::stable_sort(V.begin(), V.end(), [&p](T A, T B) { return p(*A, *B); });
		}
		else
		{
			std::stable_sort(V.begin(), V.end(), p);
		}
	}
	int32 AddDefaulted() { V.emplace_back(); return Num() - 1; }
	void RemoveAt(int32 i, int32 n = 1) { V.erase(V.begin() + i, V.begin() + i + n); }
	void Insert(const T& t, int32 i) { V.insert(V.begin() + i, t); }
	void Swap(int32 a, int32 b) { std::swap(V[a], V[b]); }
	bool Contains(const T& t) const { for (auto& x : V) if (x == t) return true; return false; }
};
namespace Algo { template <class A> void Reverse(A& a) { std::reverse(a.V.begin(), a.V.end()); } }

template <class K, class V> class TMap {
public:
	std::vector<TPair<K, V>> Items;
	V* Find(const K& k) { for (auto& p : Items) if (p.Key == k) return &p.Value; return nullptr; }
	const V* Find(const K& k) const { for (auto& p : Items) if (p.Key == k) return &p.Value; return nullptr; }
	bool Contains(const K& k) const { return Find(k) != nullptr; }
	V& Add(const K& k, const V& v) { if (V* e = Find(k)) { *e = v; return *e; } Items.emplace_back(k, v); return Items.back().Value; }
	void Reset() { Items.clear(); }
	int32 Remove(const K& k) { for (size_t i = 0; i < Items.size(); ++i) if (Items[i].Key == k) { Items.erase(Items.begin() + i); return 1; } return 0; }
	V& FindOrAdd(const K& k) { if (V* e = Find(k)) return *e; Items.emplace_back(k, V()); return Items.back().Value; }
	int32 Num() const { return (int32)Items.size(); }
	auto begin() { return Items.begin(); } auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); } auto end() const { return Items.end(); }
};

template <class T> class TSharedPtr {
public:
	std::shared_ptr<T> P;
	TSharedPtr() {}
	TSharedPtr(std::shared_ptr<T> p) : P(p) {}
	template <class U> TSharedPtr(const TSharedPtr<U>& o) : P(o.P) {}
	bool IsValid() const { return (bool)P; }
	explicit operator bool() const { return (bool)P; }
	void Reset() { P.reset(); }
	T* Get() const { return P.get(); }
	T* operator->() const { return P.get(); }
};
template <class T, class... A> TSharedPtr<T> MakeShared(A&&... a) { return TSharedPtr<T>(std::make_shared<T>(std::forward<A>(a)...)); }

struct FRandomStream { std::mt19937 G; FRandomStream() : G(1) {} void Initialize(int32 s) { G.seed((unsigned)s); }
	float FRand() { return std::uniform_real_distribution<float>(0.f, 1.f)(G) * 0.99999f; } int32 RandRange(int32 a, int32 b) { return std::uniform_int_distribution<int32>(a, b)(G); } };
template <class T> struct TNumericLimits { static T Max() { return std::numeric_limits<T>::max(); } };

struct FMath {
	template <class T> static T Abs(T a) { return a < 0 ? -a : a; }
	template <class T> static T Min(T a, T b) { return a < b ? a : b; }
	template <class T> static T Max(T a, T b) { return a > b ? a : b; }
	template <class T> static T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
	template <class T> static T Square(T a) { return a * a; }
	static double Sin(double a) { return std::sin(a); } static double Cos(double a) { return std::cos(a); }
	static double Atan(double a) { return std::atan(a); } static double Atan2(double y, double x) { return std::atan2(y, x); }
	static double Sqrt(double a) { return std::sqrt(a); } static double Fmod(double a, double b) { return std::fmod(a, b); }
	static int32 FloorToInt(double a) { return (int32)std::floor(a); } static int32 CeilToInt(double a) { return (int32)std::ceil(a); } static int32 RoundToInt(double a) { return (int32)std::lround(a); }
	template <class T> static T Lerp(const T& a, const T& b, double t) { return a + (b - a) * t; }
	static double RadiansToDegrees(double r) { return r * 180.0 / UE_DOUBLE_PI; } static double DegreesToRadians(double d) { return d * UE_DOUBLE_PI / 180.0; }
};
struct FCString { static double Atod(const char* s) { return std::strtod(s, nullptr); } static int32 Atoi(const char* s) { return std::atoi(s); } };
struct FChar { static bool IsWhitespace(char c) { return std::isspace((unsigned char)c); } static bool IsAlnum(char c) { return std::isalnum((unsigned char)c); } static bool IsDigit(char c) { return std::isdigit((unsigned char)c); } };
struct FDefaultValueHelper { static bool IsStringValidFloat(const FString& s) { if (s.IsEmpty()) return false; char* e; std::strtod(s.S.c_str(), &e); return *e == 0; } };

struct FVector {
	double X = 0, Y = 0, Z = 0;
	FVector() {} FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}
	FVector operator+(const FVector& o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
	FVector operator-(const FVector& o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); }
	FVector operator*(double s) const { return FVector(X * s, Y * s, Z * s); }
	double Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	double SizeSquared2D() const { return X * X + Y * Y; }
	static FVector OneVector;
};
struct FRotator { double Pitch, Yaw, Roll; FRotator(double p, double y, double r) : Pitch(p), Yaw(y), Roll(r) {} };
struct FQuat { double Yaw = 0; FQuat() {} FQuat(const FRotator& r) : Yaw(r.Yaw) {} FQuat operator*(const FQuat& o) const { FQuat q; q.Yaw = Yaw + o.Yaw; return q; } };
struct FTransform { FTransform() {} FTransform(const FRotator&, const FVector& v) : T(v) {} FVector T; FQuat R; static FTransform Identity; FQuat GetRotation() const { return R; } FVector TransformPosition(const FVector& v) const { return v + T; } void SetScale3D(const FVector&) {} };

class FAssetRegistryTagsContext {};
class UObject { public: virtual ~UObject() {} FString GetPathName() const { return "obj"; } FString GetName() const { return "obj"; } void MarkPackageDirty() {} virtual void PostInitProperties() {} virtual void PostLoad() {} virtual void GetAssetRegistryTags(FAssetRegistryTagsContext) const {} };
class USceneComponent {}; class UStaticMeshComponent {}; class UAssetImportData {};

template <class T> struct TObjectPtr { T* P = nullptr; TObjectPtr() {} TObjectPtr(T* p) : P(p) {} TObjectPtr& operator=(T* p) { P = p; return *this; } T* Get() const { return P; } T* operator->() const { return P; } operator T*() const { return P; } explicit operator bool() const { return P != nullptr; } };
template <class T> struct TSoftClassPtr { UClass* C = nullptr; UClass* LoadSynchronous() const { return C; } bool IsNull() const { return C == nullptr; } };
template <class T> struct TSubclassOf { UClass* C = nullptr; TSubclassOf() {} TSubclassOf(UClass* c) : C(c) {} UClass* Get() const { return C; } explicit operator bool() const { return C != nullptr; } };
template <class T, class U> T* Cast(U* p) { return dynamic_cast<T*>(p); }

class AActor : public UObject { public: virtual ~AActor() {} FVector LastLoc; void SetActorLocationAndRotation(const FVector& l, const FQuat&, bool, void*, int) { LastLoc = l; } void Destroy() {} };
enum class ESpawnActorCollisionHandlingMethod { AlwaysSpawn };
namespace ETeleportType { constexpr int TeleportPhysics = 1; }
struct FActorSpawnParameters { ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingOverride; };
class UWorld : public UObject { public: AActor* SpawnActor(UClass*, const FTransform*, const FActorSpawnParameters&) { return new AActor(); } };

template <class... A> struct TMulticastMock { std::vector<std::function<void(A...)>> F; void Broadcast(A... a) { for (auto& f : F) f(a...); } };
#define DECLARE_MULTICAST_DELEGATE_TwoParams(Name, T1, T2) typedef TMulticastMock<T1, T2> Name;
#define DECLARE_MULTICAST_DELEGATE(Name) typedef TMulticastMock<> Name;
typedef TMulticastMock<> FSimpleMulticastDelegate;

struct FPaths {
	static bool IsRelative(const FString& p) { return !std::filesystem::path(p.S).is_absolute(); }
	static FString ConvertRelativePathToFull(const FString& base, const FString& rel) { return FString((std::filesystem::path(base.S) / rel.S).lexically_normal().string()); }
	static FString ConvertRelativePathToFull(const FString& p) { return FString(std::filesystem::absolute(p.S).lexically_normal().string()); }
	static FString GetPath(const FString& p) { return FString(std::filesystem::path(p.S).parent_path().string()); }
	static FString GetCleanFilename(const FString& p) { return FString(std::filesystem::path(p.S).filename().string()); }
	static FString Combine(const FString& a, const FString& b) { return FString((std::filesystem::path(a.S) / b.S).string()); }
	static FString ProjectDir() { return "/nonexistent_project/"; } static FString ProjectContentDir() { return "/nonexistent_project/Content/"; }
	static bool FileExists(const FString& p) { return std::filesystem::is_regular_file(p.S); }
};
struct FFileHelper { static bool LoadFileToString(FString& out, const char* path) { std::ifstream f(path); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); out = FString(ss.str()); return true; } };
struct IFileManager { static IFileManager& Get() { static IFileManager m; return m; }
	void FindFiles(TArray<FString>& out, const char* pattern, bool, bool) { std::filesystem::path p(pattern); auto dir = p.parent_path(); auto ext = p.extension(); if (!std::filesystem::exists(dir)) return; for (auto& e : std::filesystem::directory_iterator(dir)) if (e.path().extension() == ext) out.Add(FString(e.path().filename().string())); } };
class IModuleInterface { public: virtual ~IModuleInterface() {} virtual void StartupModule() {} virtual void ShutdownModule() {} };
