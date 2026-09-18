#pragma once

#include "CoreMinimal.h"

namespace AITools
{
	/** Splits a "/Game/..." asset path into its package name and short asset name, validating it. */
	AITOOLS_API bool SplitAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutError);
}
