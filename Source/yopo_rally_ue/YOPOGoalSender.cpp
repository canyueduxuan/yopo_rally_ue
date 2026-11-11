#include "YOPOGoalSender.h"
#include "YOPOROSConverters.h"

#include "Async/Async.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "RI/Topic.h"
#include "ROSIntegrationGameInstance.h"
#include "ROSTime.h"
#include "ScoutMiniROSComponent.h"
#include "TimerManager.h"
#include "geometry_msgs/PoseStamped.h"

AYOPOGoalSender::AYOPOGoalSender()
{
    PrimaryActorTick.bCanEverTick = false;
    UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("GoalDirection"));
    SetRootComponent(Arrow);
    Arrow->ArrowColor = FColor::Green;
    Arrow->ArrowSize = 2.0f;
}

void AYOPOGoalSender::BeginPlay()
{
    Super::BeginPlay();
    VehicleROS = IsValid(VehicleActor) ? VehicleActor->FindComponentByClass<UScoutMiniROSComponent>() : nullptr;
    if (!VehicleROS || !VehicleROS->bEnabled)
    {
        UE_LOG(LogTemp, Error, TEXT("YOPOGoalSender: assign a vehicle with an enabled ScoutMiniROSComponent."));
        return;
    }

    // Ensure the vehicle has captured its odometry origin before we capture ours.
    // Both captures happen before the first simulation tick; do not recapture on reconnect.
    if (!VehicleActor->HasActorBegunPlay()) VehicleActor->DispatchBeginPlay();
    OdometryOrigin = VehicleActor->GetActorTransform();
    GetWorldTimerManager().SetTimer(PublishTimer, this, &AYOPOGoalSender::PublishGoal, 1.0f, true);
}

void AYOPOGoalSender::PublishGoal()
{
    bLastPublishSucceeded = false;
    if (!bPublishGoal || !IsValid(VehicleROS) || !IsValid(VehicleActor)) return;

    UROSIntegrationGameInstance* Instance = Cast<UROSIntegrationGameInstance>(GetGameInstance());
    const int32 ConnectionId = VehicleROS->ROSConnectionId;
    if (!Instance || !Instance->ConnectedToROSBridge.IsValidIndex(ConnectionId)
        || !Instance->ConnectedToROSBridge[ConnectionId]) return;

    if (!GoalTopic)
    {
        UROSIntegrationCore* Core = Instance->GetROSConnectionFromID(ConnectionId);
        if (!Core) return;
        GoalTopic = NewObject<UTopic>(this);
        GoalTopic->Init(Core, GoalTopicName, TEXT("geometry_msgs/PoseStamped"), 1, false);
        InitialPoseTopic = NewObject<UTopic>(this);
        InitialPoseTopic->Init(Core, TEXT("/initialpose"), TEXT("geometry_msgs/PoseWithCovarianceStamped"), 1, false);
        InitialPoseTopic->Advertise();

        TWeakObjectPtr<AYOPOGoalSender> WeakThis(this);
        VelocityTopic = NewObject<UTopic>(this);
        VelocityTopic->Init(Core, VehicleROS->CmdVelTopicName, TEXT("geometry_msgs/Twist"), 1, false);
        VelocityTopic->Subscribe([WeakThis](TSharedPtr<FROSBaseMsg>)
        {
            AsyncTask(ENamedThreads::GameThread, [WeakThis]()
            {
                if (!WeakThis.IsValid() || WeakThis->bEndingPlay
                    || !WeakThis->bStartSignalSent || WeakThis->bMPCStartConfirmed) return;
                WeakThis->bMPCStartConfirmed = true;
                UE_LOG(LogTemp, Display, TEXT("YOPOGoalSender: MPC start confirmed by velocity output."));
            });
        });
        EndStateTopic = NewObject<UTopic>(this);
        EndStateTopic->Init(Core, TEXT("/end_state"), TEXT("nav_msgs/Odometry"), 1, false);
        EndStateTopic->Subscribe([WeakThis](TSharedPtr<FROSBaseMsg>)
        {
            AsyncTask(ENamedThreads::GameThread, [WeakThis]()
            {
                if (WeakThis.IsValid() && !WeakThis->bEndingPlay)
                    WeakThis->LastEndStateTime = FPlatformTime::Seconds();
            });
        });
        MPCLogTopic = NewObject<UTopic>(this);
        MPCLogTopic->Init(Core, TEXT("/rosout"), TEXT("rcl_interfaces/Log"), 10, false);
        MPCLogTopic->Subscribe([WeakThis](TSharedPtr<FROSBaseMsg> BaseMessage)
        {
            const auto Log = StaticCastSharedPtr<FMPCLogMessage>(BaseMessage);
            if (Log->Name != TEXT("mpc_node")) return;
            const FString Text = Log->Message;
            AsyncTask(ENamedThreads::GameThread, [WeakThis, Text]()
            {
                if (!WeakThis.IsValid() || WeakThis->bEndingPlay) return;
                if (Text == TEXT("Waiting for MPC start..."))
                    WeakThis->LastMPCWaitingTime = FPlatformTime::Seconds();
                else if (Text == TEXT("MPC started"))
                {
                    WeakThis->bMPCStartConfirmed = true;
                    UE_LOG(LogTemp, Display, TEXT("YOPOGoalSender: MPC start confirmed."));
                }
            });
        });
    }
    if (!GoalTopic->Advertise()) return;

    const FTransform WorldGoal = IsValid(GoalActor) ? GoalActor->GetActorTransform() : GetActorTransform();
    const FTransform Goal = VehicleROS->bUseAbsoluteWorldOdometry
        ? WorldGoal : WorldGoal.GetRelativeTransform(OdometryOrigin);
    const FVector Position = Goal.GetLocation() / 100.0;
    const FQuat Rotation = Goal.GetRotation().GetNormalized();

    TSharedPtr<ROSMessages::geometry_msgs::PoseStamped> Message = MakeShared<ROSMessages::geometry_msgs::PoseStamped>();
    Message->header.seq = Sequence++;
    Message->header.time = FROSTime::Now();
    Message->header.frame_id = VehicleROS->OdometryFrameId;
    Message->pose.position.x = Position.X;
    Message->pose.position.y = -Position.Y;
    Message->pose.position.z = Position.Z;
    Message->pose.orientation.x = -Rotation.X;
    Message->pose.orientation.y = Rotation.Y;
    Message->pose.orientation.z = -Rotation.Z;
    Message->pose.orientation.w = Rotation.W;

    bLastPublishSucceeded = GoalTopic->Publish(Message);
    if (bLastPublishSucceeded)
    {
        LastGoalROSMeters = FVector(Position.X, -Position.Y, Position.Z);
        if (PublishedGoals == 0)
        {
            UE_LOG(LogTemp, Display, TEXT("YOPOGoalSender: publishing %s at 1 Hz in %s; goal=%s m"),
                *GoalTopicName, *VehicleROS->OdometryFrameId, *LastGoalROSMeters.ToString());
        }
        ++PublishedGoals;
        TryStartMPC();
    }
}

