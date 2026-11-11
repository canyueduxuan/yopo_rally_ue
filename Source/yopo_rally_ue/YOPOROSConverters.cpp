#include "YOPOROSConverters.h"

#include "Conversion/Messages/geometry_msgs/GeometryMsgsPoseWithCovarianceConverter.h"
#include "Conversion/Messages/std_msgs/StdMsgsHeaderConverter.h"

UInitialPoseMessageConverter::UInitialPoseMessageConverter()
{
    _MessageType = TEXT("geometry_msgs/PoseWithCovarianceStamped");
}

bool UInitialPoseMessageConverter::ConvertOutgoingMessage(TSharedPtr<FROSBaseMsg> Message, bson_t** BSON)
{
    const auto Pose = StaticCastSharedPtr<FInitialPoseMessage>(Message);
    if (!Pose.IsValid() || Pose->Pose.covariance.Num() != 36) return false;
    *BSON = bson_new();
    UStdMsgsHeaderConverter::_bson_append_child_header(*BSON, "header", &Pose->Header);
    UGeometryMsgsPoseWithCovarianceConverter::_bson_append_child_pose_with_covariance(*BSON, "pose", &Pose->Pose);
    return true;
}

UMPCLogMessageConverter::UMPCLogMessageConverter()
{
    _MessageType = TEXT("rcl_interfaces/Log");
}

bool UMPCLogMessageConverter::ConvertIncomingMessage(const ROSBridgePublishMsg* Message, TSharedPtr<FROSBaseMsg>& Result)
{
    auto Log = MakeShared<FMPCLogMessage>();
    bool Found = false;
    Log->Name = GetFStringFromBSON(TEXT("msg.name"), Message->full_msg_bson_, Found);
    if (!Found) return false;
    Log->Message = GetFStringFromBSON(TEXT("msg.msg"), Message->full_msg_bson_, Found);
    if (!Found) return false;
    Result = Log;
    return true;
}
