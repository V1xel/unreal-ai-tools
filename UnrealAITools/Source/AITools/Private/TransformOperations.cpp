#include "TransformOperations.h"

#include "AIToolsWorldUtils.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	void PopulateTransformJson(TSharedRef<FJsonObject>& OutResult, const FString& Name, const AActor* Actor)
	{
		const FVector Loc = Actor->GetActorLocation();
		const FRotator Rot = Actor->GetActorRotation();
		const FVector Scale = Actor->GetActorScale3D();

		OutResult->SetStringField(TEXT("name"), Name);
		OutResult->SetNumberField(TEXT("x"), Loc.X);
		OutResult->SetNumberField(TEXT("y"), Loc.Y);
		OutResult->SetNumberField(TEXT("z"), Loc.Z);
		OutResult->SetNumberField(TEXT("pitch"), Rot.Pitch);
		OutResult->SetNumberField(TEXT("yaw"), Rot.Yaw);
		OutResult->SetNumberField(TEXT("roll"), Rot.Roll);
		OutResult->SetNumberField(TEXT("scaleX"), Scale.X);
		OutResult->SetNumberField(TEXT("scaleY"), Scale.Y);
		OutResult->SetNumberField(TEXT("scaleZ"), Scale.Z);
	}
}

bool FAIToolsTransformOps::SetTransform(const FString& Name, const TOptional<FVector>& Location, const TOptional<FRotator>& Rotation, const TOptional<FVector>& Scale, TSharedRef<FJsonObject>& OutResult, FString& OutError)
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

	// A hand-placed actor may default to Static mobility, which silently refuses to move --
	// force it Movable so this always takes effect regardless of how the actor was created.
	if (USceneComponent* Root = Actor->GetRootComponent())
	{
		Root->SetMobility(EComponentMobility::Movable);
	}

	const FVector NewLocation = Location.Get(Actor->GetActorLocation());
	const FRotator NewRotation = Rotation.Get(Actor->GetActorRotation());
	Actor->SetActorLocationAndRotation(NewLocation, NewRotation);

	if (Scale.IsSet())
	{
		Actor->SetActorScale3D(Scale.GetValue());
	}

	PopulateTransformJson(OutResult, Name, Actor);
	return true;
}

bool FAIToolsTransformOps::GetTransform(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError)
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

	PopulateTransformJson(OutResult, Name, Actor);
	return true;
}
