#pragma once
#include "XmlNode.h"
namespace EConstructMethod { enum Type { ConstructFromFile, ConstructFromBuffer }; }
class FXmlFile {
	FXmlNode* Root = nullptr; FString Err; const std::string* Src; size_t P = 0; std::string Buf;
	void Ws() { while (P < Buf.size() && std::isspace((unsigned char)Buf[P])) ++P; }
	bool Skip() { for (;;) { Ws(); if (Buf.compare(P, 2, "<?") == 0) { P = Buf.find("?>", P); if (P == std::string::npos) return false; P += 2; } else if (Buf.compare(P, 4, "<!--") == 0) { P = Buf.find("-->", P); if (P == std::string::npos) return false; P += 3; } else return true; } }
	FXmlNode* Elem() {
		if (!Skip() || P >= Buf.size() || Buf[P] != '<') return nullptr; ++P;
		FXmlNode* N = new FXmlNode; size_t s = P; while (P < Buf.size() && !std::isspace((unsigned char)Buf[P]) && Buf[P] != '>' && Buf[P] != '/') ++P; N->Tag = FString(Buf.substr(s, P - s));
		for (;;) { Ws(); if (P >= Buf.size()) return nullptr; if (Buf[P] == '/') { P += 2; return N; } if (Buf[P] == '>') { ++P; break; }
			size_t a = P; while (P < Buf.size() && Buf[P] != '=' && !std::isspace((unsigned char)Buf[P])) ++P; std::string name = Buf.substr(a, P - a); Ws(); ++P; Ws(); char q = Buf[P++]; size_t v = P; while (Buf[P] != q) ++P; FXmlAttribute at; at.Tag = FString(name); at.Value = FString(Buf.substr(v, P - v)); ++P; N->Attrs.Add(at); }
		for (;;) { if (!Skip()) return nullptr; if (Buf.compare(P, 2, "</") == 0) { P = Buf.find('>', P) + 1; return N; } if (Buf[P] == '<') { FXmlNode* K = Elem(); if (!K) return nullptr; N->Kids.Add(K); } else ++P; }
	}
public:
	FXmlFile(const FString& s, EConstructMethod::Type) { Buf = s.S; Root = Elem(); if (!Root) Err = "parse error"; }
	bool IsValid() const { return Root != nullptr; } FString GetLastError() const { return Err; } const FXmlNode* GetRootNode() const { return Root; }
};
