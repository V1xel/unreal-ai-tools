#include "ObserverOperations.h"

#include "AIToolsObserverCamera.h"
#include "AIToolsWorldUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ConvexVolume.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "JsonDataAsset.h"

using AITools::GetActiveWorld;

namespace
{
	AAIToolsObserverCamera* FindObserverByName(UWorld* World, const FString& Name)
	{
		for (TActorIterator<AAIToolsObserverCamera> It(World); It; ++It)
		{
			if (It->ObserverName == Name)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Prefers the DisplayName field of a JSON data asset that references this mesh (see
	 * FAIToolsJsonDataAssetOps::FindReferencingDataAssets), falling back to the actor's own name. */
	FString ResolveObjectName(UStaticMeshComponent* MeshComp, AActor* Actor)
	{
		if (MeshComp && MeshComp->GetStaticMesh())
		{
			const FString MeshPackageName = MeshComp->GetStaticMesh()->GetOutermost()->GetName();
			IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(FName(*MeshPackageName), Referencers);

			const FTopLevelAssetPath JsonClassPath = UJsonDataAsset::StaticClass()->GetClassPathName();
			for (const FName& ReferencerPackage : Referencers)
			{
				TArray<FAssetData> AssetDatas;
				AssetRegistry.GetAssetsByPackageName(ReferencerPackage, AssetDatas);
				for (const FAssetData& AssetData : AssetDatas)
				{
					if (AssetData.AssetClassPath != JsonClassPath)
					{
						continue;
					}
					if (const UJsonDataAsset* DataAsset = Cast<UJsonDataAsset>(AssetData.GetAsset()))
					{
						auto Result = DataAsset->PropertyBag.GetValueString(TEXT("DisplayName"));
						if (Result.IsValid() && !Result.GetValue().IsEmpty())
						{
							return Result.GetValue();
						}
					}
				}
			}
		}

		return Actor->GetActorNameOrLabel();
	}
}

bool FAIToolsObserverOps::PlaceObserver(const FString& Name, const FVector& Location, const FRotator& Rotation, float FieldOfView, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	if (Name.IsEmpty())
	{
		OutError = TEXT("observer name must not be empty");
		return false;
	}

	UWorld* World = GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	AAIToolsObserverCamera* Observer = FindObserverByName(World, Name);
	bool bIsNew = false;
	if (!Observer)
	{
		Observer = World->SpawnActor<AAIToolsObserverCamera>(Location, Rotation);
		if (!Observer)
		{
			OutError = FString::Printf(TEXT("failed to spawn observer '%s'"), *Name);
			return false;
		}
		Observer->ObserverName = Name;
		bIsNew = true;
	}
	else
	{
		Observer->SetActorLocationAndRotation(Location, Rotation);
	}

	if (UCameraComponent* CameraComp = Observer->GetCameraComponent())
	{
		CameraComp->SetFieldOfView(FieldOfView);
	}

	OutResult->SetStringField(TEXT("name"), Name);
	OutResult->SetBoolField(TEXT("created"), bIsNew);
	return true;
}

bool FAIToolsObserverOps::ListObservers(TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	UWorld* World = GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (TActorIterator<AAIToolsObserverCamera> It(World); It; ++It)
	{
		AAIToolsObserverCamera* Observer = *It;
		const FVector Loc = Observer->GetActorLocation();
		const FRotator Rot = Observer->GetActorRotation();

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Observer->ObserverName);
		Entry->SetNumberField(TEXT("x"), Loc.X);
		Entry->SetNumberField(TEXT("y"), Loc.Y);
		Entry->SetNumberField(TEXT("z"), Loc.Z);
		Entry->SetNumberField(TEXT("yaw"), Rot.Yaw);
		Entry->SetNumberField(TEXT("pitch"), Rot.Pitch);
		if (const UCameraComponent* CameraComp = Observer->GetCameraComponent())
		{
			Entry->SetNumberField(TEXT("fov"), CameraComp->FieldOfView);
		}
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	OutResult->SetArrayField(TEXT("observers"), Entries);
	OutResult->SetStringField(TEXT("world"), World->GetName());
	OutResult->SetBoolField(TEXT("pie"), GEditor->PlayWorld != nullptr);
	return true;
}

bool FAIToolsObserverOps::DeleteObserver(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	UWorld* World = GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	AAIToolsObserverCamera* Observer = FindObserverByName(World, Name);
	if (!Observer)
	{
		OutError = FString::Printf(TEXT("no observer named '%s'"), *Name);
		return false;
	}

	World->DestroyActor(Observer);
	OutResult->SetStringField(TEXT("name"), Name);
	return true;
}

bool FAIToolsObserverOps::DescribeObserver(const FString& Name, TSharedRef<FJsonObject>& OutResult, FString& OutError)
{
	UWorld* World = GetActiveWorld();
	if (!World)
	{
		OutError = TEXT("no active world (editor not ready)");
		return false;
	}

	AAIToolsObserverCamera* Observer = FindObserverByName(World, Name);
	if (!Observer)
	{
		OutError = FString::Printf(TEXT("no observer named '%s'"), *Name);
		return false;
	}

	UCameraComponent* CameraComp = Observer->GetCameraComponent();
	if (!CameraComp)
	{
		OutError = TEXT("observer has no camera component");
		return false;
	}

	FMinimalViewInfo ViewInfo;
	CameraComp->GetCameraView(0.0f, ViewInfo);

	const FVector ObserverLocation = ViewInfo.Location;
	const FRotator ObserverRotation = ViewInfo.Rotation;
	const FVector Forward = ObserverRotation.Vector();
	const FVector Right = FRotationMatrix(ObserverRotation).GetUnitAxis(EAxis::Y);

	// Build the view-projection matrix from the camera's transform (no viewport/player
	// controller needed) -- see FViewMatrices::UpdateViewMatrix and FSceneView::WorldToScreen.
	const FMatrix ViewPlanesMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	const FMatrix ViewRotationMatrix = FInverseRotationMatrix(ObserverRotation) * ViewPlanesMatrix;
	const FMatrix ViewMatrix = FTranslationMatrix(-ObserverLocation) * ViewRotationMatrix;
	const FMatrix ProjMatrix = ViewInfo.CalculateProjectionMatrix();
	const FMatrix ViewProjMatrix = ViewMatrix * ProjMatrix;

	FConvexVolume Frustum;
	GetViewFrustumBounds(Frustum, ViewProjMatrix, true);

	TArray<TSharedPtr<FJsonValue>> Seen;
	TArray<TSharedPtr<FJsonValue>> Occluded;
	int32 MeshActorCount = 0;
	int32 InFrustumCount = 0;
	int32 UnoccludedCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<AAIToolsObserverCamera>())
		{
			continue;
		}

		UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (!MeshComp || !MeshComp->GetStaticMesh())
		{
			continue;
		}
		++MeshActorCount;

		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);

