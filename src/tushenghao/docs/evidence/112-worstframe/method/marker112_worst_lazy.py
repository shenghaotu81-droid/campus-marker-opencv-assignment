from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text();s=s.replace('        struct Arc\n', '        struct SupportSet{bool ready=false;std::set<std::pair<double,double>> pixels;};\n        struct Arc\n')
s=s.replace('std::shared_ptr<std::set<std::pair<double, double>>> support;', 'std::shared_ptr<SupportSet> support;').replace('std::shared_ptr<std::set<std::pair<double,double>>> support;', 'std::shared_ptr<SupportSet> support;')
a=s.index('            arc.support=std::make_shared');b=s.index('            arc.model_kind=',a)
s=s[:a]+'''            // 唯一点数只需证明达到既有下限；不为随后失败的弧构建整条树集合。
            std::vector<std::pair<double,double>> unique;unique.reserve(config.min_line_points_);
            for(auto p:arc.points){auto key=std::make_pair(p.x,p.y);if(std::find(unique.begin(),unique.end(),key)==unique.end())unique.push_back(key);if(unique.size()>=static_cast<size_t>(config.min_line_points_))break;}
            if(unique.size()<static_cast<size_t>(config.min_line_points_))return false;
''' +s[b:]
s=s.replace('            return hi - lo >= config.observation_budget_->min_support_span_px &&\n                   arc.model_mean <= config.max_line_fit_error_;', '            bool valid=hi - lo >= config.observation_budget_->min_support_span_px && arc.model_mean <= config.max_line_fit_error_;\n            if(valid)arc.support=std::make_shared<SupportSet>();return valid;')
s=s.replace('                    if (a.support->count({p.x, p.y}))', '                    if ((a.support->ready||( [&]{for(auto q:a.points)a.support->pixels.emplace(q.x,q.y);a.support->ready=true;return true;}())) && a.support->pixels.count({p.x,p.y}))')
p.write_text(s)
