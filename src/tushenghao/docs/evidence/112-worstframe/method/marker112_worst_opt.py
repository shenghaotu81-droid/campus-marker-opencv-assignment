from pathlib import Path
p=Path('src/tushenghao/lib/corners/sandbox_periodic.hpp');s=p.read_text();pos=s.index('// 对同一a/b/c模型精化斜率')
s=s[:pos]+'''// 最慢帧实验：零斜率是同一周期模型的通用候选，不绑定帧号；保留全部像素和原质量门限。
inline PeriodicFit fitPeriodicZero(const std::vector<cv::Point2d>&points){
 auto result=fitPeriodic(points);if(!result.valid)return result;std::vector<double>groups[2];
 for(auto p:points)groups[phase(p,result.axis)>0?1:0].push_back(result.axis?p.x:p.y);
 double m[2];for(int k=0;k<2;++k){auto&v=groups[k];auto mid=v.begin()+v.size()/2;std::nth_element(v.begin(),mid,v.end());m[k]=*mid;if(v.size()%2==0)m[k]=.5*(m[k]+*std::max_element(v.begin(),mid));}
 double b=.5*(m[0]+m[1]);result.coefficient=.5*(m[1]-m[0]);result.line=result.axis?cv::Vec4d(0,1,b,result.line[3]):cv::Vec4d(1,0,result.line[2],b);return result;
}
''' +s[pos:];s=s.replace('kind==1||kind==3||kind==4','kind==1||kind==3||kind==4||kind==5');p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_evidence_validation.cpp');s=p.read_text().replace('>4||','>5||').replace('==1||e.sandbox_model_kind_[edge]==3||e.sandbox_model_kind_[edge]==4','==1||e.sandbox_model_kind_[edge]==3||e.sandbox_model_kind_[edge]==4||e.sandbox_model_kind_[edge]==5').replace('auto fitted=e.sandbox_model_kind_[edge]==4?', 'auto fitted=e.sandbox_model_kind_[edge]==5?sandbox::fitPeriodicZero(support):e.sandbox_model_kind_[edge]==4?');p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text().replace('model_kind==1||model_kind==3||model_kind==4','model_kind==1||model_kind==3||model_kind==4||model_kind==5').replace('auto f=model_kind==4?', 'auto f=model_kind==5?sandbox::fitPeriodicZero(arc.points):model_kind==4?')
s=s.replace('if(!r.evidence&&model==3){r=fitObservedEdgePairImpl(contour,edges,config,false,false,4);','''if(!r.evidence&&model==3){
            // 通用零斜率先给出可独立验算的候选；成功仍须全部原门控，不以超时丢弃病态输入。
            r=fitObservedEdgePairImpl(contour,edges,config,false,false,5);
            if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,false,true,5);
            if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false,5);
            if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,false,false,4);''')
# 保留j原排序，仅枚举连接长度必要条件可能成立的来源桶。
s=s.replace('for (size_t i = 0; i < arcs.size(); ++i)\n            for (size_t j = 0; j < arcs.size(); ++j)', '''std::vector<std::vector<size_t>> by_front(contour.size());
        for(size_t j=0;j<arcs.size();++j)by_front[arcs[j].source_front].push_back(j);
        for (size_t i = 0; i < arcs.size(); ++i){
            std::vector<size_t> near;
            size_t start=arcs[i].source_back;
            for(size_t step=0;step<contour.size();++step){size_t k=(start+step)%contour.size();double length=k>=start?walk_prefix[k]-walk_prefix[start]:walk_prefix.back()-walk_prefix[start]+walk_prefix[k];
                if(length-prefix_roundoff>budget.max_turn_connection_length_px)break;
                near.insert(near.end(),by_front[k].begin(),by_front[k].end());}
            std::sort(near.begin(),near.end());
            for (size_t j : near)''')
s=s.replace('        }\n        if (!result.evidence)', '        }}\n        if (!result.evidence)');p.write_text(s)
