# 互动听书后端未实现功能

> 本文为旧版兼容资料。当前板端 V2 协议见
> [interactive-story-board-v2.md](./interactive-story-board-v2.md)。

本文只列当前后端还需要补充的功能。所有消息复用现有 WebSocket。

## 1. 进入 AI 听书

板子发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "enter",
  "data": {}
}
```

后端处理：

1. 根据当前用户或 `Device-Id` 查询是否存在未听完的故事。
2. 有未完成故事时返回：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_resume",
  "data": {
    "story_id": "story_xxx",
    "summary": "关羽已经辞别曹操，准备出城寻找刘备"
  }
}
```

3. 没有未完成故事时查询历史并返回：

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

最多返回 10 条。没有历史也必须返回 `"stories":[]`，不能静默不响应。

## 2. 请求历史列表

用户在“继续听 / 新的故事”页面点击“新的故事”时，板子先进入历史页面，然后发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "previous_stories",
  "data": {}
}
```

后端查询历史并返回与上一节完全相同的 `show_library`。

用户语音说“我要听听过的故事”时，也调用同一个历史查询函数，并：

1. 向板子发送 `show_library`。
2. 取最近两条作为语音候选。
3. 播报两个标题和摘要，然后询问“您想听哪一个？”。
4. 将会话状态设为 `WAITING_HISTORY_SELECTION`。

等待选择时：

- “第一个 / 一”对应第一个 `story_id`。
- “第二个 / 二”对应第二个 `story_id`。
- 用户说标题时按标题匹配。
- 用户描述内容时，根据两条候选的标题、摘要和故事上下文判断。
- 无法确定时重新询问，不能随便选择。

匹配成功后进入下面的 `resume_story(story_id)`。

## 3. 继续播放历史或未完成故事

用户点击“继续听”或点击一条历史记录时，板子发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "resume_story",
  "data": {
    "story_id": "story_xxx"
  }
}
```

后端处理：

1. 校验该故事属于当前用户或设备。
2. 加载保存的题材、角色、章节、分支路径和生成上下文。
3. 发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_story_playback",
  "data": {
    "story_id": "story_xxx"
  }
}
```

4. 立即从保存点生成或播放后续故事，不能等待用户再次说话。
5. 按现有 TTS 流程发送 `tts:start`、字幕、Opus 音频和 `tts:stop`。

## 4. 创建新的故事

用户在历史页面点击“新的故事”时，板子发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "new_story",
  "data": {}
}
```

后端收到后必须立即处理：

1. 将会话状态设为 `AWAITING_TOPIC`。
2. 发送 `show_story_playback`；新故事尚未生成 ID 时允许 `story_id` 为空。
3. 立即播报“您想听什么故事？”，不能等待下一次用户语音才响应。
4. 播放完成后发送 `tts:stop`，让板子恢复“聆听中”。

```json
{"type":"tts","state":"start"}
{"type":"tts","state":"sentence_start","text":"您想听什么故事？"}
```

发送对应 Opus 音频后：

```json
{"type":"tts","state":"stop"}
```

用户回答“三国”时：

1. 因当前状态是 `AWAITING_TOPIC`，保存题材“三国”。
2. 状态改为 `AWAITING_ROLE`。
3. 播报“您想扮演哪位人物？”。
4. 用户回答角色后再接入现有故事生成流程。

语音说“新的故事”时，也必须调用同一个处理函数。

## 5. 返回到上一次分支选择

用户选择分支、开始播放分支故事后点击返回，板子会停止当前播报、恢复选择页面，并发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "back_to_choice",
  "data": {
    "story_id": "story_xxx",
    "choice_id": "choice_xxx"
  }
}
```

后端必须在故事锁内同步完成：

1. 停止该分支正在进行的 LLM/TTS 任务。
2. 撤销刚才生成但尚未确认保存的分支内容。
3. 将该 `choice_id` 从“已消费”恢复成可选择，或者生成新的 `choice_id`。
4. 将故事状态恢复为 `WAITING_CHOICE`。
5. 返回可用的选择节点：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "show_choices",
  "data": {
    "story_id": "story_xxx",
    "choice_id": "choice_new_or_reactivated",
    "scene_no": 3,
    "question": "面对敌军逼近，应当如何应对？",
    "options": {
      "A": "严阵以待",
      "B": "暂时撤离"
    }
  }
}
```

后端应先朗读问题和两个选项并发送 `tts:stop`，再发送 `show_choices`。设备不解析 TTS 文案来判断页面事件。

随后用户再次点击时，板子会正常发送 `select_choice`。后端必须允许这个恢复后的节点再次选择并生成新分支。

`back_to_choice` 和紧随其后的 `select_choice` 可能到达得很快，必须按同一 WebSocket 会话顺序处理，或使用 `story_id` 锁，不能并发乱序。

## 6. 返回和 Home 的会话清理

返回普通上级页面时，板子发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "navigate_back",
  "data": {}
}
```

点击 Home 时，板子发送：

```json
{
  "type": "interactive_story",
  "version": 1,
  "action": "home",
  "data": {}
}
```

板子会在本地切换页面，并在播放中额外发送现有 `abort`。后端只需要：

- 停止尚未结束的故事生成或 TTS；
- 保存当前故事进度；
- 清除 `AWAITING_TOPIC`、`AWAITING_ROLE`、`WAITING_HISTORY_SELECTION` 等临时状态；
- `home` 后将会话恢复为普通对话状态。

这两个动作不需要再返回页面消息。

## 验收

1. `enter` 必须返回 `show_resume` 或 `show_library`。
2. `previous_stories` 必须返回 `show_library`。
3. `resume_story` 后立即继续播放。
4. `new_story` 后立即询问故事题材。
5. 语音可以按序号、标题或内容选择两条历史候选。
6. `back_to_choice` 后再次选择能够生成新的分支故事。
7. `navigate_back/home` 后后端不再保留错误的故事问答状态。
