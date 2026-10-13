# 首页 / 互动听书 / 小剧场前后端通讯消息总表

本文以《互动听书板端通信协议》为基准，对照当前板端代码的实际收发行为。

说明：

- 上行：板端发送给 `xiaozhi-server`。
- 下行：`xiaozhi-server` 发送给板端。
- 互动听书业务固定使用 `type=interactive_story`、`version=2`。
- 故事正文和引导语继续使用原有 TTS/音频链路。
- 普通 STT 只用于显示字幕，板端不根据 STT 文本判断业务事件。
- 点击 AI 听书或小剧场时，板端不主动发送 `listen/start`。

---

## 1. 首页本地行为

开机后板端直接显示“AI 听书 / 小剧场”功能首页。

该页面是本地 UI，不发送任何业务消息。

点击按钮后才发送对应业务事件：

| 点击操作 | 板端上行 |
| --- | --- |
| AI 听书 | `interactive_story / enter` |
| 小剧场 | `anti_fraud / start` |

---

## 2. WebSocket 能力声明

### 2.1 板端上行：`hello`

WebSocket 建立时发送：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "features": {
    "mcp": true,
    "interactive_story_ui": {
      "version": 2,
      "touch": true
    },
    "anti_fraud_ui": {
      "version": 2,
      "touch": true
    }
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

实际值说明：

- `touch` 取当前板卡的触摸能力。
- `frame_duration` 取固件编译配置。
- `interactive_story_ui` 和 `anti_fraud_ui` 只在板卡声明支持互动故事 UI 时添加。
- `anti_fraud_ui` 不属于本次互动听书协议，是当前固件额外声明的小剧场能力。

---

## 3. 通用语音链路消息

这些消息不属于 `interactive_story` V2 业务信封。

### 3.1 板端上行：打断播报

Home 键打断语音时发送：

```json
{
  "session_id": "当前通用会话ID",
  "type": "abort"
}
```

只有唤醒词触发的打断才会增加：

```json
{
  "session_id": "当前通用会话ID",
  "type": "abort",
  "reason": "wake_word_detected"
}
```

### 3.2 服务端下行：TTS 开始

```json
{
  "type": "tts",
  "state": "start"
}
```

板端切换为播报状态。

### 3.3 服务端下行：TTS 字幕

```json
{
  "type": "tts",
  "state": "sentence_start",
  "text": "需要显示和播报的文字"
}
```

板端只显示字幕，不从 TTS 文本猜测页面事件或 A/B 选择。

### 3.4 服务端下行：TTS 结束

```json
{
  "type": "tts",
  "state": "stop"
}
```

板端沿用原有状态机：

- 当前监听模式为 `manual`：返回空闲。
- 当前监听模式为 `auto` 或 `realtime`：进入监听。
- 如果已经收到待显示的故事选项，TTS 结束时会再次尝试显示选项。

### 3.5 服务端下行：STT 字幕

```json
{
  "type": "stt",
  "text": "用户说的话"
}
```

板端只显示用户字幕，不根据该文本判断“进入故事”“选择 A/B”等业务动作。

---

## 4. 互动听书上行消息

### 4.1 `enter`：点击首页“AI 听书”

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

当前代码行为：

- `request_id` 由板端递增生成。
- 点击后显示“正在打开互动听书……”加载页。
- 不主动发送 `listen/start`。
- 等待后端返回 `home_result`。

### 4.2 `start`：点击“开始新故事”

当前板端没有本地题材，因此发送空 `data`：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "start",
  "data": {}
}
```

如果未来板端已经掌握明确题材，协议允许：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "start",
  "data": {
    "theme": "三国"
  }
}
```

当前固件不会自行填写 `theme`，题材、主题和角色由后端继续通过语音引导收集。

### 4.3 `resume`：点击首页“继续上次”

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

`session_id` 原样取自：

```text
home_result.data.continue_play.session_id
```

发送后板端不直接进入播放页，等待：

```text
show_confirmation(purpose=resume_confirm)
```

### 4.4 `open_history`：点击某条历史故事

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

`session_id` 原样取自 `home_result.data.items[]`。

板端不根据完成状态自行创建弹窗，等待后端下发：

