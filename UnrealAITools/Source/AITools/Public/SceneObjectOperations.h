#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Creates/removes/lists placed static mesh actors in the currently active world (editor or
 * PIE), identified by display name -- the same name shown in the World Outliner, so this
 * works uniformly on actors this tool creates and ones already placed by hand. See
 * TransformOperations.h for moving/rotating/scaling an existing one.
 */
class AITOOLS_API FAIToolsSceneObjectOps
{
public:
	/** Upsert: spawns a new static mesh actor named Name if none exists, else updates the
	 * existing one's mesh and transform in place. MeshPath defaults to the engine's basic cube. */
	static bool CreateObject(const FString& Name, const FString& MeshPath, const FVector& Location, const FRotator& Rotation, const FVector& Scale, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Removes the named actor from the currently active world. */
	static bool RemoveObject(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Lists every actor in the currently active world (observer cameras excluded) -- name,
	 * class, mesh (if any), and transform. */
	static bool ListObjects(TSharedRef<FJsonObject>& OutResult, FString& OutError);
};
