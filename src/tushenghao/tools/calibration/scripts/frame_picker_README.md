# frame_picker.py 使用说明

手动选择标定帧的辅助工具。

## 用途

从标定视频（6966 帧）中手动挑选 20-30 个高质量帧，
用于相机标定。选出的帧号填入 `calibration_input.yaml` 的 `frame_list`。

## 运行

```bash
python3 frame_picker.py <视频路径>
```

示例：

```bash
python3 src/tushenghao/tools/calibration/scripts/frame_picker.py data/raw/calibration_video.avi
```

## 操作按键

| 按键 | 功能 |
|------|------|
| `n` 或 `→` | 后跳 50 帧 |
| `p` 或 `←` | 前跳 50 帧 |
| `j` | 后移 1 帧（微调） |
| `k` | 前移 1 帧（微调） |
| `s` | 标记当前帧为候选 |
| `d` | 删除上一个标记 |
| `q` | 退出并输出帧号列表 |

## 选帧标准（来自 acceptance.md）

每个选中的帧必须满足：

1. **49 个圆点全部可见**——缺一个都不行
2. **位置覆盖**——中心、四角、边缘都要有
3. **角度覆盖**——正对、倾斜、旋转都要有
4. **距离覆盖**——近、中、远都要有
5. **无运动模糊**——圆点边缘清晰

## 推荐流程

1. 先用 `n` 快速跳览整个视频，对标定板出现的位置有个大概印象
2. 回到开头，逐段挑选：
   - 前 1/3 选 7-10 帧（覆盖各种角度）
   - 中 1/3 选 7-10 帧
   - 后 1/3 选 7-10 帧
3. 每选一帧按 `s`，终端会显示已选数量
4. 选错按 `d` 撤销
5. 选够 20-30 帧后按 `q`，终端输出排序好的帧号列表

## 输出示例

```
选中的帧号列表:
[120, 345, 678, 1024, 1450, 1890, 2230, ...]

可直接填入 calibration_input.yaml:
frame_list: [120, 345, 678, 1024, 1450, 1890, 2230, ...]
```

## 下一步

把输出的 `frame_list` 复制到
`src/tushenghao/tools/calibration/configs/calibration_input.yaml`，
替换原来的空列表 `frame_list: []`。

## 注意事项

- 这是离线辅助工具，不参与编译，不影响 C++ 工程
- 需要 Python + OpenCV：`pip install opencv-python`
- WSL 下运行需要 X11 转发（`cv2.imshow` 弹窗口），
  如果窗口弹不出，去 Windows PowerShell 跑 `wsl --shutdown`，重进 WSL 即可
