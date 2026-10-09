from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text().replace('#include <cstdlib>','#include <cstdlib>\n#include <memory>')
s=s.replace('std::set<std::pair<double, double>> support;', 'std::shared_ptr<std::set<std::pair<double, double>>> support;')
s=s.replace('            for (auto p : arc.points)\n                arc.support.emplace(p.x, p.y);','            arc.support=std::make_shared<std::set<std::pair<double,double>>>();\n            for (auto p : arc.points)\n                arc.support->emplace(p.x, p.y);').replace('arc.support.size()', 'arc.support->size()').replace('a.support.count(', 'a.support->count(')
pos=s.index('        // 指定有限模型边')
s=s[:pos]+'''        // 最慢帧实验：同一条原始连续支持的拟合与位置模式无关；调用内缓存并共享不可变支持集合。
        struct FitRecord{bool valid=false;cv::Vec4d line{};std::array<double,9> values{};int kind=0,axis=1;std::shared_ptr<std::set<std::pair<double,double>>> support;};
        using FitCache=std::map<std::tuple<int,size_t,size_t>,FitRecord>;
        bool cachedFit(Arc&arc,const CornerConfig&config,int kind,size_t first,size_t last,FitCache&cache){
            auto key=std::make_tuple(kind,first,last);auto it=cache.find(key);
            if(it==cache.end()){
                bool valid=fitArc(arc,config,kind);FitRecord record;record.valid=valid;
                if(valid){record.line=arc.line;record.values={arc.mean,arc.maximum,arc.model_mean,arc.model_max,arc.model_coefficient,arc.min_x,arc.max_x,arc.min_y,arc.max_y};record.kind=arc.model_kind;record.axis=arc.model_axis;record.support=arc.support;}
                cache.emplace(key,std::move(record));return valid;
            }
            const auto&r=it->second;if(!r.valid)return false;arc.line=r.line;arc.mean=r.values[0];arc.maximum=r.values[1];arc.model_mean=r.values[2];arc.model_max=r.values[3];arc.model_coefficient=r.values[4];arc.min_x=r.values[5];arc.max_x=r.values[6];arc.min_y=r.values[7];arc.max_y=r.values[8];arc.model_kind=r.kind;arc.model_axis=r.axis;arc.support=r.support;return true;
        }

''' +s[pos:]
s=s.replace('int model_kind=0)\n    {', 'int model_kind, FitCache&fit_cache)\n    {')
s=s.replace('bool started = false, ended = false, continuous = true;', 'bool started = false, ended = false, continuous = true;size_t fit_first=0,fit_last=0;')
s=s.replace('                        started = true;\n                        arc.points.push_back(p);','                        if(!started)fit_first=k;fit_last=k;\n                        started = true;\n                        arc.points.push_back(p);')
s=s.replace('if (!fitArc(arc, config, model_kind))','if (!cachedFit(arc, config, model_kind,fit_first,fit_last,fit_cache))')
pos=s.index('    // 沙盒路由由环境变量选择');head=s[:pos];tail=s[pos:];tail=tail.replace('        auto r=fitObservedEdgePairImpl(contour,edges,config,false,false);', '        FitCache fit_cache;auto run=[&](bool all,bool position,int kind=0){return fitObservedEdgePairImpl(contour,edges,config,all,position,kind,fit_cache);};\n        auto r=run(false,false);');tail=tail.replace('fitObservedEdgePairImpl(contour,edges,config,','run(');s=head+tail
# 上面的尾部替换也匹配了新lambda，复原其唯一内部调用。
s=s.replace('return run(all,position,kind,fit_cache);','return fitObservedEdgePairImpl(contour,edges,config,all,position,kind,fit_cache);')
p.write_text(s)
