# 互动听书前后端通讯清单与协议（v1）

> 本文为旧版兼容资料。当前板端 V2 协议见
> [interactive-story-board-v2.md](./interactive-story-board-v2.md)。

本文依据当前固件代码整理，供 ESP32 前端与服务端联调。它同时说明哪些页面已经有真实通讯、哪些只是本地界面，以及服务端应如何驱动完整流程。

## 1. 当前结论

### 1.1 尚未形成后端闭环的功能

| 功能 | 当前状态 | 缺少内容 |
| --- | --- | --- |
| 小剧场 | 菜单、播放页、历史列表、分支页和 JSON 接口已完成 | 后端查询、生成、TTS 和语音意图路由 |
| 错误/超时提示 | 没有页面和消息处理 | 请求无响应时用户看不到失败原因，也不能重试 |
| 请求确认 | 没有 `request_id`、ACK 或结果码 | 只能依赖后端随后返回页面或 TTS，无法可靠判断某次点击是否成功 |

小剧场目前是可操作的前端空壳：点击会切换页面并发送本文第 7 节定义的消息，但不会在设备端生成内容或识别语音。

### 1.2 前端已实现、但完全依赖后端闭环

以下不是空壳，按钮已发送消息或页面已能接收消息，但业务数据、生成和状态保存都在后端：

| 页面/功能 | 前端已有 | 后端必须完成 |
| --- | --- | --- |
| 进入 AI 听书 | 发送 `enter` | 返回 `show_resume` 或 `show_library` |
| 未完成故事 | 展示摘要，发送 `resume_story` | 读取进度并立即继续 TTS |
| 历史故事 | 展示最多 10 条、支持手动滚动 | 查询并返回 `show_library` |
| 新的故事 | 发送 `new_story` 并进入播放页 | 清理旧状态，立即询问题材 |
| 分支选择 | 展示 A/B，发送 `select_choice` | 校验选择、生成分支并继续 TTS |
| 返回分支 | 本地恢复上一选择页，发送 `back_to_choice` | 回滚分支，重新开放该选择节点 |
| 字幕 | 所有故事页面共用底部字幕层 | 按 TTS `sentence_start` 持续发文本 |
| 故事动图 | 本地循环播放 | 无需后端通讯 |
| 小剧场 | 菜单、历史、播放、A/B 选择和结构化页面事件 | 返回列表数据、生成内容并处理结构化动作 |

### 1.3 当前已经接入的服务端下行动作

固件识别以下 AI 听书动作：

- `show_choices`
- `clear_choices`
- `show_library`
- `show_resume`
- `show_story_playback`
- `show_home`
- `clear_menu`

固件还识别以下小剧场动作：

- `show_theater_library`
- `show_theater_playback`
- `show_theater_choices`
- `clear_theater_choices`

其他 `interactive_story` 动作会记录 `Unknown action`，不会改变界面。

## 2. 传输与能力声明

所有互动听书 JSON 复用当前 WebSocket 文本通道；TTS 音频继续使用当前二进制 Opus 通道。

设备 Hello 在支持该 UI 的板型上包含：

```json
{
  "type": "hello",
  "version": 3,
  "features": {
    "interactive_story_ui": {
      "version": 1,
      "touch": true
    }
  },
  "transport": "websocket"
}
```

后端只有看到 `features.interactive_story_ui.version == 1`，才能下发本文的故事 UI 消息。`touch` 表示设备可以点击选择；没有触摸时仍可走语音选择。

互动听书统一消息外壳：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "动作名",
  "data": {}
}
```

要求：

- `type`、`version`、`action`、`data` 都必须存在。
- `version` 当前只能是数字 `1`。
- `data` 即使为空也必须是 JSON 对象。
- 同一 WebSocket 会话内，故事动作必须按接收顺序串行处理。
- `story_id` 只能访问当前用户或当前 `Device-Id` 所属数据。
- 后端内部控制标记（例如 `% start_interactive_story`、`% handle_exit_intent`）不得作为 `stt` 发给设备。

## 3. 设备发给后端的动作

### 3.1 `enter`：进入互动听书

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "enter",
  "data": {}
}
```

