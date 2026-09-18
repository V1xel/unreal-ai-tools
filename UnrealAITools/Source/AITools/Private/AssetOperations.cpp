#include "AssetOperations.h"

#include "AssetPathUtils.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Dom/JsonValue.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/UObjectGlobals.h"

bool FAIToolsAssetOps::MoveAsset(const FString& OldAssetPath, const FString& NewAssetPath, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	FString OldPackageName, OldAssetName;
	if (!AITools::SplitAssetPath(OldAssetPath, OldPackageName, OldAssetName, OutError))
	{
		return false;
	}

	FString NewPackageName, NewAssetName;
	if (!AITools::SplitAssetPath(NewAssetPath, NewPackageName, NewAssetName, OutError))
	{
		return false;
	}

	const FString OldObjectPath = OldPackageName + TEXT(".") + OldAssetName;
	UObject* Asset = LoadObject<UObject>(nullptr, *OldObjectPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("no asset found at '%s'"), *OldAssetPath);
		return false;
	}

	const FString NewPackagePath = FPackageName::GetLongPackagePath(NewPackageName);

	// IAssetTools::RenameAssets fixes up every hard/soft reference to Asset project-wide --
	// unlike a raw file move, nothing that points at it breaks.
	TArray<FAssetRenameData> RenameDataArray;
	RenameDataArray.Add(FAssetRenameData(TWeakObjectPtr<UObject>(Asset), NewPackagePath, NewAssetName));

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	if (!AssetTools.RenameAssets(RenameDataArray))
	{
		OutError = FString::Printf(TEXT("failed to move/rename '%s' to '%s' -- check the Output Log for details"), *OldAssetPath, *NewAssetPath);
		return false;
	}

	OutResult->SetStringField(TEXT("oldPath"), OldAssetPath);
	OutResult->SetStringField(TEXT("newPath"), NewAssetPath);
	return true;
}

bool FAIToolsAssetOps::DeleteAsset(const FString& AssetPath, bool bForce, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	FString PackageName, AssetName;
	if (!AITools::SplitAssetPath(AssetPath, PackageName, AssetName, OutError))
	{
		return false;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetDatas;
	AssetRegistry.GetAssetsByPackageName(FName(*PackageName), AssetDatas);
	if (AssetDatas.Num() == 0)
	{
		OutError = FString::Printf(TEXT("no asset found at '%s'"), *AssetPath);
		return false;
	}

	if (!bForce)
	{
		// A rename can be fixed up automatically; a delete cannot. Refuse rather than
		// silently leave every referencer with a dangling reference.
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(FName(*PackageName), Referencers);
		if (Referencers.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ReferencerJson;
			for (const FName& Referencer : Referencers)
			{
				ReferencerJson.Add(MakeShared<FJsonValueString>(Referencer.ToString()));
			}
			OutResult->SetArrayField(TEXT("referencedBy"), ReferencerJson);
			OutError = FString::Printf(TEXT("'%s' is referenced by %d other package(s); pass force=true to delete anyway"), *AssetPath, Referencers.Num());
			return false;
		}
	}

	const int32 NumDeleted = ObjectTools::DeleteAssets(AssetDatas, /*bShowConfirmation=*/false);
	if (NumDeleted == 0)
	{
		OutError = FString::Printf(TEXT("failed to delete '%s'"), *AssetPath);
		return false;
	}

	OutResult->SetStringField(TEXT("path"), AssetPath);
	OutResult->SetNumberField(TEXT("deletedObjectCount"), NumDeleted);
	return true;
}

bool FAIToolsAssetOps::ListAssets(const FString& Folder, const FString& ClassName, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	if (!Folder.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
	}
	if (!ClassName.IsEmpty())
	{
		const UClass* Class = FindFirstObjectSafe<UClass>(*ClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("AITools asset list class filter"));
		if (!Class)
		{
			OutError = FString::Printf(TEXT("class '%s' not found"), *ClassName);
			return false;
		}
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.bRecursiveClasses = true;
	}

	TArray<FAssetData> AssetDatas;
	AssetRegistry.GetAssets(Filter, AssetDatas);

	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(AssetDatas.Num());
	for (const FAssetData& AssetData : AssetDatas)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
		Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	OutResult->SetArrayField(TEXT("assets"), Entries);
	return true;
}
