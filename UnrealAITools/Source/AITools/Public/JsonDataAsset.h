#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/PropertyBag.h"
#include "JsonDataAsset.generated.h"

/**
 * Generic data asset whose shape is entirely defined by an FInstancedPropertyBag
 * instead of a hand-written UCLASS. Fields are added/removed/set at sync time from
 * a JSON description (see docs/tools/json-data-assets.md in the toolkit repo) so new
 * "shapes" of data never require a C++ recompile.
 */
UCLASS(BlueprintType)
class AITOOLS_API UJsonDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Descriptive label from the JSON's "assetType" field. Not a schema reference. */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "AI Tools")
	FString AssetType;

	/** The actual data, editable natively in the Details panel like any other reflected properties. */
	UPROPERTY(EditAnywhere, Category = "AI Tools", meta = (FixedLayout))
	FInstancedPropertyBag PropertyBag;
};
