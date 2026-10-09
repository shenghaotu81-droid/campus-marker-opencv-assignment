# 中文用途：位置必要条件沿弧流式应用，已不可能属于任一指定边的区间立即终止；不改变质量阈值。
from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text();s=s.replace('size_t source_front=0,source_back=0;', 'size_t source_front=0,source_back=0;\n            unsigned position_mask=3;',1)
s=s.replace('const double prefix_roundoff=', '''std::vector<unsigned> pixel_mask(contour.size(),3);
        if(!fitted_position)for(size_t k=0;k<contour.size();++k)for(unsigned e=0;e<2;++e)
          if(observed::segmentDistance(contour[k],edges[e][0],edges[e][1])>budget.max_edge_position_distance_px)pixel_mask[k]&=~(1u<<e);
        const double prefix_roundoff=''',1)
s=s.replace('arc.points.push_back(p);', '''arc.points.push_back(p);
                        if(!fitted_position){arc.position_mask&=pixel_mask[k];if(!arc.position_mask)break;}''',1)
s=s.replace('if (!continuous)\n                    ++rejected["discontinuous_arc"];', '''if(!arc.position_mask){++rejected["direction_or_position_arc"];continue;}
                if (!continuous)
                    ++rejected["discontinuous_arc"];''',1)
start=s.index('unsigned position_mask = 3;');end=s.index('if (!position_mask)',start)
# 删除原来的重复逐像素距离计算；原完整必要条件已在流式扫描处应用。
end2=s.index('if (!position_mask)',end+1) # 循环内break之外，第二个if是最终拒绝块
s=s[:start]+'unsigned position_mask = arc.position_mask;\n                    '+s[end2:]
p.write_text(s)