- 未完成故事：`show_confirmation(purpose=resume_confirm)`。
- 已完成故事：`show_confirmation(purpose=replay_confirm)`。

### 4.5 `select_option`：点击剧情 A/B

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

当前代码行为：

- `session_id`、`node_id` 原样取自当前 `show_options`。
- `option_id` 只发送大写 `"A"` 或 `"B"`。
- 点击一次后立即禁用按钮。
- 等待匹配的 `clear_options` 关闭页面。
- 如果语音先在后端完成选择，后端下发相同节点的 `clear_options`，板端同样关闭页面。

### 4.6 `confirm`：点击确认弹窗“是/否”

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

点击“否”：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "confirm",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "history:ips_01k_story:a1b2c3d4",
    "confirmed": false
  }
}
```

当前代码使用 JSON 布尔值，不发送字符串，不回传 `purpose`。

### 4.7 `exit`：请求退出互动听书

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "exit",
  "data": {}
}
```

当前触发来源：

- 互动听书页面点击返回键。
- 互动听书页面点击 Home 键。

返回键和 Home 键的页面行为不同，详见第 9 节。

---

## 5. 互动听书下行消息

### 5.1 `home_result`：渲染互动听书首页

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "home_result",
  "data": {
    "request_id": "req_home_1",
    "items": [
      {
        "public_id": "ips_01k_done",
        "session_id": "ips_01k_done",
        "title": "草庐风云",
        "summary": "刘备三顾茅庐，终于请得诸葛亮出山。",
        "theme": "三国",
        "role_name": "诸葛亮",
        "status": "ended",
        "completion_status": "completed",
        "is_resumable": false,
        "ended_reason": "completed",
        "current_scene_no": 6,
        "target_scene_count": 6,
        "started_at": "2026-08-01T09:00:00",
        "updated_at": "2026-08-01T10:00:00",
        "ended_at": "2026-08-01T10:00:00"
      }
    ],
    "continue_available": true,
    "continue_play": {
      "public_id": "ips_01k_paused",
      "session_id": "ips_01k_paused",
      "title": "长坂坡风云",
      "status": "paused",
      "theme": "三国",
      "role_name": "赵云",
      "current_scene_no": 3,
      "target_scene_count": 6,
      "last_completed_scene_no": 2,
      "updated_at": "2026-08-05T08:30:00",
      "resume_brief": "您扮演赵云，故事讲到第3节。"
    }
  }
}
```

当前板端实际读取：

- `request_id`
- `items[].session_id`
- `items[].title`
- `items[].summary`
- `items[].completion_status`
- `items[].updated_at`
- `continue_available`
- `continue_play.session_id`
- `continue_play.title`

其余字段允许后端携带，板端当前忽略。

历史列表最多读取 10 条。

### 5.2 `show_options`：显示剧情 A/B

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "show_options",
  "data": {
    "session_id": "ips_01k_story",
    "node_id": "ipd_01k_choice",
    "step_no": 3,
    "step_count": 6,
    "title": "草庐风云",
    "prompt": "面对追兵，您准备怎么办？",
    "options": [
      {
        "id": "A",
        "label": "纵马上前迎战"
      },
      {
        "id": "B",
        "label": "下马混入步兵"
      }
    ]
  }
}
```

当前板端要求：

- `options` 必须正好两项。
- 两项中必须分别存在 `"A"` 和 `"B"`。
- `session_id + node_id` 用作当前页面复合键。
- 板端收到消息后直接显示选择页，显示时机由后端下发时机决定。

### 5.3 `clear_options`：关闭剧情选择页

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "clear_options",
  "data": {
    "session_id": "ips_01k_story",
    "node_id": "ipd_01k_choice",
    "reason": "selected"
  }
}
```

板端只在 `session_id + node_id` 与当前页面一致时关闭。

`reason` 必须是字符串，但只用于日志，不用于推断业务结果。

### 5.4 `show_confirmation`：显示确认弹窗

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "show_confirmation",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "exit:8f32a91bc614",
    "purpose": "exit_confirm",
    "title": "草庐风云",
    "prompt": "要退出互动听书并保存当前故事吗？"
  }
}
```

