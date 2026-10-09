from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text()
s=s.replace('std::shared_ptr<std::set<std::pair<double,double>>> support;};','std::shared_ptr<std::set<std::pair<double,double>>> support;std::vector<cv::Point2d> points;};')
s=s.replace('using FitCache=std::map<std::tuple<int,size_t,size_t>,FitRecord>;', '''using FitCache=std::map<std::tuple<int,size_t,size_t>,FitRecord>;
        struct FitContext{FitCache fits;std::vector<std::vector<size_t>> excluded;std::vector<bool> ready;explicit FitContext(size_t n):excluded(n),ready(n,false){}};''')
s=s.replace('size_t first,size_t last,FitCache&cache){', 'size_t first,size_t last,FitCache&cache,const std::vector<cv::Point>&contour){')
s=s.replace('                bool valid=fitArc(arc,config,kind);', '                if(arc.points.empty())for(size_t k=first;;k=(k+1)%contour.size()){arc.points.emplace_back(contour[k]);if(k==last)break;}\n                bool valid=fitArc(arc,config,kind);')
s=s.replace('record.support=arc.support;', 'record.support=arc.support;record.points=arc.points;')
s=s.replace('arc.support=r.support;return true;', 'arc.support=r.support;arc.points=r.points;return true;')
s=s.replace('int model_kind, FitCache&fit_cache)', 'int model_kind, FitContext&context)')
s=s.replace('        std::vector<cv::Point> polygon;', '''        // 连续支持位置mask用前缀计数求交；端点邻域由同一cv::norm谓词惰性缓存，完全保留修剪语义。
        std::array<std::vector<size_t>,2> bad_prefix;for(unsigned e=0;e<2;++e){bad_prefix[e].resize(contour.size()+1);for(size_t k=0;k<contour.size();++k)bad_prefix[e][k+1]=bad_prefix[e][k]+(!(pixel_mask[k]&(1u<<e)));}
        auto excluded_at=[&](size_t endpoint)->const std::vector<size_t>&{if(!context.ready[endpoint]){auto&v=context.excluded[endpoint];for(size_t k=0;k<contour.size();++k)if(cv::norm(cv::Point2d(contour[k])-cv::Point2d(contour[endpoint]))<budget.turn_trim_distance_px)v.push_back(k);context.ready[endpoint]=true;}return context.excluded[endpoint];};
        std::vector<cv::Point> polygon;''')
s=s.replace('        std::map<std::string, size_t> rejected;', '        std::set<std::pair<size_t,size_t>> distinct_ranges;std::vector<size_t> excluded_offsets;\n        std::map<std::string, size_t> rejected;')
a=s.index('                bool started = false, ended = false, continuous = true;');b=s.index('                if(!arc.position_mask)',a)
s=s[:a]+'''                // 端点邻域中的剔除索引决定唯一连续支持；不再为每个候选扫描/复制整个区间。
                const size_t extent=(arc.end+contour.size()-arc.begin)%contour.size();
                excluded_offsets.clear();
                for(size_t endpoint:{arc.begin,arc.end})for(size_t k:excluded_at(endpoint)){size_t offset=(k+contour.size()-arc.begin)%contour.size();if(offset<=extent)excluded_offsets.push_back(offset);}
                std::sort(excluded_offsets.begin(),excluded_offsets.end());excluded_offsets.erase(std::unique(excluded_offsets.begin(),excluded_offsets.end()),excluded_offsets.end());
                size_t first_offset=0,last_offset=extent;for(size_t k:excluded_offsets){if(k==first_offset)++first_offset;else if(k>first_offset)break;}
                if(first_offset>extent){++rejected["short_or_residual_arc"];continue;}
                for(auto it=excluded_offsets.rbegin();it!=excluded_offsets.rend();++it){if(*it==last_offset){if(!last_offset)break;--last_offset;}else if(*it<last_offset)break;}
                bool continuous=true;for(size_t k:excluded_offsets)if(k>=first_offset&&k<=last_offset){continuous=false;break;}
                size_t fit_first=(arc.begin+first_offset)%contour.size(),fit_last=(arc.begin+last_offset)%contour.size();
                if(!fitted_position)for(unsigned e=0;e<2;++e){const auto&pref=bad_prefix[e];size_t bad=fit_last>=fit_first?pref[fit_last+1]-pref[fit_first]:pref.back()-pref[fit_first]+pref[fit_last+1];if(bad)arc.position_mask&=~(1u<<e);}
''' +s[b:]
s=s.replace('                    if(all_endpoints&&!arc.points.empty()){', '''                    if(last_offset-first_offset+1<static_cast<size_t>(config.min_line_points_)){++rejected["short_or_residual_arc"];continue;}
                    if(all_endpoints){if(!distinct_ranges.emplace(fit_first,fit_last).second)continue;for(size_t k=fit_first;;k=(k+1)%contour.size()){arc.points.emplace_back(contour[k]);if(k==fit_last)break;}}
                    if(all_endpoints&&!arc.points.empty()){''')
s=s.replace('fit_first,fit_last,fit_cache)', 'fit_first,fit_last,context.fits,contour)')
s=s.replace('FitCache fit_cache;auto run=', 'FitContext context(contour.size());auto run=').replace('all,position,kind,fit_cache)', 'all,position,kind,context)')
p.write_text(s)
