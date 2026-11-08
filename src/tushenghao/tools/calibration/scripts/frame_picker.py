#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
标定帧手动选择工具
用法: python3 frame_picker.py <视频路径>
操作:
  n / 右箭头 : 后跳 50 帧
  p / 左箭头 : 前跳 50 帧
  j / k      : 微调 ±1 帧
  s          : 标记当前帧为候选
  d          : 删除上一个标记
  q          : 退出并输出帧号列表
选帧要求（来自 acceptance.md）:
  - 49 个圆点全部可见
  - 覆盖中心/四角/边缘位置
  - 角度覆盖正对/倾斜/旋转
  - 距离覆盖近/中/远
  - 无运动模糊
"""

import sys
import math
import cv2


def main():
    if len(sys.argv) < 2:
        print(f"用法: {sys.argv[0]} <视频路径>")
        return 1

    video_path = sys.argv[1]
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"无法打开视频: {video_path}")
        return 1

    reported_count = cap.get(cv2.CAP_PROP_FRAME_COUNT)
    total = int(reported_count) if math.isfinite(reported_count) and reported_count > 0 else 0
    print(f"总帧数: {total}")

    picked = []
    idx = 0
    step = 50

    # 随机定位后的读取失败不是正常末尾；保留已选信息并以失败退出。
    completion = "READ_ERROR"
    try:
        while True:
            idx = max(0, min(idx, total - 1)) if total else max(0, idx)
            if not cap.set(cv2.CAP_PROP_POS_FRAMES, idx):
                print(f"定位帧失败: {idx}")
                break
            ok, frame = cap.read()
            if not ok or frame is None or frame.size == 0:
                print(f"读取帧失败: {idx}")
                break
            if total and idx == total - 1:
                # 定位式浏览仍可回看；到末帧只报告来源状态，不自动结束人工选择。
                print("来源状态: END_OF_INPUT（已到末帧，可回看或按 q 结束）")

            display = frame.copy()
            text = f"frame {idx}/{total or '?'}  picked {len(picked)}"
            cv2.putText(display, text, (20, 40),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
            cv2.imshow("frame picker (n/p=+-50, j/k=+-1, s=mark, d=unmark, q=quit)",
                       display)

            key = cv2.waitKey(0) & 0xFF
            if key == ord('q'):
                completion = "USER_FINISH"
                break
            elif key in (ord('n'), 83, ord('j')):
                idx += 1 if key == ord('j') else step
            elif key == ord('p') or key == 81:
                idx -= step
            elif key == ord('k'):
                idx -= 1
            elif key == ord('s'):
                if idx not in picked:
                    picked.append(idx)
                    print(f"标记: {idx}（共 {len(picked)} 个）")
            elif key == ord('d'):
                if picked:
                    removed = picked.pop()
                    print(f"取消: {removed}（剩 {len(picked)} 个）")
    finally:
        cap.release()
        cv2.destroyAllWindows()

    picked.sort()
    print("\n选中的帧号列表:")
    print(picked)
    print(f"\n运行结果: {completion}")
    if completion == "READ_ERROR":
        print("以下是读取失败前已选信息，本次选帧未正常完成：")
    else:
        print("可填入 calibration_input.yaml:")
    print(f"frame_list: {picked}")
    return 1 if completion == "READ_ERROR" else 0


if __name__ == "__main__":
    sys.exit(main())
