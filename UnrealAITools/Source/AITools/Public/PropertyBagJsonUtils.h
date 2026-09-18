#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"
#include "Dom/JsonObject.h"

/**
 * Bridges the JSON "fields" format (see docs/tools/json-data-assets.md) and
 * FInstancedPropertyBag. Owns the only place that knows how a JSON field
 * description maps onto EPropertyBagPropertyType.
 *
 * Field JSON shape: { "<Name>": { "type": "...", "value": ..., ["elementType": "...",]
 *                                 ["structType": "..."], ["objectClass": "..."],
 *                                 ["enumValues": [...]], ["category": "..."] }, ... }
 */
class AITOOLS_API FAIToolsPropertyBagJson
{
public:
	/**
	 * Reconciles Bag's properties to exactly match FieldsJson: new fields are added with
	 * their JSON value, fields no longer present are removed, matching fields are kept and
	 * revalued. Enum fields own a dynamically-created UEnum (outered to Outer) so the field's
	 * allowed values are exactly the JSON's "enumValues" list.
	 */
	static bool SyncFieldsFromJson(FInstancedPropertyBag& Bag, const TSharedRef<FJsonObject>& FieldsJson, UObject* Outer, FString& OutError);

	/** Reads Bag back out as a "fields" JSON object in the same shape SyncFieldsFromJson consumes. */
	static TSharedRef<FJsonObject> FieldsToJson(const FInstancedPropertyBag& Bag);

	/** Dry-run shape/type check of a "fields" JSON object. Does not touch any bag. */
	static bool ValidateFieldsJson(const TSharedRef<FJsonObject>& FieldsJson, FString& OutError);

	/** Payload for GET /data-asset/types: allowed "type" values and supported struct type names. */
	static TSharedRef<FJsonObject> GetSupportedTypesJson();

private:
	struct FParsedFieldDesc
	{
		FName Name;
		EPropertyBagPropertyType ValueType = EPropertyBagPropertyType::None;
		bool bIsArray = false;
		const UObject* ValueTypeObject = nullptr; // UScriptStruct / UClass / UEnum, depending on ValueType
		TSharedPtr<FJsonValue> Value; // may be null -> default value
		FString Category; // editor-only Details panel grouping, empty -> none
	};

	static bool ParseFieldDesc(const FName FieldName, const TSharedPtr<FJsonObject>& FieldJson, UObject* Outer, FParsedFieldDesc& OutDesc, FString& OutError);
	static bool TryParseScalarType(const FString& TypeStr, EPropertyBagPropertyType& OutType, FString& OutError);
	static const UScriptStruct* FindSupportedStruct(const FString& StructTypeName);
	static UEnum* CreateOrUpdateEnum(UObject* Outer, FName FieldName, const TArray<FString>& EnumValues, FString& OutError);

	static bool SetScalarOnBag(FInstancedPropertyBag& Bag, FName Name, EPropertyBagPropertyType Type, const UObject* TypeObject, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);
	static bool SetScalarOnArray(FPropertyBagArrayRef& ArrayRef, int32 Index, EPropertyBagPropertyType Type, const UObject* TypeObject, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);
	static TSharedPtr<FJsonValue> GetScalarFromBag(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, EPropertyBagPropertyType Type, const UObject* TypeObject);
	static TSharedPtr<FJsonValue> GetScalarFromArray(const FPropertyBagArrayRef& ArrayRef, int32 Index, EPropertyBagPropertyType Type, const UObject* TypeObject);

	static FString TypeToString(EPropertyBagPropertyType Type);
};
