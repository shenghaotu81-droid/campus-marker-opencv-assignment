# 中文用途：先比较标量质量与歧义，只有新最佳候选才复制完整支持证据；结果与原遍历顺序保持一致。
from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text();s=s.replace('int model_kind=0,model_axis=1;','int model_kind=0,model_axis=1;\n            double min_x=INFINITY,max_x=-INFINITY,min_y=INFINITY,max_y=-INFINITY;',1)
s=s.replace('for (auto p : arc.points)\n            {\n                double t', 'for (auto p : arc.points)\n            {\n                arc.min_x=std::min(arc.min_x,p.x);arc.max_x=std::max(arc.max_x,p.x);arc.min_y=std::min(arc.min_y,p.y);arc.max_y=std::max(arc.max_y,p.y);\n                double t',1)
s=s.replace('for (auto p : b.points)\n                    if (a.support.count', 'if(!(a.max_x<b.min_x||b.max_x<a.min_x||a.max_y<b.min_y||b.max_y<a.min_y))for (auto p : b.points)\n                    if (a.support.count',1)
s=s.replace('CornerEvidence evidence{};', '''// 所有门控均执行后，歧义检查仍无条件覆盖低分候选；只延迟深拷贝。
                const double candidate_error=first.mean+second.mean;
                if(result.evidence){
                    double delta=cv::norm(result.evidence->intersection_-intersection);
                    if(delta*delta>config.semantic_geometry_threshold_){result.evidence.reset();result.reason="AMBIGUOUS_EDGE_PAIR: 外边观测解几何冲突";return result;}
                    if(candidate_error>result.evidence->error_)continue;
                    if(candidate_error==result.evidence->error_){
                        std::array<int,2>new_begin{{int(first.begin),int(second.begin)}},new_end{{int(first.end),int(second.end)}};
                        std::array<int,2>old_begin{{result.evidence->observed_segment_ids_[0],result.evidence->observed_segment_ids_[1]}};
                        if(!(std::tie(new_begin,new_end)<std::tie(old_begin,result.evidence->observed_segment_end_ids_)))continue;
                    }
                }
                CornerEvidence evidence{};''',1)
start=s.index('if (result.evidence &&\n                    cv::norm');end=s.index('result.evidence = std::move(evidence);',start)+len('result.evidence = std::move(evidence);');s=s[:start]+'result.evidence = std::move(evidence);'+s[end:]
p.write_text(s)
