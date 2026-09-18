#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * The five JSON-data-asset operations exposed over HTTP (see docs/tools/json-data-assets.md
 * in the toolkit repo). Each one is plain C++ so it is equally callable from a route handler,
 * a commandlet, or (later) an automated test -- the HTTP layer is a thin wrapper around these.
 */
class AITOOLS_API FAIToolsJsonDataAssetOps
{
public:
	/** Upsert: create the asset at AssetPath if missing, else reconcile its fields in place. Saves the package. */
	static bool SyncJsonDataAsset(const FString& AssetPath, const TSharedRef<FJsonObject>& RequestJson, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Reads an existing JSON-backed data asset back out as JSON. */
	static bool ReadDataAssetAsJson(const FString& AssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Enumerates existing JSON-backed data assets, optionally filtered by folder and/or assetType. */
	static bool ListDataAssets(const FString& Folder, const FString& AssetType, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Dry-run type/shape check of a sync request's JSON without touching any asset. */
	static bool ValidateJsonSchema(const TSharedRef<FJsonObject>& RequestJson, TSharedRef<FJsonObject>& OutResult, FString& OutError);

	/** Allowed "type" values and supported engine struct types. */
	static TSharedRef<FJsonObject> ListSupportedTypes();

	/**
	 * Finds JSON data assets whose fields reference AssetPath (object/softObject/class/softClass,
	 * hard or soft), via the asset registry's indexed dependency graph -- no per-asset iteration.
	 * Useful before moving/renaming/deleting AssetPath to see what would break.
	 */
	static bool FindReferencingDataAssets(const FString& AssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError);

private:
	static TSharedRef<FJsonObject> AssetDataToEntryJson(const struct FAssetData& AssetData);
};