后端必须二选一响应：

- 有未完成故事：`show_resume`
- 没有未完成故事：`show_library`，即使没有历史也返回空数组

收到 `enter` 后不能继续留在普通聊天意图状态，也不能静默。

### 3.2 `previous_stories`：获取历史

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "previous_stories",
  "data": {}
}
```

后端返回 `show_library`。建议按 `updated_at` 倒序，最多返回 10 条。

### 3.3 `new_story`：创建新故事

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "new_story",
  "data": {}
}
```

后端处理顺序必须是：

1. 取消旧故事正在运行的 LLM/TTS 任务。
2. 清除 `WAITING_CHOICE`、历史选择候选和退出意图等临时状态。
3. 创建新故事草稿，会话状态改为 `AWAITING_TOPIC`。
4. 下发 `show_story_playback`。
5. 立即用 TTS 询问“您想听什么故事？”。
6. TTS 完成后等待用户回答题材。

禁止把 `new_story` 路由到退出或告别逻辑。

### 3.4 `resume_story`：继续指定故事

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "resume_story",
  "data": {
    "story_id": "story_001"
  }
}
```

后端应校验归属、加载题材/角色/章节/分支路径/生成上下文，发送 `show_story_playback`，然后立即从保存点继续生成和播放。不能再等用户说“继续讲”。

### 3.5 `select_choice`：选择分支

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "select_choice",
  "data": {
    "story_id": "story_001",
    "choice_id": "choice_003",
    "choice": "A"
  }
}
```

`choice` 只允许 `"A"` 或 `"B"`。后端应：

1. 在 `story_id` 对应的锁内校验 `choice_id` 仍处于 `WAITING_CHOICE`。
2. 幂等记录选择；同一选择的重复包不能生成两次。
3. 下发匹配 ID 的 `clear_choices`。
4. 状态改为 `BRANCH_PLAYING`。
5. 立即生成并播放所选分支。

### 3.6 `back_to_choice`：回到上一分支

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "back_to_choice",
  "data": {
    "story_id": "story_001",
    "choice_id": "choice_003"
  }
}
```

后端必须在同一个故事锁内：

1. 停止当前分支的 LLM/TTS。
2. 撤销尚未确认的分支内容。
3. 将节点恢复为可选择，或创建新的 `choice_id`。
4. 状态恢复为 `WAITING_CHOICE`。
5. 返回一条有效的 `show_choices`。

前端会立即恢复本地快照，因此 `back_to_choice` 后紧接着可能收到新的 `select_choice`。后端不能并发处理造成“节点已消费”。

### 3.7 `navigate_back` 和 `home`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "navigate_back",
  "data": {}
}
```

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "home",
  "data": {}
}
```

后端需停止未完成的生成/TTS、保存进度并清除临时问答状态。`home` 还必须恢复普通对话状态。设备在播放中点击导航时会同时走现有 abort 流程。

## 4. 后端发给设备的动作

### 4.1 `show_resume`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_resume",
  "data": {
    "story_id": "story_001",
    "summary": "诸葛亮已经答应出山，故事将从隆中继续。"
  }
}
```

`story_id` 必填且非空；`summary` 可为空字符串。

### 4.2 `show_library`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_library",
  "data": {
    "stories": [
      {
        "story_id": "story_001",
        "title": "卧龙未出山",
        "summary": "刘备第三次拜访诸葛亮……",
        "updated_at": "07/25",
        "scene_no": 7
      }
    ]
  }
}
```

字段要求：

- `stories` 必须是数组，空列表也发送 `[]`。
- `story_id`、`title` 必填。
- `summary`、`updated_at`、`scene_no` 可选。
- 固件最多读取前 10 条。

### 4.3 `clear_menu`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "clear_menu",
  "data": {}
}
```

