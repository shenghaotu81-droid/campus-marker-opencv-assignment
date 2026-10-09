# 中文用途：在内部证据中显式记录周期模型残差，validator复算；旧字段仍保存原像素到直线距离，禁止偷换含义。
from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_types.hpp');s=p.read_text().replace('struct CornerEvidence\n    {','''struct CornerEvidence
    {
        // 沙盒C实验：0=原单线，1=逐弧周期，2=当前帧统一校正；仅内部证据扩展。
        std::array<int,2> sandbox_model_kind_{{0,0}}, sandbox_model_axis_{{1,1}};
        std::array<double,2> sandbox_model_coefficient_{{0,0}}, sandbox_model_mean_{{0,0}}, sandbox_model_max_{{0,0}};''',1);p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_observation.hpp');s=p.read_text().replace('bool ready=false;', 'bool ready=false;\n    double parity_shift=0;');p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_observation.cpp');s=p.read_text().replace('#include "corners/sandbox_profile.hpp"','#include "corners/sandbox_profile.hpp"\n#include "corners/sandbox_periodic.hpp"');s=s.replace('index.ready=true;','// 沙盒C2：每帧原图入口估计一次，绝不在失败角上重新拟合偏移。\n        if(sandbox::modelMode()==2)index.parity_shift=sandbox::estimateFrameShift(contours);\n        index.ready=true;',1);p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_resolver.cpp');s=p.read_text().replace('#include "corners/corner_edge_fit.hpp"','#include "corners/corner_edge_fit.hpp"\n#include "corners/sandbox_periodic.hpp"');s=s.replace('auto fit = fitObservedEdgePair(contour.contour, edges, config);','sandbox::frame_shift=index.parity_shift;\n            auto fit = fitObservedEdgePair(contour.contour, edges, config);');p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text().replace('#include "corners/sandbox_profile.hpp"','#include "corners/sandbox_profile.hpp"\n#include "corners/sandbox_periodic.hpp"');s=s.replace('double mean, maximum;', 'double mean, maximum;\n            double model_mean=0,model_max=0,model_coefficient=0;\n            int model_kind=0,model_axis=1;',1)
s=s.replace('bool fitArc(Arc &arc, const CornerConfig &config)', 'bool fitArc(Arc &arc, const CornerConfig &config, int model_kind)')
s=s.replace('cv::fitLine(arc.points, arc.line, cv::DIST_L2, 0, 0.01, 0.01);','''arc.model_kind=model_kind;
            if(model_kind==1){auto f=sandbox::fitPeriodic(arc.points);if(!f.valid)return false;arc.line=f.line;arc.model_axis=f.axis;arc.model_coefficient=f.coefficient;}
            else if(model_kind==2){std::vector<cv::Point2d>corrected;corrected.reserve(arc.points.size());arc.model_coefficient=sandbox::frame_shift;for(auto p:arc.points)corrected.emplace_back(p.x-arc.model_coefficient*sandbox::phase(p,1),p.y);cv::fitLine(corrected,arc.line,cv::DIST_L2,0,.01,.01);}
            else cv::fitLine(arc.points, arc.line, cv::DIST_L2, 0, 0.01, 0.01);''',1)
s=s.replace('double lo = INFINITY, hi = -INFINITY, sum = 0;', 'double lo = INFINITY, hi = -INFINITY, sum = 0,model_sum=0;',1)
s=s.replace('sum += d;\n                arc.maximum', '''sum += d;
                double corrected=sandbox::correctedDistance(p,arc.line,arc.model_kind,arc.model_axis,arc.model_coefficient);
                model_sum+=corrected;arc.model_max=std::max(arc.model_max,corrected);
                arc.maximum''',1)
s=s.replace('arc.mean = sum / arc.points.size();','arc.mean = sum / arc.points.size();arc.model_mean=model_sum/arc.points.size();',1)
s=s.replace('arc.mean <= config.max_line_fit_error_;','arc.model_mean <= config.max_line_fit_error_;',1)
s=s.replace('bool all_endpoints, bool fitted_position)', 'bool all_endpoints, bool fitted_position, int model_kind=0)',1)
s=s.replace('fitArc(arc, config)', 'fitArc(arc, config, model_kind)',1)
s=s.replace('evidence.line_mean_residual_px_ = {first.mean, second.mean};','''evidence.line_mean_residual_px_ = {first.mean, second.mean};
                evidence.sandbox_model_kind_={first.model_kind,second.model_kind};
                evidence.sandbox_model_axis_={first.model_axis,second.model_axis};
                evidence.sandbox_model_coefficient_={first.model_coefficient,second.model_coefficient};
                evidence.sandbox_model_mean_={first.model_mean,second.model_mean};
                evidence.sandbox_model_max_={first.model_max,second.model_max};''',1)
s=s.replace('if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false);\n        return r;','''if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false);
        int model=sandbox::modelMode();
        if(!r.evidence&&model){r=fitObservedEdgePairImpl(contour,edges,config,false,false,model);
          if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,false,true,model);
          if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false,model);}
        return r;''',1);p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_evidence_validation.cpp');s=p.read_text().replace('#include "corners/sandbox_profile.hpp"','#include "corners/sandbox_profile.hpp"\n#include "corners/sandbox_periodic.hpp"');s=s.replace('double sum = 0, maximum = 0, lo = INFINITY, hi = -INFINITY;','double sum = 0, maximum = 0, lo = INFINITY, hi = -INFINITY, model_sum=0,model_max=0;',1)
s=s.replace('sum += d;\n                maximum', '''sum += d;
                double corrected=sandbox::correctedDistance(p,line,e.sandbox_model_kind_[edge],e.sandbox_model_axis_[edge],e.sandbox_model_coefficient_[edge]);
                model_sum+=corrected;model_max=std::max(model_max,corrected);
                maximum''',1)
s=s.replace('mean > config.max_line_fit_error_ ||', '''((e.sandbox_model_kind_[edge]==0?mean:model_sum/support.size()) > config.max_line_fit_error_) ||''',1)
s=s.replace('// 无限线交点必须确实在两条拟合线上，不能只写一个任意凸四点。','''// 沙盒C：复算模型参数和校正残差，不允许把任意小残差写进内部证据。
            if(e.sandbox_model_kind_[edge]){
                if(!same(model_sum/support.size(),e.sandbox_model_mean_[edge])||!same(model_max,e.sandbox_model_max_[edge]))return fail("SANDBOX_MODEL_RESIDUAL_MISMATCH");
                if(e.sandbox_model_kind_[edge]==1){auto fitted=sandbox::fitPeriodic(support);if(!fitted.valid||fitted.axis!=e.sandbox_model_axis_[edge]||!same(fitted.coefficient,e.sandbox_model_coefficient_[edge]))return fail("SANDBOX_MODEL_PARAMETER_MISMATCH");}
            }
            // 无限线交点必须确实在两条拟合线上，不能只写一个任意凸四点。''',1);p.write_text(s)
