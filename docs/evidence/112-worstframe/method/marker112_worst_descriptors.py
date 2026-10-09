from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text()
s=s.replace('struct FitContext{FitCache fits;', 'struct Range{size_t first=0,last=0,count=0;unsigned mask=3;bool continuous=true;};\n        struct FitContext{std::array<std::vector<Range>,2> ranges;FitCache fits;')
s=s.replace('if(!fitted_position)for(size_t k=0;k<contour.size();++k)', 'for(size_t k=0;k<contour.size();++k)')
s=s.replace('        std::set<std::pair<size_t,size_t>> distinct_ranges;std::vector<size_t> excluded_offsets;', '        std::set<std::pair<size_t,size_t>> distinct_ranges;std::vector<size_t> excluded_offsets;\n        // 同一调用的各拟合模型重复使用同一端点/支持描述表，枚举顺序仍为原顺序。\n        auto&family=context.ranges[all_endpoints?1:0];bool reuse_ranges=!family.empty();size_t range_cursor=0;')
a=s.index('                // 端点邻域中的剔除索引');b=s.index('                if(!arc.position_mask)',a)
s=s[:a]+'''                Range range;
                if(reuse_ranges)range=family[range_cursor++];else{
                    const size_t extent=(arc.end+contour.size()-arc.begin)%contour.size();excluded_offsets.clear();
                    for(size_t endpoint:{arc.begin,arc.end})for(size_t k:excluded_at(endpoint)){size_t offset=(k+contour.size()-arc.begin)%contour.size();if(offset<=extent)excluded_offsets.push_back(offset);}
                    std::sort(excluded_offsets.begin(),excluded_offsets.end());excluded_offsets.erase(std::unique(excluded_offsets.begin(),excluded_offsets.end()),excluded_offsets.end());
                    size_t first_offset=0,last_offset=extent;for(size_t k:excluded_offsets){if(k==first_offset)++first_offset;else if(k>first_offset)break;}
                    if(first_offset<=extent){
                        for(auto it=excluded_offsets.rbegin();it!=excluded_offsets.rend();++it){if(*it==last_offset){if(!last_offset)break;--last_offset;}else if(*it<last_offset)break;}
                        for(size_t k:excluded_offsets)if(k>=first_offset&&k<=last_offset){range.continuous=false;break;}
                        range.first=(arc.begin+first_offset)%contour.size();range.last=(arc.begin+last_offset)%contour.size();range.count=last_offset-first_offset+1;
                        for(unsigned e=0;e<2;++e){const auto&pref=bad_prefix[e];size_t bad=range.last>=range.first?pref[range.last+1]-pref[range.first]:pref.back()-pref[range.first]+pref[range.last+1];if(bad)range.mask&=~(1u<<e);}
                    }
                    family.push_back(range);
                }
                if(!range.count){++rejected["short_or_residual_arc"];continue;}
                bool continuous=range.continuous;size_t fit_first=range.first,fit_last=range.last;arc.position_mask=fitted_position?3:range.mask;
''' +s[b:]
s=s.replace('last_offset-first_offset+1<static_cast<size_t>(config.min_line_points_)', 'range.count<static_cast<size_t>(config.min_line_points_)')
p.write_text(s)