支持的业务用途：

- `start_confirm`
- `resume_confirm`
- `replay_confirm`
- `exit_confirm`

正常情况下，板端使用后端的 `title` 和 `prompt` 显示“是/否”弹窗。

### 5.5 `clear_confirmation`：关闭确认弹窗

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "clear_confirmation",
  "data": {
    "session_id": "ips_01k_story",
    "confirmation_id": "exit:8f32a91bc614",
    "reason": "selected"
  }
}
```

板端只在 `session_id + confirmation_id` 与当前弹窗一致时处理。

### 5.6 `history_result`：旧历史查询结果

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "history_result",
  "data": {
    "request_id": "req_history_0001",
    "items": []
  }
}
```

当前代码保留解析入口，但新版入口不会发送对应的 `history_request`。

### 5.7 `resume_result`：旧续听查询结果

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "resume_result",
  "data": {
    "request_id": "req_resume_0001",
    "available": true,
    "session_id": "ips_01k_story"
  }
}
```

无可恢复故事：

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "resume_result",
  "data": {
    "request_id": "req_resume_0001",
    "available": false,
    "session_id": null
  }
}
```

当前代码保留解析入口，但新版入口不会发送对应的 `resume_last`。

---

## 6. 兼容上行消息

当前新版入口不主动发送以下消息，仅协议保留兼容定义。

### 6.1 `history_request`

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "history_request",
  "data": {
    "request_id": "req_history_0001"
  }
}
```

### 6.2 `resume_last`

```json
{
  "type": "interactive_story",
  "version": 2,
  "action": "resume_last",
  "data": {
    "request_id": "req_resume_0001"
  }
}
```

---

## 7. 小剧场当前消息

小剧场使用 `type=anti_fraud`。这些消息不属于所附的互动听书协议。

### 7.1 板端上行：点击“小剧场”

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "start",
  "data": {}
}
```

### 7.2 服务端下行：显示小剧场 A/B

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "show_options",
  "data": {
    "session_id": "afs_01k_story",
    "node_id": "afd_01k_choice",
    "step_no": 1,
    "step_count": 6,
    "title": "反诈骗小剧场",
    "prompt": "面对陌生来电，您准备怎么办？",
    "options": [
      {
        "id": "A",
        "label": "按对方要求立即转账"
      },
      {
        "id": "B",
        "label": "挂断电话并联系家人核实"
      }
    ]
  }
}
```

### 7.3 板端上行：点击小剧场 A/B

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "select_option",
  "data": {
    "session_id": "afs_01k_story",
    "node_id": "afd_01k_choice",
    "option_id": "B"
  }
}
```

### 7.4 服务端下行：关闭小剧场 A/B

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "clear_options",
  "data": {
    "session_id": "afs_01k_story",
    "node_id": "afd_01k_choice",
    "reason": "selected"
  }
}
```

### 7.5 服务端下行：小剧场自然结束

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "completed",
  "request_id": "可选；服务端有对应请求时原样回带",
  "data": {
    "session_id": "afs_01k_story"
  }
}
```

板端收到后记录结束状态。最后一次 `tts/state=stop` 到达后，板端切换到 Idle、
不再启动自动聆听，并返回“AI 听书 / 小剧场”首页。如果 `completed` 晚于最后一次
`tts stop`，板端会立即停止已经启动的聆听并返回首页。

当前代码没有定义或发送：

- 小剧场确认弹窗消息
- 小剧场历史消息

### 7.6 板端上行：Home 主动退出小剧场

板端先发送通用 `abort`，再发送：

```json
{
  "type": "anti_fraud",
  "version": 2,
  "action": "exit",
  "request_id": "本次 Home 操作生成的唯一请求号",
  "data": {
    "session_id": "afs_01k_story"
  }
}
```

发送后板端立即清理本地小剧场状态并返回功能首页，不等待业务回包。退出后到达的
旧 `show_options` 会被忽略。若板端尚未从服务端收到任何小剧场 `session_id`，只能
发送通用 `abort` 并记录日志，不能伪造会话标识。

---

## 8. 当前完整交互时序

### 8.1 进入互动听书首页

