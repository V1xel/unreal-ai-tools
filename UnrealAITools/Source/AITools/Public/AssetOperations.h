#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Generic asset operations -- these work on any Unreal asset, not just the JSON-defined
 * data assets in JsonDataAssetOperations.h. Exposed under the /asset/ HTTP namespace.
 */
class AITOOLS_API FAIToolsAssetOps
{
public:
	/**
	 * Moves and/or renames the asset at OldAssetPath to NewAssetPath, fixing up every
	 * reference to it project-wide (via IAssetTools::RenameAssets, the same mechanism the
	 * Content Browser's own Move/Rename uses) -- unlike a raw file move, nothing breaks.
	 */
	static bool MoveAsset(const FString& OldAssetPath, const FString& NewAssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/**
	 * Deletes the asset at AssetPath. Unlike a rename, a delete cannot be fixed up: if
	 * anything still references AssetPath, this refuses and reports the referencers unless
	 * bForce is set.
	 */
	static bool DeleteAsset(const FString& AssetPath, bool bForce, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/**
	 * Lists any registered Unreal asset (StaticMesh, Texture, Material, JSON data assets,
	 * anything) -- not just placed scene objects (see SceneObjectOperations.h) or JSON data
	 * assets specifically (see JsonDataAssetOperations.h). Optionally filtered by folder
	 * and/or a class name (e.g. "StaticMesh"); either left empty means unfiltered.
	 */
	static bool ListAssets(const FString& Folder, const FString& ClassName, TSharedRef<FJsonObject>& OutResult, FString& OutError);
};