用于关闭未完成/历史菜单，露出故事播放动图。应在 `new_story`、`resume_story` 开始播放或提问前发送。

### 4.4 `show_story_playback`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_story_playback",
  "data": {
    "story_id": "story_001"
  }
}
```

这是 AI 听书的权威播放页面事件。后端通过点击或语音进入新故事、恢复故事或继续播放后，都发送该事件。`story_id` 在新故事尚未落库时可以为空。

`clear_menu` 保留兼容，但语音驱动页面时应优先使用 `show_story_playback`，因为它会同时设置 AI 听书模式、当前页面和导航状态。

### 4.5 `show_choices`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_choices",
  "data": {
    "story_id": "story_001",
    "choice_id": "choice_003",
    "scene_no": 3,
    "question": "面对追兵逼近，您准备怎么办？",
    "options": {
      "A": "打开前门，亲自迎接刘备。",
      "B": "先观察使者，探听荆州内部的秘密。"
    }
  }
}
```

设备收到该结构化事件后不解析任何 TTS 文案。如果当前仍在播放，设备等待播放队列结束后显示选择页；如果已经空闲，则立即显示。

推荐的固定顺序是：后端朗读问题和两个选项，发送 `tts:stop`，再发送 `show_choices`。这样页面出现时机完全由结构化事件决定，不依赖“第一种/第二种”等具体措辞。

### 4.6 `clear_choices`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "clear_choices",
  "data": {
    "story_id": "story_001",
    "choice_id": "choice_003"
  }
}
```

两个 ID 都必填。设备会忽略与当前选择节点不一致的陈旧消息。

### 4.7 `show_home`

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_home",
  "data": {}
}
```

用于让设备回到 AI 听书/小剧场首页。通常只在后端主动终止故事流程时使用。

## 5. TTS、字幕和音频

故事文本沿用现有 TTS 协议：

```json
{"type":"tts","state":"start"}
{"type":"tts","state":"sentence_start","text":"您想听什么故事？"}
```

随后通过同一 WebSocket 发送对应二进制 Opus 音频，最后发送：

```json
{"type":"tts","state":"stop"}
```

要求：

- 每一段要显示的字幕都发送 `sentence_start.text`。
- 文本和随后音频的顺序必须一致。
- `stop` 只在本轮音频全部发送完后下发。
- `resume_story`、`select_choice`、用户回答题材/角色后，都必须主动开始下一轮 TTS，不能等待另一句唤醒语。

## 6. 语音入口必须复用同一状态机

设备收到普通 `stt` 时只显示用户字幕，不检查关键词、不判断故事意图，也不因为 `stt.text` 切换页面。

后端语音意图映射应为：

| 用户语音 | 后端调用 |
| --- | --- |
| “我要听故事”“讲个故事” | 与 `enter` 相同 |
| “听新的故事”“换个新故事” | 与 `new_story` 相同 |
| “听以前的/听过的故事” | 与 `previous_stories` 相同 |
| “继续讲”“接着听” | 与当前故事的 `resume_story` 相同 |
| 等待分支时说“A/第一个/左边” | 与 `select_choice(A)` 相同 |
| 等待分支时说“B/第二个/右边” | 与 `select_choice(B)` 相同 |

屏幕点击和语音必须进入相同的后端业务处理函数。区别只在入口：

```text
屏幕点击 → 设备发送 new_story → 后端 NewStory()
用户语音 → 后端识别为 new_story → 后端 NewStory()
```

业务处理完成后，后端向设备发送相同的最终页面事件，例如：

- 进入入口/历史：`show_resume` 或 `show_library`
- 开始或恢复播放：`show_story_playback`
- 到达选择节点：`show_choices`
- 退出故事：`show_home`

不再发送语音专属事件让前端二次判断意图。

特别注意：

