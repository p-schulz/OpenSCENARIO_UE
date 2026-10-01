// Round-trip (parse -> write -> parse) and storyboard-edit tests, headless against the mock UE layer.
#include "CoreMinimal.h"
#include "OpenScenarioModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Scenario/OpenScenarioParser.h"
#include "Scenario/OpenScenarioWriter.h"
#include "Scenario/OpenScenarioModelEdit.h"
#include "Simulation/OpenScenarioRunner.h"
#include "Simulation/OpenScenarioEntityActor.h"

FVector FVector::OneVector(1, 1, 1);
FTransform FTransform::Identity;
void AOpenScenarioEntityActor::ConfigureFromEntity(const FOSCEntity&) {}

static int Failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++Failures; } } while (0)

static bool ParseFile(const char* Path, FOSCScenario& Out, TArray<FString>& Msgs)
{
	FString Xml;
	if (!FFileHelper::LoadFileToString(Xml, Path)) { return false; }
	FOpenScenarioParser P;
	return P.Parse(Xml, FPaths::GetPath(FString(Path)), TMap<FString, FString>(), Out, Msgs);
}

static double RunToEnd(const FString& Xml, const FString& SourceFile)
{
	UOpenScenarioAsset* Asset = new UOpenScenarioAsset();
	Asset->SetSource(Xml, SourceFile);
	UOpenScenarioRunner* R = new UOpenScenarioRunner();
	R->bSpawnActors = false;
	R->Dynamics.Mode = EOpenScenarioDynamicsMode::Kinematic;
	if (!R->Initialize(nullptr, Asset)) { return -1.0; }
	for (int i = 0; i < 6000 && R->IsRunning(); ++i) { R->Step(0.02); }
	return R->IsFinished() ? R->GetSimulationTime() : -2.0;
}

// Walks the whole tree and checks that every node resolves.
static int VisitTree(FOSCScenario& S, const FOSCNodeRef& N, int Depth = 0)
{
	int Count = 1;
	const FString D = FOSCModelEdit::Describe(S, N);
	CHECK(!D.StartsWith("(invalid)"));
	const bool bNeedsData = N.Type != EOSCNodeType::Entities && N.Type != EOSCNodeType::Init;
	if (bNeedsData) { CHECK(FOSCModelEdit::GetData(S, N) != nullptr); }
	TArray<FOSCNodeRef> Kids;
	FOSCModelEdit::GetChildren(S, N, Kids);
	for (const FOSCNodeRef& K : Kids)
	{
		FOSCNodeRef P;
		CHECK(FOSCModelEdit::GetParent(K, P));
		CHECK(P == N);
		Count += VisitTree(S, K, Depth + 1);
	}
	return Count;
}

