#include "AIToolsWorldUtils.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UWorld* AITools::GetActiveWorld()
{
	if (!GEditor)
	{
		return nullptr;
	}
	if (GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}
	return GEditor->GetEditorWorldContext().World();
}

AActor* AITools::FindActorByName(UWorld* World, const FString& Name)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorNameOrLabel() == Name)
		{
			return *It;
		}
	}
	return nullptr;
}
