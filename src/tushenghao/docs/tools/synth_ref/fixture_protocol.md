# Fixture 协议 v1

case_id 是 `{config_hash,case:descriptor}` 规范化 JSON 的 SHA-256，完整64位十六进制。nlohmann JSON 默认对象键排序、紧凑 UTF-8 dump；数组次序有语义。配置 hash 为 `{model,grid,cases}`，不是原始文件空白的 hash；来源文档则使用原始字节 SHA-256。生成器代码摘要由 CMake 对排序后的源/头/测试/CMake及构建信息模板的文件 hash 联合计算，源码改变触发重新配置。

index 的 truth_line 从1计数，非适用字段为空。全局 descriptor.base_index 是 0起的基本网格序号，theta→ratio→width→phase_x→phase_y→offset 的固定顺序；local 按 theta→width→phase→piece表顺序→edge→offset；shear 独立网格；special 按 spec §5 顺序。plan 清单保存每个 descriptor 与对应 ID。

truth 每行含连续源顶点、固定 vertex_ids/edge_ids、完整矩阵、理想/扰动/裁切后顶点、计算面积、源/名义/可见锚点、删除/越界/扰动状态、构造操作、渲染配置与证据级别。来源锚点和标签是生成时已知的身份，不是检测结果。源单位 u 与独立形状的 work_px 显式区分。设计删除保留名义设计记录但 visible_vertices/visible_anchors 为空，和画外片不同。可见真值是工作图连续覆盖边界上的裁切多边形，可能和光栅掩膜面积不同。

perturbed_vertices 无效时为 null 并给 PERTURBATION_INVALID 原因，样例仍计入 manifest.invalid_cases，图片路径为空。退化共线 fixture 的 triangle_area=0、degenerate=true、inverse_evaluated=false，未调用逆矩阵。一般透视记3×3矩阵及有限分母检查，宽度不是全域常量因此 null。独立形状没有 MARK 笔画宽，相关字段为 null 并说明。禁止 NaN/Infinity。

灰度背景0、前景255，像素中心为整数，覆盖边界[-0.5,959.5]×[-0.5,719.5]。每像素子点(x−0.5+(i+0.5)/8,y−0.5+(j+0.5)/8)，i/j=0…7；凹多边形偶奇规则，边上点算内部；多片按64位子点集合取并集。gray=floor(255*count/64+0.5)，mask=255 iff gray>127。实例掩膜按每片单独 count>32，0为空、1…6为固定 MARK片ID，新增桥为7，压力片按构造序号加1，多个过半占用记65535。灰度/二值为单通道8位PNG，实例为单通道16位PNG；压缩级别3。二值半覆盖通过而实例半覆盖不通过，是规定的两个不同规则。

truth.semantic_hash 覆盖语义记录，排除 semantic_hash 自身、artifacts 和 artifact_hashes；这些三个字段之外的记录全参与 hash，包括 descriptor、fixture、面积和渲染信息。PNG hash 是实际编码字节 SHA-256，truth.jsonl 文件 hash 另存 manifest。运行路径/时间不进入语义记录。manifest 的环境、源码、来源、配置、批准记录、seed、subset、数量和完成状态是审计信息，不能拿不同目录的 manifest 字节比较复现。

桥接正式生成时用二值连通域标签检查 M 与指定 S 的像素确实同属一个分量；失败停止该run并留INCOMPLETE。白片压力正式生成时验证实际连通域数等于127/128/129。L压力只记录生成片数与4px宽模板，不称 matcher 候选数量。12个计数描述需外部audit构造执行路径，不提供虚假检测计数。对称 fixture 仅三锚点，省略全部白片独立边；列六种候选的已知锚点仿射关系与 identity真值对应，独立验证标 NOT_RUN。该矩阵由已知三点的坐标基底解析转换，并非从图像提取或最小二乘拟合，也不宣称六个都是完整结构合法解。

observation_template.csv 只有指定列头；无算法就没有测量行。实例掩膜不得充当算法识别结果。REFUSED/RESOURCE_LIMIT/误差/成功率/支持范围全部由外部算法在未来提供。

verify 重新枚举目标集合、逐行重建场景及语义 hash、重编码每张PNG、检查尺寸类型/像素/文件hash/清单数量和目录文件集合。除 verification.json 外只读产物。内部生成校验可在事务未完成状态运行；公开 verify 拒绝 INCOMPLETE。完整生成与 subset 都能通过各自覆盖检查，但 subset 不代表完整实验交付。运行版本不同会明确拒绝验证，不悄悄调整源记录。

锚点身份单独保存为 anchor_id（如L0.v3、M1.v4、S1a.aux_center）。名义坐标nominal_anchor保留原始误差参考；perturbed_anchor对L/M使用同一v编号的偏移后支撑线交点，对S保存已知投影辅助中心。visible_anchors含{id,point}，按当前几何与画布可见性判断，设计删除/无效形状为空；这些是已知构造位置，没有从图像估计锚点。
