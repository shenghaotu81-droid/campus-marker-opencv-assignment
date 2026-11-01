# R2与审查备注逐文件记录

依据仅docs/ref/G4-Codex.md及用户审查备注。首次与恢复时20/20文件均逐字匹配第4节，恢复时未重写完整文件。

- lib/tracking/kalman_cv.hpp/cpp：按文档原样落盘R2的内部position_time_bound_s参数；有界Ppp=2*(R1+Bpos²*(v v′+velocity_mse))，Pvv、精确时间分支及门槛不变。
- lib/tracking/target_tracker.cpp：按文档两处bootstrap分别传Bdt及profile单帧Bpos；按用户审查去掉两处裸.value()，构造验证及time_bounded的非空契约保护解引用。
- tools/validation/tracking_lab.cpp：按文档先从selection预读单帧界统计，再传bootstrap；已赋值optional改解引用，数值与来源未变。
- lib/config/tracking_config.cpp：按审查补bounded_only缺界错误G4_PROFILE_BOUNDED_TIME_BOUND_MISSING；有限非负语义保持，0合法，nominal_research可以未声明。
- tests/tracking_contract_test.cpp：保留文档R2数值断言，追加缺界验证/TargetTracker构造命名拒绝与显式零界合法回归。旧fixture/断言不变，目标数仍29。
- docs/g4.md/g4_acceptance.md：记录本轮实测及失败，保留后续关闭流程与三关OPEN。

## R2直接断言

R0=R1=0.01I、dt=0.2s、Bdt=0.02s、Bpos=0.01s、v=(1,0.5,0)m/s、q=0的fixture：velocity_mse_xx=2.493827160493827，原有界Ppp_xx=0.02m²，新期望0.0206987654320988m²，补项0.0006987654320988m²。Release/Debug/ASan主动near断言通过，容差1e-12；精确时间M10仍直接断言Ppp=0.04m²。测试数值未进入生产配置。

{
  "r0_and_r1_diagonal_m2": 0.01,
  "dt_s": 0.2,
  "Bdt_s": 0.02,
  "Bpos_s": 0.01,
  "velocity_mse_x_m2ps2": 2.4938271604938276,
  "old_Ppp_xx_m2": 0.02,
  "expected_Ppp_xx_m2": 0.020698765432098767,
  "assertion_tolerance": 1e-12,
  "direct_assertion": "Release/Debug/ASan contract passed"
}

## 文档历史复核与本地重跑差异

|域|文档隔离复核：样本/RMSE|本地重跑：样本/RMSE|差值：样本/RMSE|
|---|---:|---:|---:|
|camera_relative-nominal_research|12611 / 0.057741|12611 / 0.057740984|0 / -0.000000016|
|camera_relative-bounded_only|12481 / 0.069802|12481 / 0.069801801|0 / -0.000000199|
|local_world-nominal_research|12451 / 0.064506|12451 / 0.064506477|0 / +0.000000477|
|local_world-bounded_only|12381 / 0.077787|12381 / 0.077786546|0 / -0.000000454|

历史表为文档第8节编写者隔离复核，仅给六位小数；本地数据由本轮generate/calibrate/verify重新形成，样本一致，RMSE六位小数一致。差值仅相对于公开精度，不是改善结论。仓库没有本轮可用的R2前完整执行证据，不伪造旧报告或声称重跑旧算法；同fixture可直接复核补项前后。四域错误接受0、包含率不足、33ms未确认seed错误接受及沙箱内存首轮失败均如实保留。
