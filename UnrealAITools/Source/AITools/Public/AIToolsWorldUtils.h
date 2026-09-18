#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace AITools
{
	/**
	 * Whichever world is currently active: the PIE world if a Play session is running, else the
	 * editor world. Every scene query/edit resolves this so it works both while editing and
	 * during Play-In-Editor, per docs/architecture.md.
	 */
	AITOOLS_API UWorld* GetActiveWorld();

	/**
	 * Finds any actor in World by its display name -- the same name shown in the World
	 * Outliner (GetActorNameOrLabel: the editor label if set, else the internal object name).
	 * Works uniformly for actors placed by hand and ones created via the scene-object tools.
	 */
	AITOOLS_API AActor* FindActorByName(UWorld* World, const FString& Name);
}
