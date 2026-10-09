# 中文用途：在当前沙盒分支实际落地A+B，保留原始成功解，先验证996与成本；不修改质量阈值。
from pathlib import Path
import subprocess
path=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=subprocess.check_output(['git','show','HEAD:'+str(path)],text=True)
s=s.replace('#include <sstream>','#include <sstream>\n#include <cstdlib>')
s=s.replace('const CornerObservationBudget &budget)\n        {','const CornerObservationBudget &budget, bool fitted_position)\n        {',1)
s=s.replace('observed::segmentDistance(p, edge[0], edge[1])','observed::segmentDistance(fitted_position ? cv::Point2d(arc.line[2],arc.line[3])+cv::Point2d(arc.line[0],arc.line[1])*((p-cv::Point2d(arc.line[2],arc.line[3])).dot(cv::Point2d(arc.line[0],arc.line[1]))) : p, edge[0], edge[1])')
s=s.replace('EdgePairFitResult fitObservedEdgePair(', 'static EdgePairFitResult fitObservedEdgePairImpl(',1)
s=s.replace('const CornerConfig &config)\n    {\n        EdgePairFitResult result;', 'const CornerConfig &config, bool all_endpoints, bool fitted_position)\n    {\n        EdgePairFitResult result;',1)
s=s.replace('std::sort(indices.begin(), indices.end());','// 沙盒A：失败后枚举全部真实端点，不删区间中的像素。\n        if(all_endpoints){indices.clear();for(size_t k=0;k<contour.size();++k)indices.push_back(k);}\n        std::sort(indices.begin(), indices.end());',1)
s=s.replace('std::vector<Arc> arcs;','std::vector<Arc> arcs;\n        std::set<std::vector<std::pair<double,double>>> distinct_support;',1)
s=s.replace('// 有限模型边的位置检查无需拟合，先排除跨过其他外边的长区间。','''// 沙盒A：只有完整有序支持序列相同才去重，避免回走路径误合并。
                    if(all_endpoints&&!arc.points.empty()){std::vector<std::pair<double,double>> key;for(auto q:arc.points)key.emplace_back(q.x,q.y);if(!distinct_support.insert(key).second)continue;}
                    // 有限模型边的位置检查无需拟合，先排除跨过其他外边的长区间。''',1)
s=s.replace('for (auto p : arc.points)\n                    {','if(!fitted_position)for (auto p : arc.points)\n                    {',1)
s=s.replace('matches(arc, edges[edge], budget)','matches(arc, edges[edge], budget, fitted_position)')
pos=s.rfind('\n}')
s=s[:pos]+'''\n    // 沙盒路由由环境变量选择，仅供比较；正式实施应使用实例内策略而非全局开关。
    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point>&contour,
      const std::array<std::array<cv::Point2d,2>,2>&edges,const CornerConfig&config){
        auto r=fitObservedEdgePairImpl(contour,edges,config,false,false);
        const char*route=std::getenv("MARK_SANDBOX_ROUTE");
        if(!route||std::string(route)=="baseline"||r.evidence)return r;
        r=fitObservedEdgePairImpl(contour,edges,config,false,true);
        if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false);
        return r;
    }
'''+s[pos:];path.write_text(s)
