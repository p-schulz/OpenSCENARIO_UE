#pragma once
#include "CoreMinimal.h"
class FXmlAttribute { public: FString Tag, Value; const FString& GetTag() const { return Tag; } const FString& GetValue() const { return Value; } };
class FXmlNode { public: FString Tag; TArray<FXmlAttribute> Attrs; TArray<FXmlNode*> Kids;
	const FString& GetTag() const { return Tag; } const TArray<FXmlNode*>& GetChildrenNodes() const { return Kids; } const TArray<FXmlAttribute>& GetAttributes() const { return Attrs; } };
