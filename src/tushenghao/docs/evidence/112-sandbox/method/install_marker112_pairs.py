# 中文用途：把可证无效的长连接弧先以O(1)前缀弧长排除，保留近阈值原式复算；缓存原像素首次索引。
from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text();s=s.replace('size_t begin, end;', 'size_t begin, end;\n            size_t source_front=0,source_back=0;',1)
s=s.replace('const auto &budget = *config.observation_budget_;','''const auto &budget = *config.observation_budget_;
        // 沙盒性能：前缀只是必要条件过滤器；近预算处仍执行原逐段加法。
        std::vector<double> walk_prefix(contour.size()+1,0);
        std::map<std::pair<int,int>,size_t> first_pixel;
        for(size_t k=0;k<contour.size();++k){first_pixel.emplace(std::make_pair(contour[k].x,contour[k].y),k);walk_prefix[k+1]=walk_prefix[k]+cv::norm(contour[(k+1)%contour.size()]-contour[k]);}
        const double prefix_roundoff=64*std::numeric_limits<double>::epsilon()*contour.size()*std::max(1.,walk_prefix.back());''',1)
s=s.replace('if (arc.edge_mask)\n                        arcs.push_back(std::move(arc));','''if (arc.edge_mask){
                        arc.source_front=first_pixel.at({cvRound(arc.points.front().x),cvRound(arc.points.front().y)});
                        arc.source_back=first_pixel.at({cvRound(arc.points.back().x),cvRound(arc.points.back().y)});
                        arcs.push_back(std::move(arc));
                    }''',1)
s=s.replace('bool shared = false;', '''// 先按有序轮廓真实弧长做保守排除，避免对数百万不邻接弧对做set交集。
                const size_t start_index=a.source_back,stop_index=b.source_front;
                const double connection_estimate=stop_index>=start_index?walk_prefix[stop_index]-walk_prefix[start_index]:walk_prefix.back()-walk_prefix[start_index]+walk_prefix[stop_index];
                if(connection_estimate-prefix_roundoff>budget.max_turn_connection_length_px){++rejected["connection"];continue;}
                bool shared = false;''',1)
s=s.replace('''auto begin = std::find(contour.begin(), contour.end(), cv::Point(a.points.back()));
                auto end = std::find(contour.begin(), contour.end(), cv::Point(b.points.front()));
                size_t k = static_cast<size_t>(begin - contour.begin()),
                       stop = static_cast<size_t>(end - contour.begin());''','''size_t k=start_index,stop=stop_index;''',1)
p.write_text(s)
