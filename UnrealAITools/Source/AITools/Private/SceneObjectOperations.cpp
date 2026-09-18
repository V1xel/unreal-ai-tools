#include "SceneObjectOperations.h"

#include "AIToolsObserverCamera.h"
#include "AIToolsWorldUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* DefaultMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
}

bool FAIToolsSceneObjectOps::CreateObject(const FString& Name, const FString& MeshPath, const FVector& Location, const FRotator& Rotation, const FVector& Scale, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	if (Name.IsEmpty())
	{
		OutError = TEXT("object name must not be empty");
		return false;
	}

	UWorld* World = AITools::GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	const FString ResolvedMeshPath = MeshPath.IsEmpty() ? FString(DefaultMeshPath) : MeshPath;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ResolvedMeshPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("could not load static mesh '%s'"), *ResolvedMeshPath);
		return false;
	}

	AActor* ExistingActor = AITools::FindActorByName(World, Name);
	if (ExistingActor && !ExistingActor->IsA<AStaticMeshActor>())
	{
		OutError = FString::Printf(TEXT("'%s' already exists and is not a static mesh actor"), *Name);
		return false;
	}

	AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(ExistingActor);
	bool bIsNew = false;

	if (!MeshActor)
	{
		MeshActor = World->SpawnActor<AStaticMeshActor>(Location, Rotation);
		if (!MeshActor)
		{
			OutError = FString::Printf(TEXT("failed to spawn object '%s'"), *Name);
			return false;
		}
		MeshActor->SetActorLabel(Name);
		bIsNew = true;
	}
	else
	{
		if (USceneComponent* Root = MeshActor->GetRootComponent())
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
		MeshActor->SetActorLocationAndRotation(Location, Rotation);
	}

	MeshActor->SetActorScale3D(Scale);
	if (UStaticMeshComponent* Comp = MeshActor->GetStaticMeshComponent())
	{
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
	}

	OutResult->SetStringField(TEXT("name"), Name);
	OutResult->SetBoolField(TEXT("created"), bIsNew);
	return true;
}

bool FAIToolsSceneObjectOps::RemoveObject(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	UWorld* World = AITools::GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	AActor* Actor = AITools::FindActorByName(World, Name);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("no object named '%s'"), *Name);
		return false;
	}

	World->DestroyActor(Actor);
	OutResult->SetStringField(TEXT("name"), Name);
	return true;
}

bool FAIToolsSceneObjectOps::ListObjects(TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	UWorld* World = AITools::GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<AAIToolsObserverCamera>())
		{
			continue;
		}

		const FVector Loc = Actor->GetActorLocation();
		const FRotator Rot = Actor->GetActorRotation();
		const FVector Scale = Actor->GetActorScale3D();

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Actor->GetActorNameOrLabel());
		Entry->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		if (const UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>())
		{
			if (const UStaticMesh* Mesh = MeshComp->GetStaticMesh())
			{
				Entry->SetStringField(TEXT("mesh"), Mesh->GetPathName());
			}
		}
		Entry->SetNumberField(TEXT("x"), Loc.X);
		Entry->SetNumberField(TEXT("y"), Loc.Y);
		Entry->SetNumberField(TEXT("z"), Loc.Z);
		Entry->SetNumberField(TEXT("pitch"), Rot.Pitch);
		Entry->SetNumberField(TEXT("yaw"), Rot.Yaw);
		Entry->SetNumberField(TEXT("roll"), Rot.Roll);
		Entry->SetNumberField(TEXT("scaleX"), Scale.X);
		Entry->SetNumberField(TEXT("scaleY"), Scale.Y);
		Entry->SetNumberField(TEXT("scaleZ"), Scale.Z);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	OutResult->SetArrayField(TEXT("objects"), Entries);
	return true;
}
