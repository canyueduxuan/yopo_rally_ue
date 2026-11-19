#include "YOPODatasetCollector.h"
#include "YOPOBoxPointCloudExporter.h"
#include "YOPOTerrainPointCloudExporter.h"
#include "PinholeCameraSensor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"

namespace
{
bool ExportClouds(AYOPOTerrainPointCloudExporter& Ground, AYOPOBoxPointCloudExporter& Trees,
    const FString& Directory)
{
    TGuardValue<FString> GroundDir(Ground.OutputDirectory, Directory);
    TGuardValue<FString> GroundName(Ground.FileName, TEXT("terrain"));
    TGuardValue<bool> GroundStamp(Ground.bAppendTimestamp, false);
    Ground.ExportPLY();
    if (!Ground.bLastExportSucceeded || Ground.LastPointCount == 0) return false;

    TGuardValue<FString> TreeDir(Trees.OutputDirectory, Directory);
    TGuardValue<FString> TreeName(Trees.FileName, TEXT("tree"));
    TGuardValue<bool> TreeStamp(Trees.bAppendTimestamp, false);
    TGuardValue<bool> TreeLocal(Trees.bOutputBoxLocalCoordinates, false);
    TGuardValue<EYOPOPointCloudFormat> Format(Trees.OutputFormat, EYOPOPointCloudFormat::PLY_Binary);
    TGuardValue<EYOPOPointCloudCoordinateSystem> Frame(Trees.CoordinateSystem, EYOPOPointCloudCoordinateSystem::ROS);
    Trees.ExportPointCloud();
    return Trees.bLastExportSucceeded;
}

bool SaveDepth(APinholeCameraSensor& Camera, const FString& Path)
{
    UTextureRenderTarget2D* Target = Camera.DepthCapture->TextureTarget;
    Camera.DepthCapture->CaptureScene();
    TArray<FLinearColor> Raw;
    FReadSurfaceDataFlags Flags(RCM_MinMax);
    Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Raw, Flags)
        || Raw.Num() != int64(Target->SizeX) * Target->SizeY) return false;

    TArray<FFloat16Color> Pixels;
    Pixels.Reserve(Raw.Num());
    for (const FLinearColor& Pixel : Raw)
    {
        const float D = Pixel.R > 0 && FMath::IsFinite(Pixel.R)
            ? FMath::Min(1.f, Pixel.R / (100.f * Camera.FarClipMetres)) : 1.f;
        Pixels.Emplace(FLinearColor(D, D, D, 1.f));
    }
    auto& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    auto Encoder = Module.CreateImageWrapper(EImageFormat::EXR);
    if (!Encoder || !Encoder->SetRaw(Pixels.GetData(), Pixels.Num() * int64(sizeof(FFloat16Color)),
        Target->SizeX, Target->SizeY, ERGBFormat::RGBAF, 16)) return false;
    const auto& Bytes = Encoder->GetCompressed();
    return Bytes.Num() > 0 && FFileHelper::SaveArrayToFile(Bytes, *Path);
}
}