void AYOPOGoalSender::TryStartMPC()
{
    if (!bAutoStartMPC || bStartSignalSent || bMPCStartConfirmed || !InitialPoseTopic) return;
    const double Now = FPlatformTime::Seconds();
    // The waiting log is emitted only after MPC has installed its /initialpose subscription.
    if (LastEndStateTime < 0.0 || Now - LastEndStateTime > 2.0
        || LastMPCWaitingTime < 0.0 || Now - LastMPCWaitingTime > 3.0) return;
    if (!InitialPoseTopic->Advertise()) return;

    const FTransform WorldPose = VehicleActor->GetActorTransform();
    const FTransform Pose = VehicleROS->bUseAbsoluteWorldOdometry
        ? WorldPose : WorldPose.GetRelativeTransform(OdometryOrigin);
    const FVector Position = Pose.GetLocation() / 100.0;
    const FQuat Rotation = Pose.GetRotation().GetNormalized();
    auto Message = MakeShared<FInitialPoseMessage>();
    Message->Header.time = FROSTime::Now();
    Message->Header.frame_id = VehicleROS->OdometryFrameId;
    Message->Pose.pose.position.x = Position.X;
    Message->Pose.pose.position.y = -Position.Y;
    Message->Pose.pose.position.z = Position.Z;
    Message->Pose.pose.orientation.x = -Rotation.X;
    Message->Pose.pose.orientation.y = Rotation.Y;
    Message->Pose.pose.orientation.z = -Rotation.Z;
    Message->Pose.pose.orientation.w = Rotation.W;
    Message->Pose.covariance.Init(0.0, 36);
    bStartSignalSent = InitialPoseTopic->Publish(Message);
    if (bStartSignalSent)
        UE_LOG(LogTemp, Display, TEXT("YOPOGoalSender: sent one-shot /initialpose after MPC ready and trajectory received."));
}

void AYOPOGoalSender::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    GetWorldTimerManager().ClearTimer(PublishTimer);
    if (EndStateTopic) { EndStateTopic->Unsubscribe(); EndStateTopic = nullptr; }
    if (MPCLogTopic) { MPCLogTopic->Unsubscribe(); MPCLogTopic = nullptr; }
    if (VelocityTopic) { VelocityTopic->Unsubscribe(); VelocityTopic = nullptr; }
    if (InitialPoseTopic) { InitialPoseTopic->Unadvertise(); InitialPoseTopic = nullptr; }
    if (GoalTopic)
    {
        GoalTopic->Unadvertise();
        GoalTopic = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}
