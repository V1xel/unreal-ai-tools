#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Moves/rotates/scales an existing actor in the currently active world (editor or PIE),
 * identified by display name (see SceneObjectOperations.h for creating/removing one). Any
 * unset optional is left unchanged, so a caller can move without touching rotation/scale.
 */
class AITOOLS_API FAIToolsTransformOps
{
public:
	static bool SetTransform(const FString& Name, const TOptional<FVector>& Location, const TOptional<FRotator>& Rotation, const TOptional<FVector>& Scale, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	static bool GetTransform(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError);
};
