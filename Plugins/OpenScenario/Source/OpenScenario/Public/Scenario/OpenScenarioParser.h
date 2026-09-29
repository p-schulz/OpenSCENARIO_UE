#pragma once

#include "CoreMinimal.h"
#include "Scenario/OpenScenarioModel.h"

class FXmlNode;

/**
 * Reader for ASAM OpenSCENARIO 1.x XML. Understands parameter references ($name), simple
 * expressions (${$a * 2 + 1}), parameter overrides and catalog references to Vehicle, Pedestrian
 * and MiscObject entries. Unsupported elements are reported in Messages and skipped.
 */
class OPENSCENARIO_API FOpenScenarioParser
{
public:
	/**
	 * @param BaseDirectory Directory used to resolve relative catalog paths (may be empty).
	 * @return false if the XML is invalid or is not an OpenSCENARIO document.
	 */
	bool Parse(const FString& Xml, const FString& BaseDirectory, const TMap<FString, FString>& ParameterOverrides, FOSCScenario& OutScenario, TArray<FString>& OutMessages);

private:
	// Attribute access with parameter resolution
	FString Resolve(const FString& Value);
	bool TryAttr(const FXmlNode* Node, const TCHAR* Name, FString& Out);
	FString Attr(const FXmlNode* Node, const TCHAR* Name, const FString& Default = FString());
	double AttrD(const FXmlNode* Node, const TCHAR* Name, double Default = 0.0);
	int32 AttrI(const FXmlNode* Node, const TCHAR* Name, int32 Default = 0);
	bool AttrB(const FXmlNode* Node, const TCHAR* Name, bool Default = false);

	void Warn(const FString& Message);
	void ReadParameterDeclarations(const FXmlNode* Node, const TMap<FString, FString>* Overrides, TArray<FOSCParameterDeclaration>* OutDeclarations = nullptr);

	// Entities
	void ParseEntities(const FXmlNode* EntitiesNode, FOSCScenario& Out);
	void ParseEntityNode(const FXmlNode* Node, FOSCEntity& Entity);
	bool ParseCatalogEntity(const FXmlNode* RefNode, FOSCEntity& Entity);

	// Positions
	FOSCPosition ParsePosition(const FXmlNode* PositionNode);
	void ParseOrientation(const FXmlNode* PosChild, FOSCPosition& Pos);

	// Actions
	FOSCAction ParseAction(const FXmlNode* Node);
	FOSCDynamics ParseDynamics(const FXmlNode* Node);
	void ParseTrajectory(const FXmlNode* TrajectoryNode, FOSCAction& Action);

	// Triggers
	FOSCTrigger ParseTrigger(const FXmlNode* TriggerNode);
	FOSCCondition ParseCondition(const FXmlNode* Node);
	void ParseEntityCondition(const FXmlNode* EntityConditionNode, FOSCCondition& Cond);
	static EOSCRule ParseRule(const FString& S);

	// Storyboard
	void ParseInit(const FXmlNode* InitNode, FOSCScenario& Out);
	void ParseStoryboard(const FXmlNode* StoryboardNode, FOSCScenario& Out);

	TMap<FString, FString> Params;
	TMap<FString, FString> CatalogDirs;
	FString BaseDir;
	TArray<FString>* Messages = nullptr;
};
