#pragma once

#include "Conversion/Messages/BaseMessageConverter.h"
#include "geometry_msgs/PoseWithCovariance.h"
#include "YOPOROSConverters.generated.h"

class FInitialPoseMessage : public FROSBaseMsg
{
public:
    FInitialPoseMessage() { _MessageType = TEXT("geometry_msgs/PoseWithCovarianceStamped"); }
    ROSMessages::std_msgs::Header Header;
    ROSMessages::geometry_msgs::PoseWithCovariance Pose;
};

class FMPCLogMessage : public FROSBaseMsg
{
public:
    FMPCLogMessage() { _MessageType = TEXT("rcl_interfaces/Log"); }
    FString Name;
    FString Message;
};

UCLASS()
class UInitialPoseMessageConverter : public UBaseMessageConverter
{
    GENERATED_BODY()
public:
    UInitialPoseMessageConverter();
    virtual bool ConvertOutgoingMessage(TSharedPtr<FROSBaseMsg> Message, bson_t** BSON) override;
};

UCLASS()
class UMPCLogMessageConverter : public UBaseMessageConverter
{
    GENERATED_BODY()
public:
    UMPCLogMessageConverter();
    virtual bool ConvertIncomingMessage(const ROSBridgePublishMsg* Message, TSharedPtr<FROSBaseMsg>& Result) override;
};
