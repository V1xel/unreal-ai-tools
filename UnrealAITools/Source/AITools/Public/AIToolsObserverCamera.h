#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"

#include "AIToolsObserverCamera.generated.h"

/**
 * A placeable viewpoint for the observer tool (see ObserverOperations.h): plain ACameraActor
 * plus a stable name. Actor labels are editor-only, so ObserverName is what HTTP callers use
 * to find/update/delete/describe a specific observer, both in the editor and in PIE.
 */
UCLASS(Blueprintable)
class AITOOLS_API AAIToolsObserverCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "AI Tools")
	FString ObserverName;
};
