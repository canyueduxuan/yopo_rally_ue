#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YOPODatasetCollector.generated.h"

class AYOPOTerrainPointCloudExporter;
class AYOPOBoxPointCloudExporter;
class APinholeCameraSensor;

UCLASS(Blueprintable)
class YOPO_RALLY_UE_API AYOPODatasetCollector : public AActor
{
    GENERATED_BODY()

public:
    AYOPODatasetCollector();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dataset|Actors")
    AYOPOTerrainPointCloudExporter* TerrainExporter = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dataset|Actors")
    AYOPOBoxPointCloudExporter* TreeExporter = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dataset|Actors")
    AActor* Vehicle = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dataset|Actors")
    APinholeCameraSensor* DepthCamera = nullptr;

    UPROPERTY(EditAnywhere, Category="Dataset|Poisson", meta=(ClampMin="1"))
    int32 TargetSamples = 64;

    UPROPERTY(EditAnywhere, Category="Dataset|Poisson", meta=(ClampMin="1", Units="cm"))
    float MinimumDistanceCm = 800;

    UPROPERTY(EditAnywhere, Category="Dataset|Poisson", meta=(ClampMin="1"))
    int32 AttemptsPerSample = 100;

    UPROPERTY(EditAnywhere, Category="Dataset|Poisson")
    int32 RandomSeed = 1337;

    /** Distance from the terrain to the vehicle origin along the surface normal. */
    UPROPERTY(EditAnywhere, Category="Dataset|Poisson", meta=(Units="cm"))
    float VehicleGroundOffsetCm = 22;

    /** Relative paths start at the project Saved directory. */
    UPROPERTY(EditAnywhere, Category="Dataset|Output")
    FString OutputDirectory = TEXT("TrainingData");

    UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category="Dataset")
    FString Status;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Dataset")
    void CollectDataset();
};