AYOPODatasetCollector::AYOPODatasetCollector()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AYOPODatasetCollector::CollectDataset()
{
    auto Fail = [this](const FString& Message)
    {
        Status = Message;
        UE_LOG(LogTemp, Error, TEXT("YOPO dataset: %s"), *Status);
    };
    if (!GetWorld() || GetWorld()->WorldType != EWorldType::Editor)
        return Fail(TEXT("Run Collect Dataset in the editor world, outside Play mode."));
    if (!IsValid(Vehicle) || !IsValid(DepthCamera) || !IsValid(TerrainExporter) || !IsValid(TreeExporter)
        || !DepthCamera->IsAttachedTo(Vehicle))
        return Fail(TEXT("Assign both exporters, the vehicle and its attached pinhole depth camera."));
    if (TargetSamples < 1 || AttemptsPerSample < 1 || MinimumDistanceCm < 1 || OutputDirectory.IsEmpty()
        || DepthCamera->FarClipMetres <= 0 || !DepthCamera->GetDepthRenderTarget())
        return Fail(TEXT("Invalid sampling parameters, output directory or camera configuration."));

    UBoxComponent* Box = TerrainExporter->ScanBounds;
    const FVector Extent = Box->GetScaledBoxExtent();
    TArray<FTransform> Poses;
    TMap<FIntPoint, TArray<FVector2D>> Grid;
    FRandomStream Random(RandomSeed);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(YOPODatasetSample), TerrainExporter->bTraceComplex, Vehicle);
    Query.AddIgnoredActor(DepthCamera);
    const int64 Budget = int64(TargetSamples) * AttemptsPerSample;
    for (int64 I = 0; I < Budget && Poses.Num() < TargetSamples; ++I)
    {
        const double X = Random.FRandRange(-Extent.X, Extent.X);
        const double Y = Random.FRandRange(-Extent.Y, Extent.Y);
        const FTransform Bounds(Box->GetComponentQuat(), Box->GetComponentLocation());
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Bounds.TransformPosition(FVector(X,Y,Extent.Z)),
            Bounds.TransformPosition(FVector(X,Y,-Extent.Z)), TerrainExporter->TerrainTraceChannel, Query)) continue;
        if (Hit.ImpactNormal.Z <= 0) continue;
        const FVector2D XY(Hit.ImpactPoint);
        const FIntPoint Cell(FMath::FloorToInt(XY.X / MinimumDistanceCm), FMath::FloorToInt(XY.Y / MinimumDistanceCm));
        bool bTooClose = false;
        for (int32 Dx = -1; Dx <= 1 && !bTooClose; ++Dx)
            for (int32 Dy = -1; Dy <= 1 && !bTooClose; ++Dy)
                if (auto* Neighbors = Grid.Find(Cell + FIntPoint(Dx,Dy)))
                    bTooClose = Neighbors->ContainsByPredicate([&](const FVector2D& P)
                    { return FVector2D::DistSquared(P, XY) < FMath::Square(MinimumDistanceCm); });
        if (bTooClose) continue;
        Grid.FindOrAdd(Cell).Add(XY);
        const FVector Heading = FRotator(0, Random.FRandRange(0.f, 360.f), 0).Vector();
        Poses.Emplace(FRotationMatrix::MakeFromZX(Hit.ImpactNormal, Heading).ToQuat(),
            Hit.ImpactPoint + Hit.ImpactNormal * VehicleGroundOffsetCm, Vehicle->GetActorScale3D());
    }
    if (Poses.IsEmpty()) return Fail(TEXT("No terrain samples. Check loaded terrain, collision channel and scan bounds."));

    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(OutputDirectory)
        ? FPaths::ProjectSavedDir() / OutputDirectory : OutputDirectory);
    auto& Files = IFileManager::Get();
    int32 SceneIndex = 0;
    FString Directory;
    do { Directory = Root / FString::Printf(TEXT("Scene_%d"), SceneIndex++); }
    while (Files.DirectoryExists(*Directory) || Files.FileExists(*Directory));
    if (!Files.MakeDirectory(*(Directory / TEXT("Textures")), true))
        return Fail(TEXT("Cannot create output directory: ") + Directory);
    FScopedSlowTask Progress(Poses.Num() + 1, FText::FromString(TEXT("Collecting YOPO dataset")));
    Progress.MakeDialog(true);
    Progress.EnterProgressFrame(1);
    if (!ExportClouds(*TerrainExporter, *TreeExporter, Directory))
        return Fail(TEXT("Point cloud export failed. Check exporter log and query budgets: ") + Directory);

    // The projection matrix uses normalized intrinsics, so resizing the capture
    // preserves the camera rays and field of view at the reference resolution.
    UTextureRenderTarget2D* DatasetDepth = NewObject<UTextureRenderTarget2D>(this);
    DatasetDepth->RenderTargetFormat = RTF_RGBA32f;
    DatasetDepth->InitAutoFormat(160, 90);
    DatasetDepth->UpdateResourceImmediate(true);
    auto* Capture = DepthCamera->DepthCapture;
    UTextureRenderTarget2D* OriginalTarget = Capture->TextureTarget;
    Capture->TextureTarget = DatasetDepth;
    ON_SCOPE_EXIT { Capture->TextureTarget = OriginalTarget; };

    const FTransform Original = Vehicle->GetActorTransform();
    ON_SCOPE_EXIT { Vehicle->SetActorTransform(Original, false, nullptr, ETeleportType::TeleportPhysics); };
    const auto& K = DepthCamera->Intrinsics;
    const double HFov = FMath::RadiansToDegrees(FMath::Atan2(double(K.Cx),double(K.Fx)) +
        FMath::Atan2(double(K.Width-K.Cx),double(K.Fx)));
    FString Toml = FString::Printf(TEXT("depthCameraFarClipPlane = %.9f\ndepthCameraHorizontalFOV = %.9f\n"), DepthCamera->FarClipMetres, HFov);
    for (int32 I = 0; I < Poses.Num(); ++I)
    {
        if (Progress.ShouldCancel()) return Fail(TEXT("Cancelled. Partial output: ") + Directory);
        Progress.EnterProgressFrame(1);
        if (!Vehicle->SetActorTransform(Poses[I], false, nullptr, ETeleportType::TeleportPhysics))
            return Fail(TEXT("Cannot move vehicle."));
        const auto CameraPose = DepthCamera->DepthCapture->GetComponentTransform();
        const FVector P = CameraPose.GetLocation() * 0.01;
        const double Yaw = FRotator::ClampAxis(-CameraPose.Rotator().Yaw);
        const FString Name = FString::Printf(TEXT("depth_%d.exr"), I);
        if (!SaveDepth(*DepthCamera, Directory / TEXT("Textures") / Name))
            return Fail(TEXT("Depth capture or EXR write failed: ") + Name);
        Toml += FString::Printf(TEXT("\n[[dataArray]]\nimageFileNameList = [ \"%s\", ]\nposStart = [ %.9f, %.9f, ]\nyawStart = %.9f\n"),
            *Name, P.X, -P.Y, Yaw);
    }
    if (!FFileHelper::SaveStringToFile(Toml, *(Directory / TEXT("data.toml")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Fail(TEXT("Cannot write data.toml: ") + Directory);
    Status = FString::Printf(TEXT("Saved %d/%d samples, %d terrain and %d tree points to %s"),
        Poses.Num(), TargetSamples, TerrainExporter->LastPointCount, TreeExporter->LastPointCount, *Directory);
    UE_LOG(LogTemp, Display, TEXT("YOPO dataset: %s"), *Status);
}
