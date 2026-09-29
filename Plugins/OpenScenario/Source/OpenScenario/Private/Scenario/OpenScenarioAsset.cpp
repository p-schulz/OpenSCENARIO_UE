#include "Scenario/OpenScenarioAsset.h"
#include "OpenDrive/OpenDriveAsset.h"
#include "OpenDrive/OpenDriveMap.h"
#include "Scenario/OpenScenarioParser.h"
#include "Scenario/OpenScenarioWriter.h"
#include "GameFramework/Actor.h"
#include "OpenScenarioModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/UnrealType.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

bool UOpenScenarioAsset::SetSource(const FString& Xml, const FString& Filename)
{
	SourceXml = Xml;
	SourceFilename = Filename;
	Reparse();
	return bParsedOk;
}

FString UOpenScenarioAsset::GetBaseDirectory() const
{
	if (!SourceFilename.IsEmpty())
	{
		return FPaths::GetPath(SourceFilename);
	}
	return FPaths::ProjectDir();
}

void UOpenScenarioAsset::Reparse()
{
	FallbackMap.Reset();
	ParseMessages.Reset();
	EntityNames.Reset();

	FOpenScenarioParser Parser;
	bParsedOk = Parser.Parse(SourceXml, GetBaseDirectory(), ParameterOverrides, Scenario, ParseMessages);
	if (bParsedOk)
	{
		ScenarioDescription = Scenario.Description;
		Author = Scenario.Author;
		RoadNetworkFile = Scenario.RoadNetworkFile;
		for (const FOSCEntity& E : Scenario.Entities)
		{
			EntityNames.Add(E.Name);
		}
	}
	else
	{
		Scenario = FOSCScenario();
		UE_LOG(LogOpenScenario, Warning, TEXT("OpenSCENARIO asset '%s' failed to parse: %s"), *GetPathName(), ParseMessages.Num() > 0 ? *ParseMessages.Last() : TEXT("unknown error"));
	}
	OnReparsed.Broadcast();
}

bool UOpenScenarioAsset::ApplyScenarioModel(const FOSCScenario& Model)
{
	return SetSource(FOpenScenarioWriter::Write(Model), SourceFilename);
}

UClass* UOpenScenarioAsset::FindEntityActorClass(const FString& EntityName) const
{
	const TSoftClassPtr<AActor>* Mapped = EntityActorClasses.Find(EntityName);
	return (Mapped && !Mapped->IsNull()) ? Mapped->LoadSynchronous() : nullptr;
}

UClass* UOpenScenarioAsset::FindKindActorClass(EOSCEntityKind Kind) const
{
	const TSoftClassPtr<AActor>* Mapped = &VehicleActorClass;
	if (Kind == EOSCEntityKind::Pedestrian) { Mapped = &PedestrianActorClass; }
	else if (Kind == EOSCEntityKind::MiscObject || Kind == EOSCEntityKind::External) { Mapped = &MiscObjectActorClass; }
	return Mapped->IsNull() ? nullptr : Mapped->LoadSynchronous();
}

bool UOpenScenarioAsset::ReadReferencedRoadNetworkFile(FString& OutXml, FString& OutResolvedPath) const
{
	const FString& Rel = Scenario.RoadNetworkFile;
	if (Rel.IsEmpty())
	{
		return false;
	}

	const FString Base = GetBaseDirectory();
	const FString Clean = FPaths::GetCleanFilename(Rel);
	TArray<FString> Candidates;
	if (!FPaths::IsRelative(Rel))
	{
		Candidates.Add(Rel);
	}
	else
	{
		Candidates.Add(FPaths::ConvertRelativePathToFull(Base, Rel));
	}
	Candidates.Add(FPaths::Combine(Base, Clean));
	Candidates.Add(FPaths::Combine(FPaths::ProjectContentDir(), Rel));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), Rel));

	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate) && FFileHelper::LoadFileToString(OutXml, *Candidate))
		{
			OutResolvedPath = FPaths::ConvertRelativePathToFull(Candidate);
			return true;
		}
	}
	return false;
}

TSharedPtr<const FOpenDriveMap> UOpenScenarioAsset::ResolveRoadNetwork()
{
	if (RoadNetwork)
	{
		if (TSharedPtr<const FOpenDriveMap> Map = RoadNetwork->GetMap())
		{
			return Map;
		}
	}
	if (!FallbackMap.IsValid())
	{
		FString Xml, Path;
		if (ReadReferencedRoadNetworkFile(Xml, Path))
		{
			TSharedPtr<FOpenDriveMap> Loaded = MakeShared<FOpenDriveMap>();
			FString Error;
			if (Loaded->LoadFromString(Xml, Error))
			{
				FallbackMap = Loaded;
				UE_LOG(LogOpenScenario, Log, TEXT("Loaded referenced OpenDRIVE file '%s'."), *Path);
			}
			else
			{
				UE_LOG(LogOpenScenario, Warning, TEXT("Referenced OpenDRIVE file '%s' is invalid: %s"), *Path, *Error);
			}
		}
	}
	return FallbackMap;
}