int main(int argc, char** argv)
{
	const FString Dir = argc > 1 ? FString(argv[1]) : FString(".");

	for (const char* File : { "JunctionRouting.xosc", "TrajectoryAndEvents.xosc" })
	{
		std::printf("== round trip %s\n", File);
		const FString Path = Dir / File;
		FOSCScenario A, B, C;
		TArray<FString> MA, MB, MC;
		CHECK(ParseFile(*Path, A, MA));
		const FString Xml1 = FOpenScenarioWriter::Write(A);
		FOpenScenarioParser P1;
		CHECK(P1.Parse(Xml1, Dir, TMap<FString, FString>(), B, MB));
		CHECK(MB.Num() == 0);
		const FString Xml2 = FOpenScenarioWriter::Write(B);
		CHECK(Xml1 == Xml2); // writing is idempotent
		CHECK(A.Entities.Num() == B.Entities.Num());
		CHECK(A.Stories.Num() == B.Stories.Num());
		CHECK(A.InitActions.Num() == B.InitActions.Num());
		CHECK(A.RoadNetworkFile == B.RoadNetworkFile);

		const double T1 = RunToEnd(FString(Xml1), Path);
		UOpenScenarioAsset Orig;
		FString OrigXml; FFileHelper::LoadFileToString(OrigXml, *Path);
		const double T0 = RunToEnd(OrigXml, Path);
		std::printf("   original ends at %.2f s, rewritten at %.2f s\n", T0, T1);
		CHECK(T0 > 0 && std::fabs(T0 - T1) < 0.05);
	}

	for (const char* File : { "PopulatedWorld.xosc", "TrafficSourceSink.xosc" })
	{
		std::printf("== round trip (traffic) %s\n", File);
		FOSCScenario A, B;
		TArray<FString> MA, MB;
		CHECK(ParseFile(*(Dir / File), A, MA));
		CHECK(MA.Num() == 0);
		const FString Xml1 = FOpenScenarioWriter::Write(A);
		FOpenScenarioParser P1;
		CHECK(P1.Parse(Xml1, Dir, TMap<FString, FString>(), B, MB));
		CHECK(FOpenScenarioWriter::Write(B) == Xml1);
		int32 TrafficA = 0, TrafficB = 0;
		for (const FOSCInitActions& G : A.InitActions) { for (const FOSCAction& X : G.Actions) { TrafficA += X.Type == EOSCActionType::Traffic; } }
		for (const FOSCInitActions& G : B.InitActions) { for (const FOSCAction& X : G.Actions) { TrafficB += X.Type == EOSCActionType::Traffic; } }
		CHECK(TrafficA >= 1 && TrafficA == TrafficB);
	}

	std::printf("== edit operations\n");
	FOSCScenario S;
	TArray<FString> Msgs;
	CHECK(ParseFile(*(Dir / "JunctionRouting.xosc"), S, Msgs));
	const int Before = VisitTree(S, FOSCNodeRef(EOSCNodeType::Storyboard));
	std::printf("   tree nodes before: %d\n", Before);

	FOSCNodeRef New, Root(EOSCNodeType::Storyboard);
	CHECK(FOSCModelEdit::AddChild(S, FOSCNodeRef(EOSCNodeType::Entities), EOSCNodeType::Entity, New));
	CHECK(S.Entities.Num() == 3 && New.Path[0] == 2);
	CHECK(S.FindEntity(S.Entities[2].Name) != nullptr);

	CHECK(FOSCModelEdit::AddChild(S, Root, EOSCNodeType::Story, New));
	FOSCNodeRef Story = New;
	CHECK(FOSCModelEdit::AddChild(S, Story, EOSCNodeType::Act, New));
	FOSCNodeRef Act = New;
	CHECK(FOSCModelEdit::AddChild(S, Act, EOSCNodeType::ManeuverGroup, New));
	FOSCNodeRef MG = New;
	CHECK(FOSCModelEdit::AddChild(S, MG, EOSCNodeType::Maneuver, New));
	FOSCNodeRef Man = New;
	CHECK(FOSCModelEdit::AddChild(S, Man, EOSCNodeType::Event, New));
	FOSCNodeRef Ev = New;
	CHECK(FOSCModelEdit::AddChild(S, Ev, EOSCNodeType::Action, New));
	FOSCNodeRef Action = New;
	CHECK(S.Stories.Last().Acts[0].ManeuverGroups[0].Maneuvers[0].Events[0].Actions.Num() == 2);

	// Stop trigger of the new act: added through its Trigger node.
	TArray<FOSCNodeRef> ActKids;
	FOSCModelEdit::GetChildren(S, Act, ActKids);
	CHECK(ActKids.Num() == 3 && ActKids[1].Owner == EOSCTriggerOwner::ActStop);
	CHECK(!S.Stories.Last().Acts[0].StopTrigger.bPresent);
	FOSCNodeRef Group, Cond;
	CHECK(FOSCModelEdit::AddChild(S, ActKids[1], EOSCNodeType::ConditionGroup, Group));
	CHECK(S.Stories.Last().Acts[0].StopTrigger.bPresent && S.Stories.Last().Acts[0].StopTrigger.Groups.Num() == 1);
	CHECK(FOSCModelEdit::AddChild(S, Group, EOSCNodeType::Condition, Cond));
	CHECK(S.Stories.Last().Acts[0].StopTrigger.Groups[0].Conditions.Num() == 2);
	FOSCCondition* CondData = static_cast<FOSCCondition*>(FOSCModelEdit::GetData(S, Cond));
	CHECK(CondData != nullptr);
	if (CondData) { CondData->Value = 42.0; }

	// Move / duplicate / remove
	FOSCNodeRef Moved;
	CHECK(FOSCModelEdit::Move(S, Action, -1, Moved));
	CHECK(Moved.Path.Last() == 0);
	CHECK(!FOSCModelEdit::Move(S, Moved, -1, Moved));
	FOSCNodeRef Dup;
	CHECK(FOSCModelEdit::Duplicate(S, Moved, Dup));
	CHECK(S.Stories.Last().Acts[0].ManeuverGroups[0].Maneuvers[0].Events[0].Actions.Num() == 3);
	CHECK(FOSCModelEdit::Remove(S, Dup));
	CHECK(FOSCModelEdit::Remove(S, Cond));
	CHECK(FOSCModelEdit::Remove(S, ActKids[1])); // clears the stop trigger
	CHECK(!S.Stories.Last().Acts[0].StopTrigger.bPresent);
	CHECK(!FOSCModelEdit::Remove(S, Root));
	CHECK(!FOSCModelEdit::AddChild(S, Root, EOSCNodeType::Action, New)); // not a valid child type

	// Init actions
	CHECK(FOSCModelEdit::AddChild(S, FOSCNodeRef(EOSCNodeType::Init), EOSCNodeType::InitGroup, New));
	CHECK(S.InitActions.Last().EntityRef == S.Entities[2].Name); // first entity without init actions
	CHECK(FOSCModelEdit::AddChild(S, New, EOSCNodeType::InitAction, New));

	const int After = VisitTree(S, Root);
	std::printf("   tree nodes after: %d\n", After);
	CHECK(After > Before);

	// Validation catches dangling references.
	TArray<FString> Issues;
	FOSCModelEdit::Validate(S, Issues);
	const int BaseIssues = Issues.Num();
	S.Entities[0].Name = TEXT("Renamed");
	FOSCModelEdit::Validate(S, Issues);
	std::printf("   validation issues: %d -> %d after renaming an entity\n", BaseIssues, Issues.Num());
	CHECK(Issues.Num() > BaseIssues);

	// The edited model still serialises and parses.
	const FString Edited = FOpenScenarioWriter::Write(S);
	FOSCScenario Reparsed;
	TArray<FString> RM;
	FOpenScenarioParser P;
	CHECK(P.Parse(Edited, Dir, TMap<FString, FString>(), Reparsed, RM));
	CHECK(Reparsed.Stories.Num() == S.Stories.Num());
	CHECK(Reparsed.Entities.Num() == 3);

	std::printf(Failures ? "%d FAILURES\n" : "edit tests passed\n", Failures);
	return Failures ? 1 : 0;
}