- `% start_interactive_story`、`% handle_exit_intent` 是后端内部控制命令，不是用户语音，绝不能包装成 `{"type":"stt"}` 发给设备。
- “听新的故事”必须先清除旧故事的选择节点和恢复上下文，再进入 `AWAITING_TOPIC`。
- 说“从刚才这一节重新讲起”后，后端必须立即继续 TTS；只改变状态但不生成内容，会表现为设备回到 listening 等用户再次说话。

## 7. 小剧场前后端协议

小剧场复用故事历史列表、播放动图、底部字幕和 A/B 分支页面，但使用独立动作名和 `theater_id`，避免与 AI 听书的 `story_id` 状态互相覆盖。

### 7.1 点击小剧场

设备先立即显示小剧场重播选择页，再发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "theater_enter",
  "data": {}
}
```

后端查询历史并返回：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_theater_library",
  "data": {
    "theaters": [
      {
        "theater_id": "theater_001",
        "title": "卧龙未出山",
        "summary": "建安十二年，刘备第三次来到隆中……",
        "updated_at": "07/25",
        "scene_no": 7
      }
    ]
  }
}
```

`theaters` 必须是数组，空列表也返回 `[]`；固件最多显示前 10 条，列表支持触摸滚动。

### 7.2 点击新的故事

设备立即进入播放动图页并发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "theater_new",
  "data": {}
}
```

后端创建新的小剧场会话、生成内容并沿用现有 TTS/字幕协议。后端也可以显式确认播放页：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_theater_playback",
  "data": {
    "theater_id": "theater_002"
  }
}
```

### 7.3 点击之前听过的故事

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "theater_resume",
  "data": {
    "theater_id": "theater_001"
  }
}
```

设备会立即进入播放动图页。后端校验归属、恢复进度并直接开始 TTS，不能等待用户再说“继续”。

### 7.4 小剧场分支

后端需要用户选择时下发：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_theater_choices",
  "data": {
    "theater_id": "theater_001",
    "choice_id": "theater_choice_003",
    "scene_no": 3,
    "question": "知道自己的选择，将改变往后许多人的命运。",
    "options": {
      "A": "打开前门，亲自迎接刘备。",
      "B": "先观察使者，探听荆州内部的秘密。"
    }
  }
}
```

触摸选择后设备发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "theater_select_choice",
  "data": {
    "theater_id": "theater_001",
    "choice_id": "theater_choice_003",
    "choice": "B"
  }
}
```

后端确认选择后下发：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "clear_theater_choices",
  "data": {
    "theater_id": "theater_001",
    "choice_id": "theater_choice_003"
  }
}
```

随后立即生成选择后的内容并发送 TTS。播放分支时点击返回，设备发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "theater_back_to_choice",
  "data": {
    "theater_id": "theater_001",
    "choice_id": "theater_choice_003"
  }
}
```

后端处理方式与 AI 听书的 `back_to_choice` 一致。

### 7.5 返回和 Home

小剧场分别使用：

```json
{"type":"interactive_story","version":1,"action":"theater_navigate_back","data":{}}
{"type":"interactive_story","version":1,"action":"theater_home","data":{}}
```

后端应停止生成/TTS、保存进度并清理小剧场临时状态。

### 7.6 小剧场语音事件

设备端不进行小剧场语音识别或意图映射。后端完成 ASR、LLM 意图判断和业务处理后，直接发送与触摸处理结果相同的最终页面事件。

```text
用户说“打开小剧场”
  → 后端识别并进入小剧场状态
  → show_theater_library(theaters=[...])

用户说“新的故事”
  → 后端调用与 theater_new 相同的业务处理函数
  → show_theater_playback(theater_id=...)
  → TTS/字幕/音频

用户说“选第二个”
  → 后端在当前 choice_id 上执行与 theater_select_choice(B) 相同的处理
  → clear_theater_choices(theater_id, choice_id)
  → show_theater_playback(theater_id)
  → TTS/字幕/音频
