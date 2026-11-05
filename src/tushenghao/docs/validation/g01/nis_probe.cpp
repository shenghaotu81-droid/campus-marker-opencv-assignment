// 只读离线诊断：真实TargetTracker决定更新/重建；影子状态调用同一未修改CvFilter数学。
// 逐帧核对公开位置/P/已发布速度，确保门前NIS没有换滤波器、关门或截断样本。
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include "filter/kalman_cv.hpp"
#include "measurement/measurement_adapter.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <limits>
using namespace mark::tracking;
namespace d=mark::tracking::detail;
static bool has(const TrackingResult &r,const std::string &prefix){
    for(const auto &e:r.events_)if(e.rfind(prefix,0)==0)return true;return false;
}
static double elapsed(int64_t a,int64_t b){return double((static_cast<long double>(a)-b)/1e6L);}
int main(int argc,char **argv){try{
    if(argc!=4)throw std::runtime_error("OBSERVATIONS TRACKING_CONFIG OUTPUT_PREFIX required");
    const auto config=load_tracking_config(argv[2]);
    if(!config.profile_ || config.coordinate_mode_!=CoordinateMode::CameraRelative)
        throw std::runtime_error("probe requires explicit camera-relative profile");
    const auto &p=*config.profile_;const auto frames=read_tracking_frames(argv[1]);
    TargetTracker tracker(config);d::CvFilter shadow;
    bool live=false,confirmed=false;int64_t state_time=0,seed_time=0,pending_time=0;
    d::PositionMeasurement seed;std::optional<d::PositionMeasurement> pending;
    std::vector<TrackingResult> results;size_t checked=0,vel_checked=0,nis_checked=0;
    double max_position_difference=0,max_covariance_difference=0,max_velocity_difference=0,max_nis_difference=0;
    std::ofstream out(std::string(argv[3])+".csv");out<<std::setprecision(17);
    out<<"frame,pre_live,pre_confirmed,nis,accepted,reseeded,created,lost,rx,ry,rz,s00,s01,s02,s11,s12,s22,pred_vx,pred_vy,pred_vz,pred_vstd,post_vstd,velocity_public,track_id,post_vx,post_vy,post_vz\n";
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for(const auto &f:frames){
        if(f.stamp_.exposure_us_ || f.stamp_.time_uncertainty_s_)
            throw std::runtime_error("probe restricted to this unknown-time video domain");
        const int64_t now=d::sample_time(f.stamp_);
        if(live){const double dt=elapsed(now,state_time);if(dt>p.max_dt_s_)live=false;
            else shadow.predict(dt,p.acceleration_psd_m2ps3_);}
        const bool pre_live=live,pre_confirmed=confirmed;
        state_time=now;
        std::vector<std::string> ignored;
        auto measurements=d::adapt_measurements(f,config,ignored);
        if(measurements.size()>1)throw std::runtime_error("probe is single-target diagnostic");
        cv::Vec3d pv=live?shadow.velocity():cv::Vec3d::all(nan);
        double pvstd=live?shadow.velocity_std():nan;
        // 与TargetTracker相同的未知时间路径；直接调用生产time_augmented_noise。
        for(auto &m:measurements){
            cv::Vec3d velocity(0,0,0);
            cv::Matx33d vv=cv::Matx33d::eye()*p.initial_velocity_variance_m2ps2_;
            if(live){velocity=shadow.velocity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)vv(i,j)=shadow.covariance()(i+3,j+3);}
            const double fallback=p.validated_time_bound_s_.value_or(p.max_dt_s_);
            const double b=fallback>0?fallback:p.max_dt_s_;
            m.R_m2_=d::time_augmented_noise(m.R_m2_,velocity,vv,b,p.acceleration_psd_m2ps3_);
        }
        std::optional<d::InnovationResult> innovation;
        if(live && measurements.size()==1)innovation=shadow.innovation(measurements[0].position_m_,measurements[0].R_m2_);
        auto r=tracker.step(f);
        if(!r.input_accepted_ || !r.ready_ || has(r,"TRACK_MATH_REJECTED"))
            throw std::runtime_error("real tracker rejected frame or mathematics");
        const bool created=has(r,"TRACK_CREATED:"),reseeded=has(r,"TRACK_RESEEDED"),lost=has(r,"TRACK_LOST:");
        if(created){
            if(measurements.size()!=1)throw std::runtime_error("create without one measurement");
            const auto &m=measurements[0];shadow.initialize(m.position_m_,m.R_m2_,p.initial_velocity_variance_m2ps2_);
            if(reseeded){if(!pending)throw std::runtime_error("missing real pending source");
                shadow.bootstrap(pending->position_m_,pending->R_m2_,m.position_m_,m.R_m2_,elapsed(now,pending_time),p.acceleration_psd_m2ps3_);}
            seed=m;seed_time=now;live=true;
        }else if(r.association_nis_){
            if(!innovation || measurements.size()!=1)throw std::runtime_error("missing pre-gate innovation");
            max_nis_difference=std::max(max_nis_difference,std::abs(innovation->score-*r.association_nis_));++nis_checked;
            const auto &m=measurements[0];
            if(!confirmed)shadow.bootstrap(seed.position_m_,seed.R_m2_,m.position_m_,m.R_m2_,elapsed(now,seed_time),p.acceleration_psd_m2ps3_);
            else shadow.correct(*innovation);
            seed=m;seed_time=now;
        }
        if(has(r,"PENDING_RESEED_POSITION_ONLY")){pending=measurements.at(0);pending_time=now;}
        else if(created || r.association_nis_ || measurements.empty())pending.reset();
        const TargetTrack *t=r.tracks_.empty()?nullptr:&r.tracks_[0];
        if(!t || t->state_==TrackState::Lost)live=false;
        double poststd=nan;cv::Vec3d postv=cv::Vec3d::all(nan);bool published=false;uint64_t id=t?t->track_id_:0;
        if(live){
            if(!t->position_m_ || !t->covariance_)throw std::runtime_error("missing public comparison state");
            const double pd=cv::norm(shadow.position()-*t->position_m_);
            const double cd=cv::norm(cv::Mat(shadow.covariance()-*t->covariance_),cv::NORM_INF);
            max_position_difference=std::max(max_position_difference,pd);max_covariance_difference=std::max(max_covariance_difference,cd);++checked;
            if(pd>1e-12 || cd>1e-12)throw std::runtime_error("shadow differs from actual G4 at frame "+std::to_string(f.stamp_.frame_id_));
            if(t->velocity_mps_){const double vd=cv::norm(shadow.velocity()-*t->velocity_mps_);max_velocity_difference=std::max(max_velocity_difference,vd);++vel_checked;
                if(vd>1e-12)throw std::runtime_error("public velocity mismatch");published=true;}
            confirmed=t->confirmed_;poststd=shadow.velocity_std();postv=shadow.velocity();
        }else confirmed=false;
        if(t && (t->time_bounded_ || t->qualified_for_g5_))throw std::runtime_error("unknown time was promoted");
        cv::Vec3d residual=innovation?innovation->residual:cv::Vec3d::all(nan);
        cv::Matx33d S=innovation?innovation->S:cv::Matx33d::all(nan);
        out<<f.stamp_.frame_id_<<','<<pre_live<<','<<pre_confirmed<<','<<(innovation?innovation->score:nan)<<','<<bool(r.association_nis_)<<','<<reseeded<<','<<created<<','<<lost;
        for(double x:residual.val)out<<','<<x;
        for(auto ij:{std::pair<int,int>{0,0},{0,1},{0,2},{1,1},{1,2},{2,2}})out<<','<<S(ij.first,ij.second);
        for(double x:pv.val)out<<','<<x;
        out<<','<<pvstd<<','<<poststd<<','<<published<<','<<id;
        for(double x:postv.val)out<<','<<x;
        out<<'\n';results.push_back(std::move(r));
    }
    write_tracking_results(results,std::string(argv[3])+"-tracks.yaml");
    std::cout<<std::setprecision(17)<<"frames="<<frames.size()<<" live_comparisons="<<checked<<" public_velocity_comparisons="<<vel_checked<<" accepted_nis_comparisons="<<nis_checked
        <<" max_position_diff="<<max_position_difference<<" max_covariance_diff="<<max_covariance_difference<<" max_velocity_diff="<<max_velocity_difference<<" max_accepted_nis_diff="<<max_nis_difference<<'\n';
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
