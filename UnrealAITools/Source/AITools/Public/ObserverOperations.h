#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Places named camera viewpoints in the level and reports what each one "sees" as compact
 * JSON (name/distance/angle/screen bounding box per object) instead of pixels -- cheap for an
 * LLM to consume, no vision model needed. Works both while editing and during Play-In-Editor:
 * observers are normal placed actors, so they carry over into PIE with the level, and every
 * operation resolves whichever world (editor or PIE) is currently active.
 */
class AITOOLS_API FAIToolsObserverOps
{
public:
	/** Upsert: creates the named observer if missing, else moves the existing one. */
	static bool PlaceObserver(const FString& Name, const FVector& Location, const FRotator& Rotation, float FieldOfView, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Lists observers placed in the currently active world. */
	static bool ListObservers(TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Removes the named observer from the currently active world. */
	static bool DeleteObserver(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/**
	 * Reports what the named observer currently sees: meshed actors inside its view frustum
	 * with an unobstructed line of sight, each as {"n": name, "d": distance (m),
	 * "a": signed horizontal angle from center (deg), "box": [x, y, w, h] normalized
	 * screen-space bounding box}. Name prefers a referencing JSON data asset's DisplayName.
	 */
	static bool DescribeObserver(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError);
};