		if (!Frustum.IntersectBox(Origin, Extent))
		{
			continue;
		}
		++InFrustumCount;

		// A single ray to the bounding-box center is too crude for adjacent/overlapping
		// objects: the ray to one's center can clip through a neighbor even though most of
		// the object is genuinely visible. Sample the center plus all 8 corners and count
		// the object visible if any one of them has a clear line of sight.
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AIToolsObserverVisibility), true);
		TraceParams.AddIgnoredActor(Actor);
		TraceParams.AddIgnoredActor(Observer);

		TArray<FVector, TInlineAllocator<9>> SamplePoints;
		SamplePoints.Add(Origin);
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			SamplePoints.Add(Origin + FVector(
				(CornerIndex & 1) ? Extent.X : -Extent.X,
				(CornerIndex & 2) ? Extent.Y : -Extent.Y,
				(CornerIndex & 4) ? Extent.Z : -Extent.Z));
		}

		bool bAnyClear = false;
		FHitResult LastHit;
		for (const FVector& SamplePoint : SamplePoints)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, ObserverLocation, SamplePoint, ECC_Visibility, TraceParams))
			{
				bAnyClear = true;
				break;
			}
			LastHit = Hit;
		}

		if (!bAnyClear)
		{
			TSharedRef<FJsonObject> OccludedEntry = MakeShared<FJsonObject>();
			OccludedEntry->SetStringField(TEXT("n"), ResolveObjectName(MeshComp, Actor));
			OccludedEntry->SetStringField(TEXT("blockedBy"), LastHit.GetActor() ? LastHit.GetActor()->GetActorNameOrLabel() : TEXT("(unknown)"));
			OccludedEntry->SetStringField(TEXT("blockedByComponent"), LastHit.GetComponent() ? LastHit.GetComponent()->GetName() : TEXT("(none)"));
			OccludedEntry->SetNumberField(TEXT("hitDistance"), LastHit.Distance / 100.0f);
			Occluded.Add(MakeShared<FJsonValueObject>(OccludedEntry));
			continue;
		}
		++UnoccludedCount;

		// Screen-space bounding box: project all 8 world-space corners, keep the ones in
		// front of the camera, and take their min/max in normalized [0,1] screen space.
		float MinX = 1.0f, MinY = 1.0f, MaxX = 0.0f, MaxY = 0.0f;
		bool bAnyOnScreen = false;
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector Corner = Origin + FVector(
				(CornerIndex & 1) ? Extent.X : -Extent.X,
				(CornerIndex & 2) ? Extent.Y : -Extent.Y,
				(CornerIndex & 4) ? Extent.Z : -Extent.Z);

			const FVector4 ScreenPos = ViewProjMatrix.TransformFVector4(FVector4(Corner, 1.0f));
			if (ScreenPos.W <= 0.0f)
			{
				continue;
			}
			const float InvW = 1.0f / ScreenPos.W;
			const float X01 = 0.5f + ScreenPos.X * 0.5f * InvW;
			const float Y01 = 0.5f - ScreenPos.Y * 0.5f * InvW;
			MinX = FMath::Min(MinX, X01);
			MinY = FMath::Min(MinY, Y01);
			MaxX = FMath::Max(MaxX, X01);
			MaxY = FMath::Max(MaxY, Y01);
			bAnyOnScreen = true;
		}
		if (!bAnyOnScreen)
		{
			continue;
		}

		MinX = FMath::Clamp(MinX, 0.0f, 1.0f);
		MinY = FMath::Clamp(MinY, 0.0f, 1.0f);
		MaxX = FMath::Clamp(MaxX, 0.0f, 1.0f);
		MaxY = FMath::Clamp(MaxY, 0.0f, 1.0f);

		const FVector ToTarget = Origin - ObserverLocation;
		const FVector ToTargetDir = ToTarget.GetSafeNormal();
		const float DistanceMeters = ToTarget.Size() / 100.0f;
		const float SignedAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(
			FVector::DotProduct(ToTargetDir, Right), FVector::DotProduct(ToTargetDir, Forward)));

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("n"), ResolveObjectName(MeshComp, Actor));
		Entry->SetNumberField(TEXT("d"), FMath::RoundToFloat(DistanceMeters * 100.0f) / 100.0f);
		Entry->SetNumberField(TEXT("a"), FMath::RoundToFloat(SignedAngleDeg));

		TArray<TSharedPtr<FJsonValue>> Box;
		Box.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(MinX * 1000.0f) / 1000.0f));
		Box.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(MinY * 1000.0f) / 1000.0f));
		Box.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat((MaxX - MinX) * 1000.0f) / 1000.0f));
		Box.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat((MaxY - MinY) * 1000.0f) / 1000.0f));
		Entry->SetArrayField(TEXT("box"), Box);

		Seen.Add(MakeShared<FJsonValueObject>(Entry));
	}

	OutResult->SetStringField(TEXT("name"), Name);
	OutResult->SetArrayField(TEXT("seen"), Seen);
	OutResult->SetArrayField(TEXT("occluded"), Occluded);
	OutResult->SetStringField(TEXT("world"), World->GetName());
	OutResult->SetBoolField(TEXT("pie"), GEditor->PlayWorld != nullptr);
	OutResult->SetNumberField(TEXT("meshActors"), MeshActorCount);
	OutResult->SetNumberField(TEXT("inFrustum"), InFrustumCount);
	OutResult->SetNumberField(TEXT("unoccluded"), UnoccludedCount);
	return true;
}
