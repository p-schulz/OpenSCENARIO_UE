#pragma once

#include "CoreMinimal.h"
#include "XmlNode.h"

/** Thin helpers around FXmlNode used by the OpenDRIVE reader. */
namespace ODRXml
{
	inline bool TryAttr(const FXmlNode* Node, const TCHAR* Name, FString& Out)
	{
		if (!Node)
		{
			return false;
		}
		for (const FXmlAttribute& A : Node->GetAttributes())
		{
			if (A.GetTag().Equals(Name, ESearchCase::CaseSensitive))
			{
				Out = A.GetValue();
				return true;
			}
		}
		return false;
	}

	inline const FXmlNode* Child(const FXmlNode* Node, const TCHAR* Tag)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (const FXmlNode* C : Node->GetChildrenNodes())
		{
			if (C && C->GetTag().Equals(Tag, ESearchCase::CaseSensitive))
			{
				return C;
			}
		}
		return nullptr;
	}

	inline TArray<const FXmlNode*> Children(const FXmlNode* Node, const TCHAR* Tag)
	{
		TArray<const FXmlNode*> Out;
		if (Node)
		{
			for (const FXmlNode* C : Node->GetChildrenNodes())
			{
				if (C && C->GetTag().Equals(Tag, ESearchCase::CaseSensitive))
				{
					Out.Add(C);
				}
			}
		}
		return Out;
	}

	inline bool ParseBool(const FString& S)
	{
		return S.Equals(TEXT("true"), ESearchCase::IgnoreCase) || S == TEXT("1");
	}
}
