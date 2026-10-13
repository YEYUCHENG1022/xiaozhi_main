# 互动听书板端通信协议 V2（固件实现说明）

本文件记录当前固件实现所遵循的最终协议。消息通过既有 WebSocket 发送 UTF-8 JSON 文本帧，固定使用：

```json
{"type":"interactive_story","version":2,"action":"...","data":{}}
```

故事正文和语音提示继续走既有 TTS/音频链路；本文 JSON 只负责页面、按钮和状态同步。

## 能力声明

WebSocket `hello.features` 必须包含：

```json
{
  "interactive_story_ui": {
    "version": 2,
    "touch": true
  }
}
```

## 新版消息集合

上行：

| action | 用途 |
|---|---|
| `enter` | 进入互动听书首页，携带 `request_id` |
| `start` | 开始新故事，`theme` 可选 |
| `resume` | 继续首页推荐故事，携带 `session_id` |
| `open_history` | 打开历史故事，携带 `session_id` |
| `select_option` | 提交 A/B，携带 `session_id + node_id + option_id` |
| `confirm` | 提交确认结果，携带 `session_id + confirmation_id + confirmed` |
| `exit` | 请求退出，发送空 `data` |

下行：

| action | 板端处理 |
|---|---|
| `home_result` | 渲染历史列表、完成标志和续听入口 |
| `show_options` | 显示 A/B 页面 |
| `clear_options` | 复合键匹配后关闭 A/B 页面 |
| `show_confirmation` | 使用后端的标题和提示显示“是/否”弹窗 |
| `clear_confirmation` | 复合键匹配后关闭确认弹窗 |
| `history_result` | 旧固件兼容结果 |
| `resume_result` | 旧固件兼容结果 |

## home_result

板端使用：

- `request_id`：匹配最近一次 `enter`。
- `items[]`：最多显示 10 条，业务标识只使用 `session_id`。
- `completion_status`：`completed` 显示“已完成”，`incomplete` 显示“未完成”。
- `continue_available`：决定是否显示“继续上次故事”。
- `continue_play.session_id/title`：续听按钮的会话标识和展示标题。

点击历史条目只发送 `open_history`，不在本地推断或创建确认弹窗。

## 选项页复合键

板端保存当前 `session_id + node_id`。点击只提交一次并禁用按钮；只有收到完全匹配的 `clear_options` 才关闭当前页面。`reason` 只记录日志，不决定业务结果。

## 确认弹窗复合键

板端保存当前 `session_id + confirmation_id`，并原样回传。按钮固定为“是/否”，`confirmed` 必须是 JSON 布尔值。

`purpose` 可能是：

- `start_confirm`
- `resume_confirm`
- `replay_confirm`
- `exit_confirm`

只有收到完全匹配的 `clear_confirmation` 才关闭弹窗；迟到或不匹配的清理消息必须忽略。

## 页面约束

1. 点击 AI 听书后发送 `enter`，只显示加载页。
2. 收到匹配的 `home_result` 后才显示互动听书首页。
3. 点击 `start`、`resume`、`open_history` 后不得在本地直接进入播放页或生成弹窗。
4. A/B 和确认按钮点击后防连点，并等待后端 `clear_*` 收敛状态。
5. 点击返回/Home 发送统一 `exit`，不得立即销毁互动听书状态。
6. `session_id`、`node_id`、`confirmation_id` 均作为不透明字符串使用，不解析、不拼接、不修改。
7. 新版固件不发送 `entry_decision`、`select_history`、`history_decision`、`navigate`。
8. 新版入口不发送兼容动作 `history_request`、`resume_last`。

## Home 键补充行为

当前固件把 Home 定义为立即返回“AI 听书 / 小剧场”功能首页：

1. 先发送通用 `type=abort`，立即打断 TTS。
2. 再发送既有 `interactive_story/action=exit/data={}`。
3. 板端立即回功能首页，不展示退出弹窗。
4. 服务端下发 `show_confirmation(purpose=exit_confirm)` 后，板端在后台自动发送当前复合键对应的 `confirm{confirmed:true}`。
5. 收到匹配的 `clear_confirmation` 后完成状态收敛。

该流程没有新增互动听书 action。完整时序见
[boot-home-and-home-button-protocol.md](./boot-home-and-home-button-protocol.md)。

## 自然结束返回首页

最终正文真实播放完成后，服务端下发 `clear_home` 且 `reason=handoff`。板端收到后：

1. 停止可能已经启动的自动聆听；
2. 切换到 Idle；
3. 清理互动听书运行状态；
4. 返回“AI 听书 / 小剧场”功能首页。

迟到的 `tts/state=stop` 不会再次把设备切换到 Listening。
