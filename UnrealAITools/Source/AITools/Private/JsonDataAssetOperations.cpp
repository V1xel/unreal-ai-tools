#include "JsonDataAssetOperations.h"

#include "AssetPathUtils.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonValue.h"
#include "JsonDataAsset.h"
#include "Misc/PackageName.h"
#include "PropertyBagJsonUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

bool FAIToolsJsonDataAssetOps::SyncJsonDataAsset(const FString& AssetPath, const TSharedRef<FJsonObject>& RequestJson, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	FString PackageName, AssetName;
	if (!AITools::SplitAssetPath(AssetPath, PackageName, AssetName, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* FieldsObjPtr = nullptr;
	if (!RequestJson->TryGetObjectField(TEXT("fields"), FieldsObjPtr) || !FieldsObjPtr->IsValid())
	{
		OutError = TEXT("request is missing required \"fields\" object");
		return false;
	}

	FString AssetType;
	RequestJson->TryGetStringField(TEXT("assetType"), AssetType);

	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	UJsonDataAsset* Asset = LoadObject<UJsonDataAsset>(nullptr, *ObjectPath);
	bool bIsNew = false;
	UPackage* Package = nullptr;
	if (Asset)
	{
		Package = Asset->GetOutermost();
	}
	else
	{
		Package = CreatePackage(*PackageName);
		Asset = NewObject<UJsonDataAsset>(Package, UJsonDataAsset::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);
		bIsNew = true;
	}

	FString SyncError;
	if (!FAIToolsPropertyBagJson::SyncFieldsFromJson(Asset->PropertyBag, FieldsObjPtr->ToSharedRef(), Asset, SyncError))
	{
		OutError = SyncError;
		return false;
	}
	Asset->AssetType = AssetType;

	if (bIsNew)
	{
		FAssetRegistryModule::AssetCreated(Asset);
	}
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("failed to save package '%s'"), *PackageName);
		return false;
	}

	OutResult->SetStringField(TEXT("path"), AssetPath);
	OutResult->SetBoolField(TEXT("created"), bIsNew);
	OutResult->SetStringField(TEXT("assetType"), Asset->AssetType);
	OutResult->SetObjectField(TEXT("fields"), FAIToolsPropertyBagJson::FieldsToJson(Asset->PropertyBag));
	return true;
}

bool FAIToolsJsonDataAssetOps::ReadDataAssetAsJson(const FString& AssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	FString PackageName, AssetName;
	if (!AITools::SplitAssetPath(AssetPath, PackageName, AssetName, OutError))
	{
		return false;
	}

	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	UJsonDataAsset* Asset = LoadObject<UJsonDataAsset>(nullptr, *ObjectPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("no JSON data asset found at '%s'"), *AssetPath);
		return false;
	}

	OutResult->SetStringField(TEXT("path"), AssetPath);
	OutResult->SetStringField(TEXT("assetType"), Asset->AssetType);
	OutResult->SetObjectField(TEXT("fields"), FAIToolsPropertyBagJson::FieldsToJson(Asset->PropertyBag));
	return true;
}

TSharedRef<FJsonObject> FAIToolsJsonDataAssetOps::AssetDataToEntryJson(const FAssetData& AssetData)
{
	FString ThisAssetType;
	AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UJsonDataAsset, AssetType), ThisAssetType);

	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
	Entry->SetStringField(TEXT("assetType"), ThisAssetType);
	return Entry;
}

bool FAIToolsJsonDataAssetOps::ListDataAssets(const FString& Folder, const FString& AssetType, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UJsonDataAsset::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	if (!Folder.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDatas;
	AssetRegistry.GetAssets(Filter, AssetDatas);

	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(AssetDatas.Num());
	for (const FAssetData& AssetData : AssetDatas)
	{
		FString ThisAssetType;
		AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UJsonDataAsset, AssetType), ThisAssetType);

		if (!AssetType.IsEmpty() && ThisAssetType != AssetType)
		{
			continue;
		}

		Entries.Add(MakeShared<FJsonValueObject>(AssetDataToEntryJson(AssetData)));
	}

	OutResult->SetArrayField(TEXT("assets"), Entries);
	return true;
}

bool FAIToolsJsonDataAssetOps::FindReferencingDataAssets(const FString& AssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	FString PackageName, AssetName;
	if (!AITools::SplitAssetPath(AssetPath, PackageName, AssetName, OutError))
	{
		return false;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Indexed reverse-dependency lookup -- covers both hard and soft references (package
	// category, the default), without opening or iterating any other asset.
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(FName(*PackageName), Referencers);

	const FTopLevelAssetPath JsonDataAssetClassPath = UJsonDataAsset::StaticClass()->GetClassPathName();

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FName& ReferencerPackageName : Referencers)
	{
		TArray<FAssetData> AssetDatas;
		AssetRegistry.GetAssetsByPackageName(ReferencerPackageName, AssetDatas);
		for (const FAssetData& AssetData : AssetDatas)
		{
			if (AssetData.AssetClassPath == JsonDataAssetClassPath)
			{
				Entries.Add(MakeShared<FJsonValueObject>(AssetDataToEntryJson(AssetData)));
			}
		}
	}

	OutResult->SetStringField(TEXT("path"), AssetPath);
	OutResult->SetArrayField(TEXT("referencedBy"), Entries);
	return true;
}

bool FAIToolsJsonDataAssetOps::ValidateJsonSchema(const TSharedRef<FJsonObject>& RequestJson, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	const TSharedPtr<FJsonObject>* FieldsObjPtr = nullptr;
	if (!RequestJson->TryGetObjectField(TEXT("fields"), FieldsObjPtr) || !FieldsObjPtr->IsValid())
	{
		OutError = TEXT("request is missing required \"fields\" object");
		return false;
	}

	FString ValidateError;
	const bool bValid = FAIToolsPropertyBagJson::ValidateFieldsJson(FieldsObjPtr->ToSharedRef(), ValidateError);
	OutResult->SetBoolField(TEXT("valid"), bValid);
	if (!bValid)
	{
		OutResult->SetStringField(TEXT("error"), ValidateError);
	}
	return true;
}

TSharedRef<FJsonObject> FAIToolsJsonDataAssetOps::ListSupportedTypes()
{
	return FAIToolsPropertyBagJson::GetSupportedTypesJson();
}
