# 中文用途：显式当前帧观测索引，原图只分割一次、组件匹配只做一次；不缓存跨帧图像或预测角。
from pathlib import Path
p=Path('src/tushenghao/lib/corners/corner_observation.hpp');s=p.read_text().replace('#include <string>','#include <string>\n#include <map>');s=s.replace('OriginalContourResult observeOriginalContour(','''// 沙盒性能实验：该索引只活在一次decode调用中；不使用static或跨帧缓存。
struct OriginalContourIndex {
    bool ready=false;
    std::vector<std::vector<cv::Point>> contours;
    std::map<size_t,OriginalContourResult> matched;
};
OriginalContourResult observeOriginalContour(const PreparedFrame&, const WhiteComponent&,
                                            const CornerObservationBudget&, OriginalContourIndex&);
OriginalContourResult observeOriginalContour(''',1);p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_observation.cpp');s=p.read_text();s=s.replace('const CornerObservationBudget &budget)\n    {','const CornerObservationBudget &budget, OriginalContourIndex &index)\n    {',1)
s=s.replace('cv::Mat gray, mask;','if(auto it=index.matched.find(component.component_id_);it!=index.matched.end())return it->second;\n        if(!index.ready){\n        cv::Mat gray, mask;',1)
s=s.replace('std::vector<std::vector<cv::Point>> contours;', 'auto &contours=index.contours;',1)
s=s.replace('sandbox::Scope mapping_profile(sandbox::Mapping);', 'index.ready=true;\n        }\n        const auto &contours=index.contours;\n        sandbox::Scope mapping_profile(sandbox::Mapping);',1)
# 保存所有成功/失败匹配；同一frame components和budget在调用内只读不变。
s=s.replace('return result;', 'index.matched[component.component_id_]=result;return result;')
pos=s.rfind('\n}');s=s[:pos]+'''\n    // 历史内部入口保持语义：每次单独调用都用新索引，fixture改图/预算不会命中旧缓存。
    OriginalContourResult observeOriginalContour(const PreparedFrame &frame,const WhiteComponent &component,
      const CornerObservationBudget &budget){OriginalContourIndex index;return observeOriginalContour(frame,component,budget,index);}
'''+s[pos:];p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_resolver.hpp');s=p.read_text().replace('namespace mark\n{','namespace mark\n{\n    struct OriginalContourIndex;\n    CornerResolution resolveObservedCorners(const PreparedFrame&,const GeometryHypothesis&,const MarkerGeometry&,const CornerConfig&,OriginalContourIndex&);',1);p.write_text(s)
p=Path('src/tushenghao/lib/corners/corner_resolver.cpp');s=p.read_text().replace('const MarkerGeometry &model, const CornerConfig &config)\n    {','const MarkerGeometry &model, const CornerConfig &config, OriginalContourIndex &index)\n    {',1).replace('*config.observation_budget_);','*config.observation_budget_, index);',1);pos=s.rfind('\n}');s=s[:pos]+'''\n    // 内部旧入口仍可用；正式decode把同帧索引在全部父假设间共享。
    CornerResolution resolveObservedCorners(const PreparedFrame&frame,const GeometryHypothesis&hypothesis,
      const MarkerGeometry&model,const CornerConfig&config){OriginalContourIndex index;return resolveObservedCorners(frame,hypothesis,model,config,index);}
'''+s[pos:];p.write_text(s)
p=Path('src/tushenghao/lib/pipeline/decode_stage.cpp');s=p.read_text().replace('#include "corners/corner_resolver.hpp"','#include "corners/corner_resolver.hpp"\n#include "corners/corner_observation.hpp"');s=s.replace('bool unresolved = false;', 'bool unresolved = false;\n            // 沙盒：一次decode的显式索引，不跨帧复用，不在无角点假设时提取原图。\n            OriginalContourIndex original_index;',1).replace('resolveObservedCorners(frame, hypothesis, model, config);','resolveObservedCorners(frame, hypothesis, model, config, original_index);');p.write_text(s)
