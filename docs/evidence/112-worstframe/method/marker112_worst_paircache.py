from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_edge_fit.cpp');s=p.read_text().replace('#include <memory>','#include <memory>\n#include <unordered_map>');s=s.replace('size_t source_front=0,source_back=0;', 'size_t source_front=0,source_back=0,fit_identity=0;');s=s.replace('struct FitRecord{bool valid=false;', 'struct FitRecord{size_t identity=0;bool valid=false;');s=s.replace('FitRecord record;record.valid=valid;', 'FitRecord record;record.valid=valid;record.identity=cache.size();arc.fit_identity=record.identity;');s=s.replace('arc.line=r.line;arc.mean=', 'arc.fit_identity=r.identity;arc.line=r.line;arc.mean=');
needle='        const double winding = cv::contourArea(contour, true);';s=s.replace(needle,'''        // 相同支持的几何配对仅验算一次；每个端点别名仍独立执行歧义/评分/来源tie-break。
        struct PairMemo{bool valid=false;std::string reason;cv::Point2d intersection;double corner_error=0,ea=0,eb=0;std::vector<cv::Point2d> connector;};
        struct PairHash{size_t operator()(const std::pair<size_t,size_t>&p)const{return p.first^(p.second+size_t(0x9e3779b9)+(p.first<<6)+(p.first>>2));}};
        std::unordered_map<std::pair<size_t,size_t>,PairMemo,PairHash> pair_cache;
''' +needle)
a=s.index('                // 先按有序轮廓真实弧长');b=s.index('                // 所有门控均执行后',a);body=s[a:b]
import re
body=re.sub(r'\+\+rejected\["([^"]+)"\];',r'memo.reason="\1";',body).replace('continue;', 'return false;')
body+='''                memo.intersection=intersection;memo.corner_error=corner_error;memo.ea=ea;memo.eb=eb;memo.connector=std::move(connector);return true;
'''
replacement='''                auto key=std::make_pair(a.fit_identity,b.fit_identity);auto it=pair_cache.find(key);
                if(it==pair_cache.end()){
                    PairMemo memo;auto compute=[&]()->bool{
''' +body+'''                    };memo.valid=compute();it=pair_cache.emplace(key,std::move(memo)).first;
                }
                const auto&memo=it->second;if(!memo.valid){if(!memo.reason.empty())++rejected[memo.reason];continue;}
                const Arc&first=direct?a:b;const Arc&second=direct?b:a;
                const auto&connector=memo.connector;auto intersection=memo.intersection;double corner_error=memo.corner_error,ea=memo.ea,eb=memo.eb;
'''
s=s[:a]+replacement+s[b:];p.write_text(s)
