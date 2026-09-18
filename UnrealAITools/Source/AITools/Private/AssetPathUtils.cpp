#include "AssetPathUtils.h"

#include "Misc/PackageName.h"

bool AITools::SplitAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutError)
{
	if (AssetPath.IsEmpty() || !AssetPath.StartsWith(TEXT("/")))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid asset path (expected e.g. /Game/Data/Items/HealthPotion)"), *AssetPath);
		return false;
	}

	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		OutError = FString::Printf(TEXT("'%s' is not a valid long package name"), *AssetPath);
		return false;
	}

	OutPackageName = AssetPath;
	OutAssetName = FPackageName::GetShortName(AssetPath);
	return true;
}
