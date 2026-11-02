// T→A06：只把类型化原因编码为既有记录枚举，不反向控制时序执行。
#pragma once
#include "vision/observability/frame_record.hpp"
#include "vision/types/detection/temporal_types.hpp"
namespace mark {
inline ReasonCode temporalRecordCode(TemporalCause cause) {
    switch(cause) {
    case TemporalCause::HistoryExpired:return ReasonCode::HistoryExpired;
    case TemporalCause::AssociationFailed:return ReasonCode::AssociationFailed;
    case TemporalCause::AssociationAmbiguous:return ReasonCode::AssociationAmbiguous;
    case TemporalCause::CorrespondenceAmbiguous:return ReasonCode::CorrespondenceAmbiguous;
    case TemporalCause::ZeroDt:return ReasonCode::ZeroDt;
    case TemporalCause::SmoothingDeviation:return ReasonCode::SmoothingRejected;
    default:return ReasonCode::OtherRecordedReason;
    }
}
}