FString UOpenScenarioAsset::MakeTemplateXml(const FString& Name)
{
	FString Xml = TEXT(R"(<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="1" date="2025-01-01T00:00:00" description="__NAME__" author="" />
  <ParameterDeclarations>
    <ParameterDeclaration name="EgoSpeed" parameterType="double" value="10.0" />
    <ParameterDeclaration name="TargetSpeed" parameterType="double" value="5.0" />
  </ParameterDeclarations>
  <CatalogLocations />
  <RoadNetwork>
    <!-- <LogicFile filepath="MyRoad.xodr" /> -->
  </RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      <Vehicle name="EgoCar" vehicleCategory="car">
        <BoundingBox>
          <Center x="1.4" y="0.0" z="0.75" />
          <Dimensions width="1.8" length="4.5" height="1.5" />
        </BoundingBox>
        <Performance maxSpeed="60" maxAcceleration="3.5" maxDeceleration="8" />
      </Vehicle>
    </ScenarioObject>
    <ScenarioObject name="Target">
      <Vehicle name="TargetCar" vehicleCategory="car">
        <BoundingBox>
          <Center x="1.4" y="0.0" z="0.75" />
          <Dimensions width="1.8" length="4.5" height="1.5" />
        </BoundingBox>
        <Performance maxSpeed="60" maxAcceleration="3.5" maxDeceleration="8" />
      </Vehicle>
    </ScenarioObject>
  </Entities>
  <Storyboard>
    <Init>
      <Actions>
        <Private entityRef="Ego">
          <PrivateAction>
            <TeleportAction>
              <Position><WorldPosition x="0" y="0" z="0" h="0" /></Position>
            </TeleportAction>
          </PrivateAction>
          <PrivateAction>
            <LongitudinalAction>
              <SpeedAction>
                <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time" />
                <SpeedActionTarget><AbsoluteTargetSpeed value="$EgoSpeed" /></SpeedActionTarget>
              </SpeedAction>
            </LongitudinalAction>
          </PrivateAction>
        </Private>
        <Private entityRef="Target">
          <PrivateAction>
            <TeleportAction>
              <Position><WorldPosition x="30" y="0" z="0" h="0" /></Position>
            </TeleportAction>
          </PrivateAction>
          <PrivateAction>
            <LongitudinalAction>
              <SpeedAction>
                <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time" />
                <SpeedActionTarget><AbsoluteTargetSpeed value="$TargetSpeed" /></SpeedActionTarget>
              </SpeedAction>
            </LongitudinalAction>
          </PrivateAction>
        </Private>
      </Actions>
    </Init>
    <Story name="MainStory">
      <Act name="MainAct">
        <ManeuverGroup maximumExecutionCount="1" name="TargetGroup">
          <Actors selectTriggeringEntities="false">
            <EntityRef entityRef="Target" />
          </Actors>
          <Maneuver name="TargetManeuver">
            <Event name="TargetBrakes" priority="overwrite" maximumExecutionCount="1">
              <Action name="SlowDown">
                <PrivateAction>
                  <LongitudinalAction>
                    <SpeedAction>
                      <SpeedActionDynamics dynamicsShape="linear" value="2.0" dynamicsDimension="rate" />
                      <SpeedActionTarget><AbsoluteTargetSpeed value="0" /></SpeedActionTarget>
                    </SpeedAction>
                  </LongitudinalAction>
                </PrivateAction>
              </Action>
              <StartTrigger>
                <ConditionGroup>
                  <Condition name="After3s" delay="0" conditionEdge="rising">
                    <ByValueCondition>
                      <SimulationTimeCondition value="3.0" rule="greaterThan" />
                    </ByValueCondition>
                  </Condition>
                </ConditionGroup>
              </StartTrigger>
            </Event>
          </Maneuver>
        </ManeuverGroup>
        <StartTrigger>
          <ConditionGroup>
            <Condition name="Start" delay="0" conditionEdge="rising">
              <ByValueCondition>
                <SimulationTimeCondition value="0" rule="greaterOrEqual" />
              </ByValueCondition>
            </Condition>
          </ConditionGroup>
        </StartTrigger>
      </Act>
    </Story>
    <StopTrigger>
      <ConditionGroup>
        <Condition name="EndOfScenario" delay="0" conditionEdge="rising">
          <ByValueCondition>
            <SimulationTimeCondition value="30.0" rule="greaterThan" />
          </ByValueCondition>
        </Condition>
      </ConditionGroup>
    </StopTrigger>
  </Storyboard>
</OpenSCENARIO>
)");
	Xml = Xml.Replace(TEXT("__NAME__"), *Name);
	return Xml;
}

void UOpenScenarioAsset::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

void UOpenScenarioAsset::PostLoad()
{
	Super::PostLoad();
	if (!SourceXml.IsEmpty())
	{
		Reparse();
	}
}

void UOpenScenarioAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		Context.AddTag(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}
#endif
	Super::GetAssetRegistryTags(Context);
}

#if WITH_EDITOR
void UOpenScenarioAsset::PostEditUndo()
{
	Super::PostEditUndo();
	Reparse();
}

void UOpenScenarioAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Prop = PropertyChangedEvent.GetPropertyName();
	if (Prop == GET_MEMBER_NAME_CHECKED(UOpenScenarioAsset, SourceXml) || Prop == GET_MEMBER_NAME_CHECKED(UOpenScenarioAsset, ParameterOverrides))
	{
		Reparse();
	}
}
#endif
