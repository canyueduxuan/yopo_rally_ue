#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YOPOGoalSender.generated.h"

class UTopic;
class UScoutMiniROSComponent;

/** Place in a level, assign the odometry vehicle, and publish a goal every second. */
UCLASS(Blueprintable)
class YOPO_RALLY_UE_API AYOPOGoalSender : public AActor
{
    GENERATED_BODY()

public:
    AYOPOGoalSender();

    /** Vehicle whose ScoutMiniROSComponent publishes the algorithm's odometry. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="YOPO|Goal")
    AActor* VehicleActor = nullptr;

    /** Optional target. When unset, this actor's position and rotation are used. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YOPO|Goal")
    AActor* GoalActor = nullptr;

    /** Can be toggled at runtime without restarting the timer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YOPO|Goal")
    bool bPublishGoal = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="YOPO|ROS")
    FString GoalTopicName = TEXT("/goal_pose");

    /** Once per Play session, start MPC after it reports waiting and YOPO produces a trajectory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="YOPO|MPC")
    bool bAutoStartMPC = true;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="YOPO|Status")
    bool bStartSignalSent = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="YOPO|Status")
    bool bMPCStartConfirmed = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="YOPO|Status")
    int32 PublishedGoals = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="YOPO|Status")
    FVector LastGoalROSMeters = FVector::ZeroVector;

    /** True means the last publish was accepted locally, not acknowledged by ROS. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="YOPO|Status")
    bool bLastPublishSucceeded = false;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    UTopic* GoalTopic = nullptr;

    UPROPERTY(Transient)
    UTopic* InitialPoseTopic = nullptr;

    UPROPERTY(Transient)
    UTopic* EndStateTopic = nullptr;

    UPROPERTY(Transient)
    UTopic* MPCLogTopic = nullptr;

    UPROPERTY(Transient)
    UTopic* VelocityTopic = nullptr;

    UPROPERTY(Transient)
    UScoutMiniROSComponent* VehicleROS = nullptr;

    FTransform OdometryOrigin;
    FTimerHandle PublishTimer;
    uint32 Sequence = 0;
    double LastEndStateTime = -1.0;
    double LastMPCWaitingTime = -1.0;
    bool bEndingPlay = false;
    void PublishGoal();
    void TryStartMPC();
};
