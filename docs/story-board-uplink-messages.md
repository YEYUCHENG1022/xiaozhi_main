# 互动听书板端上行消息（V2）

本文与最终《互动听书板端通信协议》一致，只列新版板端会主动发送的消息。传输通道为设备与 `xiaozhi-server` 已建立的 WebSocket，消息为 UTF-8 JSON 文本帧。

## 公共信封

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "动作名称",
  "data": {}
}
```

## 1. 进入互动听书首页

触发：用户在功能首页点击“AI 听书”，或后端把语音意图归一为同一入口事件。

板端点击路径发送：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "enter",
  "data": {
    "request_id": "req_home_1"
  }
}
```

板端发送后只显示加载页，等待匹配 `request_id` 的 `home_result`，不得自行创建或恢复故事。

## 2. 开启新故事

触发：互动听书首页点击“开启新故事”。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "start",
  "data": {}
}
```

若板端已有明确题材，可在 `data` 中携带可选字符串 `theme`。发送后等待后端语音引导及 `show_confirmation`。

## 3. 继续上次故事

触发：`home_result.data.continue_available=true` 时，点击“继续上次故事”。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "resume",
  "data": {
    "session_id": "ips_01k_story"
  }
}
```

`session_id` 必须原样取自 `home_result.data.continue_play.session_id`。发送后等待 `show_confirmation(purpose=resume_confirm)`，不得直接进入播放页。

## 4. 打开历史故事

触发：点击 `home_result.data.items[]` 中的一条历史故事。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "open_history",
  "data": {
    "session_id": "ips_01k_history"
  }
}
```

`session_id` 必须原样取自被点击条目。确认弹窗由后端根据 `completion_status` 下发，板端不得自行生成。

## 5. 提交剧情 A/B 选择

触发：点击当前 `show_options` 页面中的 A 或 B。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "select_option",
  "data": {
    "session_id": "ips_01k_story",
    "node_id": "ipd_01k_choice",
    "option_id": "B"
  }
}
```

`session_id`、`node_id` 必须原样回传；`option_id` 只能是 `"A"` 或 `"B"`。点击后禁用按钮，直到收到匹配复合键的 `clear_options`。

## 6. 提交确认弹窗结果

触发：点击当前 `show_confirmation` 弹窗中的“是”或“否”。

点击“是”：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "confirm",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "history:ips_01k_story:a1b2c3d4",
    "confirmed": true
  }
}
```

点击“否”时仅将 `confirmed` 改为 JSON 布尔值 `false`。`session_id`、`confirmation_id` 必须来自当前弹窗并原样回传；不回传 `purpose`。

## 7. 请求退出互动听书

触发：互动听书首页、播放页或 A/B 选项页点击固定返回/Home 键。

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "exit",
  "data": {}
}
```

发送后保留当前页面和状态，等待 `show_confirmation(purpose=exit_confirm)`。用户再点击“是/否”时按第 6 节发送 `confirm`，收到匹配的 `clear_confirmation` 后才收敛本地页面状态。

## 8. 新版固件禁止发送的动作

新版流程不发送：

- `entry_decision`
- `select_history`
- `history_decision`
- `navigate`
- `history_request`
- `resume_last`

其中 `history_request`、`resume_last` 只属于旧固件兼容流程；新版统一使用 `enter → home_result`。