```text
用户点击 AI 听书
板端  -> interactive_story / enter {request_id}
服务端 -> interactive_story / home_result
板端显示历史记录、继续上次和开始新故事
```

页面入口不发送 `listen/start`。

### 8.2 开始新故事

```text
用户点击“开始新故事”
板端  -> interactive_story / start {}
服务端 -> TTS 引导题材、主题或角色
服务端 -> interactive_story / show_confirmation {purpose=start_confirm}
用户点击“是/否”
板端  -> interactive_story / confirm {confirmed}
服务端 -> interactive_story / clear_confirmation
服务端 -> TTS 播放故事
```

### 8.3 继续上次

```text
用户点击“继续上次”
板端  -> interactive_story / resume {session_id}
服务端 -> interactive_story / show_confirmation {purpose=resume_confirm}
用户点击“是/否”
板端  -> interactive_story / confirm {confirmed}
服务端 -> interactive_story / clear_confirmation
服务端 -> TTS 回顾并继续故事
```

### 8.4 打开历史故事

```text
用户点击历史条目
板端  -> interactive_story / open_history {session_id}
服务端 -> interactive_story / show_confirmation
           未完成：purpose=resume_confirm
           已完成：purpose=replay_confirm
用户点击“是/否”
板端  -> interactive_story / confirm {confirmed}
服务端 -> interactive_story / clear_confirmation
```

### 8.5 剧情选择

```text
服务端 -> interactive_story / show_options
用户点击 A/B
板端  -> interactive_story / select_option
服务端 -> interactive_story / clear_options
服务端 -> TTS 播放后续剧情
```

语音选择由后端完成。板端普通 STT 不判断选择；后端完成语音选择后下发 `clear_options` 关闭板端页面。

### 8.6 返回键退出互动听书

```text
用户点击返回键
板端  -> interactive_story / exit {}
服务端 -> interactive_story / show_confirmation {purpose=exit_confirm}
用户点击“是/否”
板端  -> interactive_story / confirm {confirmed}
服务端 -> interactive_story / clear_confirmation
```

该流程与基准协议一致。

### 8.7 Home 键退出互动听书

```text
用户点击 Home
板端  -> 通用 abort
板端  -> interactive_story / exit {}
板端立即显示“AI 听书 / 小剧场”首页
服务端 -> interactive_story / show_confirmation {purpose=exit_confirm}
板端不显示弹窗，自动发送 confirm {confirmed=true}
服务端 -> interactive_story / clear_confirmation
```

该流程是当前固件为“Home 必须立即回首页”实现的特殊流程，与基准协议存在差异。

---

## 9. 相对所附协议的通讯变化

对照所附《互动听书板端通信协议》，当前互动听书的消息名称、版本和 `data` 字段均未修改：

- `enter`
- `start`
- `resume`
- `open_history`
- `select_option`
- `confirm`
- `exit`
- `home_result`
- `show_options`
- `clear_options`
- `show_confirmation`
- `clear_confirmation`
- 兼容的 `history_request/history_result`
- 兼容的 `resume_last/resume_result`

唯一变化是 **Home 键的退出时序**。

### 9.1 协议原流程

基准协议要求：

```text
exit -> 显示 exit_confirm -> 用户点击是/否 -> confirm -> clear_confirmation
```

当前 Home 键实际行为：

```text
abort -> exit -> 立即本地回首页
      -> 收到 exit_confirm 后自动 confirm(true)
      -> clear_confirmation
```

差异：

- Home 不显示退出确认弹窗。
- Home 默认等同于用户确认退出。
- Home 在收到 `clear_confirmation` 前已经清理本地故事状态。

其中：

- `abort` 是既有通用语音协议，不是新增的 `interactive_story` action。
- `exit` 的 JSON 格式与所附协议完全相同。
- 自动发送的 `confirm{confirmed:true}` 格式与所附协议完全相同，只是由 Home 流程自动触发，不再等待用户点击。
- 普通返回键仍严格按照所附协议显示退出确认弹窗，由用户点击“是/否”。
- 点击 AI 听书不发送 `listen/start`，这与所附互动听书协议一致。

除 Home 键上述时序变化外，没有修改其他互动听书前后端通讯消息。