```

前端不再接收 `theater_voice_result`，也不会读取原始语音文本来决定页面。

## 8. 推荐的后端状态机

| 状态 | 可接受输入 | 成功后的状态 |
| --- | --- | --- |
| `NORMAL` | `enter`、听故事语音 | `ENTRY_PENDING` |
| `ENTRY_PENDING` | 后端查询结果 | `MENU` |
| `MENU` | `new_story` | `AWAITING_TOPIC` |
| `MENU` | `resume_story` | `PLAYING` |
| `AWAITING_TOPIC` | 用户题材 | `AWAITING_ROLE` |
| `AWAITING_ROLE` | 用户角色或“旁观者” | `PLAYING` |
| `PLAYING` | 生成到分支点 | `WAITING_CHOICE` |
| `WAITING_CHOICE` | `select_choice`/语音选择 | `BRANCH_PLAYING` |
| `BRANCH_PLAYING` | `back_to_choice` | `WAITING_CHOICE` |
| 任意故事状态 | `home` | `NORMAL` |
| `NORMAL` / `MENU` | `theater_enter` | `THEATER_MENU` |
| `THEATER_MENU` | `theater_new` / `theater_resume` | `THEATER_PLAYING` |
| `THEATER_PLAYING` | 生成到分支点 | `THEATER_WAITING_CHOICE` |
| `THEATER_WAITING_CHOICE` | `theater_select_choice` | `THEATER_PLAYING` |
| 任意小剧场状态 | `theater_home` | `NORMAL` |

不要只靠 LLM 自由判断上述状态。结构化动作必须先由确定性的路由器处理，再调用故事生成。

## 9. 建议补充的 v1 兼容扩展

当前前端还没实现以下消息，建议双方下一轮一起补充：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "error",
  "data": {
    "request_action": "new_story",
    "code": "INVALID_STATE",
    "message": "故事服务暂时不可用",
    "recoverable": true
  }
}
```

同时建议给所有设备上行动作增加可选 `request_id`，后端原样带回页面结果或 `error`。这样可以处理重复点击、重连和超时，而不需要升级消息版本。

## 10. 端到端正确流程

### 10.1 新故事

```text
设备 new_story
  → 后端 show_story_playback
  → 后端 TTS：您想听什么故事？
  → 用户回答题材
  → 后端 TTS：您想扮演哪位人物？
  → 用户回答角色
  → 后端生成并连续发送故事 TTS
  → 后端 show_choices + 朗读 A/B
  → 用户点击 select_choice
  → 后端 clear_choices
  → 后端生成并播放所选分支
```

### 10.2 返回后重新选择

```text
设备 back_to_choice
  → 后端取消当前分支并恢复节点
  → 后端 show_choices
  → 设备再次 select_choice
  → 后端允许重新选择并生成新分支
```

### 10.3 继续历史故事

```text
设备 previous_stories
  → 后端 show_library
  → 设备 resume_story(story_id)
  → 后端 show_story_playback
  → 后端立即从保存点继续 TTS
```

## 11. 联调验收清单

1. `enter` 每次都在 3 秒内返回 `show_resume` 或 `show_library`。
2. 空历史返回 `show_library` + `"stories":[]`。
3. `new_story` 不触发告别，立即询问题材。
4. 回答题材后立即询问角色，回答角色后立即开始正文。
5. `resume_story` 不需要用户再说“继续讲”。
6. `show_choices` 的 ID、文字与随后 `clear_choices`、`select_choice` 完全一致。
7. 选择分支后返回，再选 A 或 B 都能继续生成。
8. 连续快速点击只产生一次有效选择。
9. Home/返回后不再残留 `AWAITING_TOPIC`、`AWAITING_ROLE` 或 `WAITING_CHOICE`。
10. 服务端日志可记录内部控制命令，但设备日志的 `Application: >>` 中不能出现 `% ...`。
11. 所有 TTS 句子都有 `sentence_start`，所有页面底部都能看到字幕。
12. 断线重连后，后端可根据用户/设备恢复一致的故事状态。
